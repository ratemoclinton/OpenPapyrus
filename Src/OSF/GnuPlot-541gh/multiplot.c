// GNUPLOT - term.c 
// Copyright 1986 - 1993, 1998, 2004   Thomas Williams, Colin Kelley
//
/*
 * Bookkeeping and support routines for 'set multiplot layout ...'
 * Jul 2004 Volker Dobler     layout rows, columns
 * Feb 2013 Christoph Bersch  layout margins spacing
 * Mar 2014 Ethan A Merritt   refactor into separate file (used to be in term.c)
 */
#include <gnuplot.h>
#pragma hdrstop

static void mp_layout_size_and_offset();
static void mp_layout_margins_and_spacing();
static void mp_layout_set_margin_or_spacing(GpPosition *);

enum set_multiplot_id {
	S_MULTIPLOT_LAYOUT,
	S_MULTIPLOT_COLUMNSFIRST, S_MULTIPLOT_ROWSFIRST, S_MULTIPLOT_SCALE,
	S_MULTIPLOT_DOWNWARDS, S_MULTIPLOT_UPWARDS,
	S_MULTIPLOT_OFFSET, S_MULTIPLOT_TITLE,
	S_MULTIPLOT_MARGINS, S_MULTIPLOT_SPACING,
	S_MULTIPLOT_INVALID
};

static const struct gen_table set_multiplot_tbl[] =
{
	{ "lay$out", S_MULTIPLOT_LAYOUT },
	{ "col$umnsfirst", S_MULTIPLOT_COLUMNSFIRST },
	{ "row$sfirst", S_MULTIPLOT_ROWSFIRST },
	{ "down$wards", S_MULTIPLOT_DOWNWARDS },
	{ "up$wards", S_MULTIPLOT_UPWARDS },
	{ "sca$le", S_MULTIPLOT_SCALE },
	{ "off$set", S_MULTIPLOT_OFFSET },
	{ "ti$tle", S_MULTIPLOT_TITLE },
	{ "ma$rgins", S_MULTIPLOT_MARGINS },
	{ "spa$cing", S_MULTIPLOT_SPACING },
	{ NULL, S_MULTIPLOT_INVALID }
};

#define MP_LAYOUT_DEFAULT {          \
		FALSE, /* auto_layout */         \
		0, /* current_panel */       \
		0, 0, /* num_rows, num_cols */  \
		FALSE, /* row_major */           \
		TRUE, /* downwards */           \
		0, 0, /* act_row, act_col */    \
		1, 1, /* xscale, yscale */      \
		0, 0, /* xoffset, yoffset */    \
		FALSE, /* auto_layout_margins */ \
		{screen, screen, screen, 0.1, -1, -1}, /* lmargin */ \
		{screen, screen, screen, 0.9, -1, -1}, /* rmargin */ \
		{screen, screen, screen, 0.1, -1, -1}, /* bmargin */ \
		{screen, screen, screen, 0.9, -1, -1}, /* tmargin */ \
		{screen, screen, screen, 0.05, -1, -1}, /* xspacing */ \
		{screen, screen, screen, 0.05, -1, -1}, /* yspacing */ \
		0, 0, 0, 0, /* prev_ sizes and offsets */ \
		DEFAULT_MARGIN_POSITION, \
		DEFAULT_MARGIN_POSITION, \
		DEFAULT_MARGIN_POSITION, \
		DEFAULT_MARGIN_POSITION, /* prev_ margins */ \
		EMPTY_LABELSTRUCT, 0.0 \
}

static struct MpLayout_ {
	MpLayout_() : auto_layout(false), current_panel(0), num_rows(0), num_cols(0), row_major(false), downwards(true), act_row(0), act_col(0),
		xscale(1.0), yscale(1.0), xoffset(0.0), yoffset(0.0), auto_layout_margins(false),
		prev_xsize(0.0), prev_ysize(0.0), prev_xoffset(0.0), prev_yoffset(0.0), title_height(0.0)
	{
		lmargin.Set(screen, screen, screen, 0.1, -1.0, -1.0);
		rmargin.Set(screen, screen, screen, 0.9, -1.0, -1.0);
		bmargin.Set(screen, screen, screen, 0.1, -1.0, -1.0);
		tmargin.Set(screen, screen, screen, 0.9, -1.0, -.01);
		xspacing.Set(screen, screen, screen, 0.05, -1.0, -1.0);
		yspacing.Set(screen, screen, screen, 0.05, -1.0, -1.0);
		prev_lmargin.SetDefaultMargin();
		prev_rmargin.SetDefaultMargin();
		prev_tmargin.SetDefaultMargin();
		prev_bmargin.SetDefaultMargin();
		//EMPTY_LABELSTRUCT, 0.0
	}
	bool   auto_layout;   // automatic layout if true 
	int    current_panel; // initialized to 0, incremented after each plot 
	int    num_rows;      // number of rows in layout 
	int    num_cols;      // number of columns in layout 
	bool   row_major;     // row major mode if true, column major else 
	bool   downwards;     // prefer downwards or upwards direction 
	int    act_row;       // actual row in layout 
	int    act_col;       // actual column in layout 
	double xscale;        // factor for horizontal scaling 
	double yscale;        // factor for vertical scaling 
	double xoffset;       // horizontal shift 
	double yoffset;       // horizontal shift 
	bool   auto_layout_margins;
	GpPosition lmargin;
	GpPosition rmargin;
	GpPosition bmargin;
	GpPosition tmargin;
	GpPosition xspacing;
	GpPosition yspacing;
	double prev_xsize;
	double prev_ysize;
	double prev_xoffset;
	double prev_yoffset;
	GpPosition prev_lmargin;
	GpPosition prev_rmargin;
	GpPosition prev_tmargin;
	GpPosition prev_bmargin;
	// values before 'set multiplot layout' 
	text_label title;    // goes above complete set of plots 
	double title_height; // fractional height reserved for title 
} mp_layout;// = MP_LAYOUT_DEFAULT;

/* Helper routines */
void multiplot_next()
{
	mp_layout.current_panel++;
	if(mp_layout.auto_layout) {
		if(mp_layout.row_major) {
			mp_layout.act_row++;
			if(mp_layout.act_row == mp_layout.num_rows) {
				mp_layout.act_row = 0;
				mp_layout.act_col++;
				if(mp_layout.act_col == mp_layout.num_cols) {
					/* GPO.IntWarn(NO_CARET,"will overplot first plot"); */
					mp_layout.act_col = 0;
				}
			}
		}
		else { /* column-major */
			mp_layout.act_col++;
			if(mp_layout.act_col == mp_layout.num_cols) {
				mp_layout.act_col = 0;
				mp_layout.act_row++;
				if(mp_layout.act_row == mp_layout.num_rows) {
					/* GPO.IntWarn(NO_CARET,"will overplot first plot"); */
					mp_layout.act_row = 0;
				}
			}
		}
		multiplot_reset();
	}
}

void multiplot_previous()
{
	mp_layout.current_panel--;
	if(mp_layout.auto_layout) {
		if(mp_layout.row_major) {
			mp_layout.act_row--;
			if(mp_layout.act_row < 0) {
				mp_layout.act_row = mp_layout.num_rows-1;
				mp_layout.act_col--;
				if(mp_layout.act_col < 0) {
					/* GPO.IntWarn(NO_CARET,"will overplot first plot"); */
					mp_layout.act_col = mp_layout.num_cols-1;
				}
			}
		}
		else { /* column-major */
			mp_layout.act_col--;
			if(mp_layout.act_col < 0) {
				mp_layout.act_col = mp_layout.num_cols-1;
				mp_layout.act_row--;
				if(mp_layout.act_row < 0) {
					/* GPO.IntWarn(NO_CARET,"will overplot first plot"); */
					mp_layout.act_row = mp_layout.num_rows-1;
				}
			}
		}
		multiplot_reset();
	}
}

int multiplot_current_panel()
{
	return mp_layout.current_panel;
}

//void multiplot_start()
void GnuPlot::MultiplotStart()
{
	bool set_spacing = FALSE;
	bool set_margins = FALSE;
	Pgm.Shift();
	// Only a few options are possible if we are already in multiplot mode 
	// So far we have "next".  Maybe also "previous", "clear"? 
	if(multiplot) {
		if(Pgm.EqualsCur("next")) {
			Pgm.Shift();
			if(!mp_layout.auto_layout)
				IntErrorCurToken("only valid inside an auto-layout multiplot");
			multiplot_next();
			return;
		}
		else if(Pgm.AlmostEqualsCur("prev$ious")) {
			Pgm.Shift();
			if(!mp_layout.auto_layout)
				IntErrorCurToken("only valid inside an auto-layout multiplot");
			multiplot_previous();
			return;
		}
		else {
			term_end_multiplot();
		}
	}
	// FIXME: more options should be reset/initialized each time 
	mp_layout.auto_layout = FALSE;
	mp_layout.auto_layout_margins = FALSE;
	mp_layout.current_panel = 0;
	mp_layout.title.noenhanced = FALSE;
	SAlloc::F(mp_layout.title.text);
	mp_layout.title.text = NULL;
	SAlloc::F(mp_layout.title.font);
	mp_layout.title.font = NULL;
	mp_layout.title.boxed = 0;
	// Parse options 
	while(!Pgm.EndOfCommand()) {
		if(Pgm.AlmostEqualsCur("ti$tle")) {
			Pgm.Shift();
			ParseLabelOptions(&mp_layout.title, 2);
			if(!Pgm.EndOfCommand())
				mp_layout.title.text = TryToGetString();
			ParseLabelOptions(&mp_layout.title, 2);
			continue;
		}
		if(Pgm.AlmostEqualsCur("lay$out")) {
			if(mp_layout.auto_layout)
				IntErrorCurToken("too many layout commands");
			else
				mp_layout.auto_layout = TRUE;
			Pgm.Shift();
			if(Pgm.EndOfCommand())
				IntErrorCurToken("expecting '<num_cols>,<num_rows>'");
			// read row,col 
			mp_layout.num_rows = IntExpression();
			if(Pgm.EndOfCommand() || !Pgm.EqualsCur(",") )
				IntErrorCurToken("expecting ', <num_cols>'");
			Pgm.Shift();
			if(Pgm.EndOfCommand())
				IntErrorCurToken("expecting <num_cols>");
			mp_layout.num_cols = IntExpression();
			// remember current values of the plot size and the margins 
			mp_layout.prev_xsize = V.XSize;
			mp_layout.prev_ysize = V.YSize;
			mp_layout.prev_xoffset = V.XOffset;
			mp_layout.prev_yoffset = V.YOffset;
			mp_layout.prev_lmargin = V.MarginL;
			mp_layout.prev_rmargin = V.MarginR;
			mp_layout.prev_bmargin = V.MarginB;
			mp_layout.prev_tmargin = V.MarginT;
			mp_layout.act_row = 0;
			mp_layout.act_col = 0;
			continue;
		}
		// The remaining options are only valid for auto-layout mode 
		if(!mp_layout.auto_layout)
			IntErrorCurToken("only valid in the context of an auto-layout command");
		switch(Pgm.LookupTableForCurrentToken(&set_multiplot_tbl[0])) {
			case S_MULTIPLOT_COLUMNSFIRST:
			    mp_layout.row_major = TRUE;
			    Pgm.Shift();
			    break;
			case S_MULTIPLOT_ROWSFIRST:
			    mp_layout.row_major = FALSE;
			    Pgm.Shift();
			    break;
			case S_MULTIPLOT_DOWNWARDS:
			    mp_layout.downwards = TRUE;
			    Pgm.Shift();
			    break;
			case S_MULTIPLOT_UPWARDS:
			    mp_layout.downwards = FALSE;
			    Pgm.Shift();
			    break;
			case S_MULTIPLOT_SCALE:
			    Pgm.Shift();
			    mp_layout.xscale = RealExpression();
			    mp_layout.yscale = mp_layout.xscale;
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",") ) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand()) {
					    IntErrorCurToken("expecting <yscale>");
				    }
				    mp_layout.yscale = RealExpression();
			    }
			    break;
			case S_MULTIPLOT_OFFSET:
			    Pgm.Shift();
			    mp_layout.xoffset = RealExpression();
			    mp_layout.yoffset = mp_layout.xoffset;
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",") ) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand()) {
					    IntErrorCurToken("expecting <yoffset>");
				    }
				    mp_layout.yoffset = RealExpression();
			    }
			    break;
			case S_MULTIPLOT_MARGINS:
			    Pgm.Shift();
			    if(Pgm.EndOfCommand())
				    IntErrorCurToken("expecting '<left>,<right>,<bottom>,<top>'");
			    mp_layout.lmargin.scalex = screen;
			    mp_layout_set_margin_or_spacing(&(mp_layout.lmargin));
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",") ) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand())
					    IntErrorCurToken("expecting <right>");
				    mp_layout.rmargin.scalex = mp_layout.lmargin.scalex;
				    mp_layout_set_margin_or_spacing(&(mp_layout.rmargin));
			    }
			    else {
				    IntErrorCurToken("expecting <right>");
			    }
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",") ) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand())
					    IntErrorCurToken("expecting <top>");

				    mp_layout.bmargin.scalex = mp_layout.rmargin.scalex;
				    mp_layout_set_margin_or_spacing(&(mp_layout.bmargin));
			    }
			    else {
				    IntErrorCurToken("expecting <bottom>");
			    }
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",") ) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand())
					    IntErrorCurToken("expecting <bottom>");
				    mp_layout.tmargin.scalex = mp_layout.bmargin.scalex;
				    mp_layout_set_margin_or_spacing(&(mp_layout.tmargin));
			    }
			    else {
				    IntErrorCurToken("expecting <top>");
			    }
			    set_margins = TRUE;
			    break;
			case S_MULTIPLOT_SPACING:
			    Pgm.Shift();
			    if(Pgm.EndOfCommand())
				    IntErrorCurToken("expecting '<xspacing>,<yspacing>'");
			    mp_layout.xspacing.scalex = screen;
			    mp_layout_set_margin_or_spacing(&(mp_layout.xspacing));
			    mp_layout.yspacing = mp_layout.xspacing;
			    if(!Pgm.EndOfCommand() && Pgm.EqualsCur(",")) {
				    Pgm.Shift();
				    if(Pgm.EndOfCommand())
					    IntErrorCurToken("expecting <yspacing>");
				    mp_layout_set_margin_or_spacing(&(mp_layout.yspacing));
			    }
			    set_spacing = TRUE;
			    break;
			default:
			    IntErrorCurToken("invalid or duplicate option");
			    break;
		}
	}
	if(set_spacing || set_margins) {
		if(set_spacing && set_margins) {
			if(mp_layout.lmargin.x >= 0 && mp_layout.rmargin.x >= 0 && mp_layout.tmargin.x >= 0 && mp_layout.bmargin.x >= 0 && mp_layout.xspacing.x >= 0 && mp_layout.yspacing.x >= 0)
				mp_layout.auto_layout_margins = TRUE;
			else
				IntError(NO_CARET, "must give positive margin and spacing values");
		}
		else if(set_margins) {
			mp_layout.auto_layout_margins = TRUE;
			mp_layout.xspacing.scalex = screen;
			mp_layout.xspacing.x = 0.05;
			mp_layout.yspacing.scalex = screen;
			mp_layout.yspacing.x = 0.05;
		}
		// Sanity check that screen tmargin is > screen bmargin 
		if(mp_layout.bmargin.scalex == screen && mp_layout.tmargin.scalex == screen)
			if(mp_layout.bmargin.x > mp_layout.tmargin.x) {
				double tmp = mp_layout.bmargin.x;
				mp_layout.bmargin.x = mp_layout.tmargin.x;
				mp_layout.tmargin.x = tmp;
			}
	}
	// If we reach here, then the command has been successfully parsed.
	// Aug 2013: call term_start_plot() before setting multiplot so that
	// the wxt and qt terminals will reset the plot count to 0 before
	// ignoring subsequent TERM_LAYER_RESET requests.
	term_start_plot();
	multiplot = TRUE;
	multiplot_count = 0;
	Ev.FillGpValInteger("GPVAL_MULTIPLOT", 1);
	// Place overall title before doing anything else 
	if(mp_layout.title.text) {
		char * p = mp_layout.title.text;
		uint x = term->xmax  / 2;
		uint y = term->ymax - term->ChrV;
		write_label(term, x, y, &(mp_layout.title));
		reset_textcolor(&(mp_layout.title.textcolor));
		// Calculate fractional height of title compared to entire page 
		// If it would fill the whole page, forget it! 
		for(y = 1; *p; p++)
			if(*p == '\n')
				y++;
		// Oct 2012 - ChrV depends on the font used 
		if(mp_layout.title.font && *mp_layout.title.font)
			term->set_font(mp_layout.title.font);
		mp_layout.title_height = (double)(y * term->ChrV) / (double)term->ymax;
		if(mp_layout.title.font && *mp_layout.title.font)
			term->set_font("");
		if(mp_layout.title_height > 0.9)
			mp_layout.title_height = 0.05;
	}
	else
		mp_layout.title_height = 0.0;
	multiplot_reset();
}

//void multiplot_end()
void GnuPlot::MultiplotEnd()
{
	multiplot = FALSE;
	multiplot_count = 0;
	GPO.Ev.FillGpValInteger("GPVAL_MULTIPLOT", 0);
	// reset plot size, origin and margins to values before 'set multiplot layout' 
	if(mp_layout.auto_layout) {
		V.XSize = mp_layout.prev_xsize;
		V.YSize = mp_layout.prev_ysize;
		V.XOffset = mp_layout.prev_xoffset;
		V.YOffset = mp_layout.prev_yoffset;
		V.MarginL = mp_layout.prev_lmargin;
		V.MarginR = mp_layout.prev_rmargin;
		V.MarginB = mp_layout.prev_bmargin;
		V.MarginT = mp_layout.prev_tmargin;
	}
	// reset automatic multiplot layout 
	mp_layout.auto_layout = FALSE;
	mp_layout.auto_layout_margins = FALSE;
	mp_layout.xscale = mp_layout.yscale = 1.0;
	mp_layout.xoffset = mp_layout.yoffset = 0.0;
	mp_layout.lmargin.scalex = mp_layout.rmargin.scalex = screen;
	mp_layout.bmargin.scalex = mp_layout.tmargin.scalex = screen;
	mp_layout.lmargin.x = mp_layout.rmargin.x = mp_layout.bmargin.x = mp_layout.tmargin.x = -1;
	mp_layout.xspacing.scalex = mp_layout.yspacing.scalex = screen;
	mp_layout.xspacing.x = mp_layout.yspacing.x = -1;
	ZFREE(mp_layout.title.text);
}

/* Helper function for multiplot auto layout to issue size and offset cmds */
void multiplot_reset()
{
	if(mp_layout.auto_layout_margins)
		mp_layout_margins_and_spacing();
	else
		mp_layout_size_and_offset();
}

static void mp_layout_size_and_offset()
{
	if(!mp_layout.auto_layout) 
		return;
	// fprintf(stderr,"col==%d row==%d\n",mp_layout.act_col,mp_layout.act_row); 
	// the 'set size' command 
	GPO.V.XSize = mp_layout.xscale / mp_layout.num_cols;
	GPO.V.YSize = mp_layout.yscale / mp_layout.num_rows;
	// the 'set origin' command 
	GPO.V.XOffset = (double)(mp_layout.act_col) / mp_layout.num_cols;
	if(mp_layout.downwards)
		GPO.V.YOffset = 1.0 - (double)(mp_layout.act_row+1) / mp_layout.num_rows;
	else
		GPO.V.YOffset = (double)(mp_layout.act_row) / mp_layout.num_rows;
	// fprintf(stderr,"xoffset==%g  yoffset==%g\n", xoffset,yoffset); 
	// Allow a little space at the top for a title 
	if(mp_layout.title.text) {
		GPO.V.YSize *= (1.0 - mp_layout.title_height);
		GPO.V.YOffset *= (1.0 - mp_layout.title_height);
	}
	// corrected for x/y-scaling factors and user defined offsets 
	GPO.V.XOffset -= (mp_layout.xscale-1)/(2*mp_layout.num_cols);
	GPO.V.YOffset -= (mp_layout.yscale-1)/(2*mp_layout.num_rows);
	// fprintf(stderr,"  xoffset==%g  yoffset==%g\n", xoffset,yoffset); 
	GPO.V.XOffset += mp_layout.xoffset;
	GPO.V.YOffset += mp_layout.yoffset;
	// fprintf(stderr,"  xoffset==%g  yoffset==%g\n", xoffset,yoffset); 
}

/* Helper function for multiplot auto layout to set the explicit plot margins,
   if requested with 'margins' and 'spacing' options. */
static void mp_layout_margins_and_spacing()
{
	/* width and height of a single sub plot. */
	double tmp_width, tmp_height;
	double leftmargin, rightmargin, topmargin, bottommargin, xspacing, yspacing;
	if(!mp_layout.auto_layout_margins) 
		return;
	if(mp_layout.lmargin.scalex == screen)
		leftmargin = mp_layout.lmargin.x;
	else
		leftmargin = (mp_layout.lmargin.x * term->ChrH) / term->xmax;
	if(mp_layout.rmargin.scalex == screen)
		rightmargin = mp_layout.rmargin.x;
	else
		rightmargin = 1 - (mp_layout.rmargin.x * term->ChrH) / term->xmax;
	if(mp_layout.tmargin.scalex == screen)
		topmargin = mp_layout.tmargin.x;
	else
		topmargin = 1 - (mp_layout.tmargin.x * term->ChrV) / term->ymax;
	if(mp_layout.bmargin.scalex == screen)
		bottommargin = mp_layout.bmargin.x;
	else
		bottommargin = (mp_layout.bmargin.x * term->ChrV) / term->ymax;
	if(mp_layout.xspacing.scalex == screen)
		xspacing = mp_layout.xspacing.x;
	else
		xspacing = (mp_layout.xspacing.x * term->ChrH) / term->xmax;
	if(mp_layout.yspacing.scalex == screen)
		yspacing = mp_layout.yspacing.x;
	else
		yspacing = (mp_layout.yspacing.x * term->ChrV) / term->ymax;
	tmp_width = (rightmargin - leftmargin - (mp_layout.num_cols - 1) * xspacing) / mp_layout.num_cols;
	tmp_height = (topmargin - bottommargin - (mp_layout.num_rows - 1) * yspacing) / mp_layout.num_rows;
	GPO.V.MarginL.x = leftmargin + mp_layout.act_col * (tmp_width + xspacing);
	GPO.V.MarginL.scalex = screen;
	GPO.V.MarginR.x = GPO.V.MarginL.x + tmp_width;
	GPO.V.MarginR.scalex = screen;
	if(mp_layout.downwards) {
		GPO.V.MarginB.x = bottommargin + (mp_layout.num_rows - mp_layout.act_row - 1) * (tmp_height + yspacing);
	}
	else {
		GPO.V.MarginB.x = bottommargin + mp_layout.act_row * (tmp_height + yspacing);
	}
	GPO.V.MarginB.scalex = screen;
	GPO.V.MarginT.x = GPO.V.MarginB.x + tmp_height;
	GPO.V.MarginT.scalex = screen;
}

static void mp_layout_set_margin_or_spacing(GpPosition * margin)
{
	margin->x = -1;
	if(GPO.Pgm.EndOfCommand())
		return;
	if(GPO.Pgm.AlmostEqualsCur("sc$reen")) {
		margin->scalex = screen;
		GPO.Pgm.Shift();
	}
	else if(GPO.Pgm.AlmostEqualsCur("char$acter")) {
		margin->scalex = character;
		GPO.Pgm.Shift();
	}
	margin->x = GPO.RealExpression();
	if(margin->x < 0)
		margin->x = -1;
	if(margin->scalex == screen) {
		if(margin->x < 0)
			margin->x = 0;
		if(margin->x > 1)
			margin->x = 1;
	}
}