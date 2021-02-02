// GNUPLOT - term.c 
// Copyright 1986 - 1993, 1998, 2004   Thomas Williams, Colin Kelley
//
/* This module is responsible for looking after the terminal
 * drivers at the lowest level. Only this module (should)
 * know about all the various rules about interpreting
 * the terminal capabilities exported by the terminal
 * drivers in the table.
 *
 * Note that, as far as this module is concerned, a
 * terminal session lasts only until _either_ terminal
 * or output file changes. Before either is changed,
 * the terminal is shut down.
 *
 * Entry points : (see also term/README)
 *
 * term_set_output() : called when  set output  invoked
 *
 * term_initialise()  : optional. Prepare the terminal for first
 *                use. It protects itself against subsequent calls.
 *
 * term_start_plot() : called at start of graph output. Calls term_init
 *                     if necessary
 *
 * term_apply_lp_properties() : apply linewidth settings
 *
 * term_end_plot() : called at the end of a plot
 *
 * term_reset() : called during int_error handling, to shut
 *                terminal down cleanly
 *
 * term_start_multiplot() : called by   set multiplot
 *
 * term_end_multiplot() : called by  set nomultiplot
 *
 * term_check_multiplot_okay() : called just before an interactive
 *                        prompt is issued while in multiplot mode,
 *                        to allow terminal to suspend if necessary,
 *                        Raises an error if interactive multiplot
 *                       is not supported.
 */
#include <gnuplot.h>
#pragma hdrstop
#include "driver.h"
#include "term.h"
#ifndef USE_MOUSE
	/* Some terminals (svg canvas) can provide mousing information */
	/* even if the interactive gnuplot session itself cannot.      */
	long mouse_mode = 0;
	char* mouse_alt_string = NULL;
	#define MOUSE_COORDINATES_FUNCTION 8    /* Normally an enum in mouse.h */
#endif
#ifdef _WIN32
	#include "win/winmain.h"
	#include "win/wcommon.h"
#endif

static int termcomp(const generic * a, const generic * b);

/* Externally visible variables */
/* the central instance: the current terminal's interface structure */
struct termentry * term = NULL;  /* unknown */
char term_options[MAX_LINE_LEN+1] = ""; /* ... and its options string */

/* the 'output' file name and handle */
char * outstr = NULL;            /* means "STDOUT" */
FILE * gpoutfile;
/* Output file where the PostScript output goes to. See term_api.h for more details. */
FILE * gppsfile = 0;
char * PS_psdir = NULL;
char * PS_fontpath = NULL;
bool term_initialised; /* true if terminal has been initialized */
/* The qt and wxt terminals cannot be used in the same session. */
/* Whichever one is used first to plot, this locks out the other. */
void * term_interlock = NULL;
bool monochrome = FALSE; /* true if "set monochrome" */
bool multiplot = FALSE; /* true if in multiplot mode */
int multiplot_count = 0;
enum set_encoding_id encoding; /* text output encoding, for terminals that support it */
/* table of encoding names, for output of the setting */
const char * encoding_names[] = {
	"default", "iso_8859_1", "iso_8859_2", "iso_8859_9", "iso_8859_15",
	"cp437", "cp850", "cp852", "cp950", "cp1250", "cp1251", "cp1252", "cp1254",
	"koi8r", "koi8u", "sjis", "utf8", NULL
};
/* 'set encoding' options */
const struct gen_table set_encoding_tbl[] =
{
	{ "def$ault", S_ENC_DEFAULT },
	{ "utf$8", S_ENC_UTF8 },
	{ "iso$_8859_1", S_ENC_ISO8859_1 },
	{ "iso_8859_2", S_ENC_ISO8859_2 },
	{ "iso_8859_9", S_ENC_ISO8859_9 },
	{ "iso_8859_15", S_ENC_ISO8859_15 },
	{ "cp4$37", S_ENC_CP437 },
	{ "cp850", S_ENC_CP850 },
	{ "cp852", S_ENC_CP852 },
	{ "cp950", S_ENC_CP950 },
	{ "cp1250", S_ENC_CP1250 },
	{ "cp1251", S_ENC_CP1251 },
	{ "cp1252", S_ENC_CP1252 },
	{ "cp1254", S_ENC_CP1254 },
	{ "koi8$r", S_ENC_KOI8_R },
	{ "koi8$u", S_ENC_KOI8_U },
	{ "sj$is", S_ENC_SJIS },
	{ NULL, S_ENC_INVALID }
};

const char * arrow_head_names[4] = {"nohead", "head", "backhead", "heads"};

enum { IPC_BACK_UNUSABLE = -2, IPC_BACK_CLOSED = -1 };

int gp_resolution = 72; /* resolution in dpi for converting pixels to size units */

/* Support for enhanced text mode. Declared extern in term_api.h */
char enhanced_text[MAX_LINE_LEN+1] = "";
char * enhanced_cur_text = NULL;
double enhanced_fontscale = 1.0;
char enhanced_escape_format[16] = "";
double enhanced_max_height = 0.0, enhanced_min_height = 0.0;
#define ENHANCED_TEXT_MAX (&enhanced_text[MAX_LINE_LEN])
bool ignore_enhanced_text = FALSE; /* flag variable to disable enhanced output of filenames, mainly. */
/* Recycle count for user-defined linetypes */
int linetype_recycle_count = 0;
int mono_recycle_count = 0;

/* Internal variables */
static bool term_graphics = FALSE; /* true if terminal is in graphics mode */
static bool term_suspended = FALSE; /* we have suspended the driver, in multiplot mode */
static bool opened_binary = FALSE; /* true if? */
static bool term_force_init = FALSE; /* true if require terminal to be initialized */
static double term_pointsize = 1; /* internal pointsize for do_point */

/* Internal prototypes: */
static void term_suspend();
static void term_close_output();
static void null_linewidth(double);
static void do_point(uint x, uint y, int number);
static void do_pointsize(double size);
static void line_and_point(uint x, uint y, int number);
static void do_arrow(uint sx, uint sy, uint ex, uint ey, int headstyle);
static void null_dashtype(int type, t_dashtype * custom_dash_pattern);
static int  null_text_angle(int ang);
static int  null_justify_text(enum JUSTIFY just);
static int  null_scale(double x, double y);
static void null_layer(t_termlayer layer);
static int  null_set_font(const char * font);
static void null_set_color(struct t_colorspec * colorspec);
static void options_null();
static void graphics_null();
static void UNKNOWN_null();
static void MOVE_null(uint, uint);
static void LINETYPE_null(int);
static void PUTTEXT_null(uint, uint, const char *);
static int strlen_tex(const char *);
static char * stylefont(const char * fontname, bool isbold, bool isitalic);
//static GpSizeUnits parse_term_size(float * xsize, float * ysize, GpSizeUnits def_units);

#ifdef VMS
	#include "vms.h"
#else
	#define FOPEN_BINARY(file) fopen(file, "wb")
	#define fflush_binary()
#endif /* !VMS */
#if defined(MSDOS) || defined(_WIN32)
	#if defined(__DJGPP__)
	#include <io.h>
	#endif
	#include <fcntl.h>
	#ifndef O_BINARY
	#ifdef _O_BINARY
	#define O_BINARY _O_BINARY
	#else
	#define O_BINARY O_BINARY_is_not_defined
	#endif
	#endif
#endif
#ifdef __EMX__
	#include <io.h>
	#include <fcntl.h>
#endif
#if defined(__WATCOMC__) || defined(__MSC__)
	#include <io.h> /* for setmode() */
#endif
#define NICE_LINE               0
#define POINT_TYPES             6
#ifndef DEFAULTTERM
	#define DEFAULTTERM NULL
#endif

/* interface to the rest of gnuplot - the rules are getting
 * too complex for the rest of gnuplot to be allowed in
 */

#if defined(PIPES)
static bool output_pipe_open = FALSE;
#endif /* PIPES */

static void term_close_output()
{
	FPRINTF((stderr, "term_close_output\n"));
	opened_binary = FALSE;
	if(!outstr)             /* ie using stdout */
		return;
#if defined(PIPES)
	if(output_pipe_open) {
		pclose(gpoutfile);
		output_pipe_open = FALSE;
	}
	else
#endif /* PIPES */
#ifdef _WIN32
	if(sstreqi_ascii(outstr, "PRN"))
		close_printer(gpoutfile);
	else
#endif
	if(gpoutfile != gppsfile)
		fclose(gpoutfile);
	gpoutfile = stdout;     /* Don't dup... */
	ZFREE(outstr);
	SFile::ZClose(&gppsfile);
}

/* assigns dest to outstr, so it must be allocated or NULL
 * and it must not be outstr itself !
 */
void term_set_output(char * dest)
{
	FILE * f = NULL;
	FPRINTF((stderr, "term_set_output\n"));
	assert(dest == NULL || dest != outstr);
	if(multiplot) {
		fputs("In multiplot mode you can't change the output\n", stderr);
		return;
	}
	if(term && term_initialised) {
		(*term->reset)();
		term_initialised = FALSE;
		// switch off output to special postscript file (if used) 
		gppsfile = NULL;
	}
	if(dest == NULL) {      /* stdout */
		term_close_output();
	}
	else {
#if defined(PIPES)
		if(*dest == '|') {
			restrict_popen();
#if defined(_WIN32 ) || defined(MSDOS)
			if(term && (term->flags & TERM_BINARY))
				f = popen(dest + 1, "wb");
			else
				f = popen(dest + 1, "w");
#else
			f = popen(dest + 1, "w");
#endif
			if(f == (FILE*)NULL)
				os_error(GPO.Pgm.GetCurTokenIdx(), "cannot create pipe; output not changed");
			else
				output_pipe_open = TRUE;
		}
		else {
#endif /* PIPES */

#ifdef _WIN32
		if(outstr && sstreqi_ascii(outstr, "PRN")) {
			/* we can't call open_printer() while printer is open, so */
			close_printer(gpoutfile); /* close printer immediately if open */
			gpoutfile = stdout; /* and reset output to stdout */
			SAlloc::F(outstr);
			outstr = NULL;
		}
		if(sstreqi_ascii(dest, "PRN")) {
			if((f = open_printer()) == (FILE*)NULL)
				os_error(GPO.Pgm.GetCurTokenIdx(), "cannot open printer temporary file; output may have changed");
		}
		else
#endif
		{
			if(term && (term->flags & TERM_BINARY))
				f = FOPEN_BINARY(dest);
			else
				f = fopen(dest, "w");
			if(!f)
				os_error(GPO.Pgm.GetCurTokenIdx(), "cannot open file; output not changed");
		}
#if defined(PIPES)
	}
#endif
		term_close_output();
		gpoutfile = f;
		outstr = dest;
		opened_binary = (term && (term->flags & TERM_BINARY));
	}
}

void term_initialise()
{
	FPRINTF((stderr, "term_initialise()\n"));
	if(!term)
		GPO.IntError(NO_CARET, "No terminal defined");
	// check if we have opened the output file in the wrong mode
	// (text/binary), if set term comes after set output
	// This was originally done in change_term, but that
	// resulted in output files being truncated
	if(outstr && (term->flags & TERM_NO_OUTPUTFILE)) {
		if(interactive)
			fprintf(stderr, "Closing %s\n", outstr);
		term_close_output();
	}
	if(outstr && (((term->flags & TERM_BINARY) && !opened_binary) || ((!(term->flags & TERM_BINARY) && opened_binary)))) {
		// this is nasty - we cannot just term_set_output(outstr)
		// since term_set_output will first free outstr and we
		// end up with an invalid pointer. I think I would
		// prefer to defer opening output file until first plot.
		char * temp = (char *)gp_alloc(strlen(outstr) + 1, "temp file string");
		if(temp) {
			FPRINTF((stderr, "term_initialise: reopening \"%s\" as %s\n", outstr, term->flags & TERM_BINARY ? "binary" : "text"));
			strcpy(temp, outstr);
			term_set_output(temp); /* will free outstr */
			if(temp != outstr) {
				SAlloc::F(temp);
				temp = outstr;
			}
		}
		else
			fputs("Cannot reopen output file in binary", stderr);
		// and carry on, hoping for the best ! 
	}
#if defined(_WIN32)
#ifdef _WIN32
	else if(!outstr && (term->flags & TERM_BINARY))
#else
	else if(!outstr && !interactive && (term->flags & TERM_BINARY))
#endif
	{
#if defined(_WIN32) && !defined(WGP_CONSOLE)
#ifdef PIPES
		if(!output_pipe_open)
#endif
		if(!outstr && !(term->flags & TERM_NO_OUTPUTFILE))
			GPO.IntErrorCurToken("cannot output binary data to wgnuplot text window");
#endif
		// binary to stdout in non-interactive session... 
		fflush(stdout);
		_setmode(_fileno(stdout), O_BINARY);
	}
#endif
	if(!term_initialised || term_force_init) {
		FPRINTF((stderr, "- calling term->init()\n"));
		(*term->init)();
		term_initialised = TRUE;
#ifdef HAVE_LOCALE_H
		// This is here only from an abundance of caution (a.k.a. paranoia).
		// Some terminals (wxt qt caca) are known to change the locale when
		// initialized.  Others have been implicated (gd).  Rather than trying
		// to catch all such offenders one by one, cover for all of them here.
		setlocale(LC_NUMERIC, "C");
#endif
	}
}

void term_start_plot()
{
	FPRINTF((stderr, "term_start_plot()\n"));
	if(!term_initialised)
		term_initialise();
	if(!term_graphics) {
		FPRINTF((stderr, "- calling term->graphics()\n"));
		(*term->graphics)();
		term_graphics = true;
	}
	else if(multiplot && term_suspended) {
		if(term->resume) {
			FPRINTF((stderr, "- calling term->resume()\n"));
			(*term->resume)();
		}
		term_suspended = false;
	}
	if(multiplot)
		multiplot_count++;
	(*term->layer)(TERM_LAYER_RESET); // Sync point for epslatex text positioning 
	// Because PostScript plots may be viewed out of order, make sure 
	// Each new plot makes no assumption about the previous palette.
	if(term->flags & TERM_IS_POSTSCRIPT)
		invalidate_palette();
	// Set canvas size to full range of current terminal coordinates 
	GPO.V.BbCanvas.xleft  = 0;
	GPO.V.BbCanvas.xright = term->xmax - 1;
	GPO.V.BbCanvas.ybot   = 0;
	GPO.V.BbCanvas.ytop   = term->ymax - 1;
}

void term_end_plot()
{
	FPRINTF((stderr, "term_end_plot()\n"));
	if(!term_initialised)
		return;
	// Sync point for epslatex text positioning 
	(*term->layer)(TERM_LAYER_END_TEXT);
	if(!multiplot) {
		FPRINTF((stderr, "- calling term->text()\n"));
		(*term->text)();
		term_graphics = FALSE;
	}
	else {
		multiplot_next();
	}
#ifdef VMS
	if(opened_binary)
		fflush_binary();
	else
#endif /* VMS */
	fflush(gpoutfile);
#ifdef USE_MOUSE
	if(term->set_ruler) {
		recalc_statusline();
		update_ruler();
	}
#endif
}

static void term_suspend()
{
	FPRINTF((stderr, "term_suspend()\n"));
	if(term_initialised && !term_suspended && term->suspend) {
		FPRINTF((stderr, "- calling term->suspend()\n"));
		(*term->suspend)();
		term_suspended = TRUE;
	}
}

void term_reset()
{
	FPRINTF((stderr, "term_reset()\n"));
#ifdef USE_MOUSE
	// Make sure that ^C will break out of a wait for 'pause mouse' 
	paused_for_mouse = 0;
#ifdef _WIN32
	kill_pending_Pause_dialog();
#endif
#endif
	if(term_initialised) {
		if(term_suspended) {
			if(term->resume) {
				FPRINTF((stderr, "- calling term->resume()\n"));
				(*term->resume)();
			}
			term_suspended = FALSE;
		}
		if(term_graphics) {
			(*term->text)();
			term_graphics = FALSE;
		}
		if(term_initialised) {
			(*term->reset)();
			term_initialised = FALSE;
			// switch off output to special postscript file (if used) 
			gppsfile = NULL;
		}
	}
}

//void term_apply_lp_properties(termentry * pTerm, const lp_style_type * lp)
void GnuPlot::TermApplyLpProperties(termentry * pTerm, const lp_style_type * lp)
{
	// This function passes all the line and point properties to the
	// terminal driver and issues the corresponding commands.
	// 
	// Alas, sometimes it might be necessary to give some help to
	// this function by explicitly issuing additional '(*pTerm)(...)' commands.
	// 
	int lt = lp->l_type;
	int dt = lp->d_type;
	t_dashtype custom_dash_pattern = lp->custom_dash_pattern;
	t_colorspec colorspec = lp->pm3d_color;
	if((lp->flags & LP_SHOW_POINTS)) {
		// change points, too
		// Currently, there is no 'pointtype' function.  For points
		// there is a special function also dealing with (x,y) co-ordinates.
		if(lp->p_size < 0)
			(pTerm->pointsize)(pointsize);
		else
			(pTerm->pointsize)(lp->p_size);
	}
	//  _first_ set the line width, _then_ set the line type !
	// The linetype might depend on the linewidth in some terminals.
	(pTerm->linewidth)(lp->l_width);
	// LT_DEFAULT (used only by "set errorbars"?) means don't change it 
	if(lt == LT_DEFAULT)
		;
	else
	// The paradigm for handling linetype and dashtype in version 5 is 
	// linetype < 0 (e.g. LT_BACKGROUND, LT_NODRAW) means some special 
	// category that will be handled directly by term->linetype().     
	// linetype > 0 is now redundant. It used to encode both a color   
	// and a dash pattern.  Now we have separate mechanisms for those. 
	if(LT_COLORFROMCOLUMN < lt && lt < 0)
		(pTerm->linetype)(lt);
	else if(pTerm->set_color == null_set_color) {
		(pTerm->linetype)(lt-1);
		return;
	}
	else // All normal lines will be solid unless a dashtype is given 
		(pTerm->linetype)(LT_SOLID);
	// Version 5.3
	// If the line is not wanted at all, setting dashtype and color can only hurt
	if(lt == LT_NODRAW)
		return;
	// Apply dashtype or user-specified dash pattern, which may override
	// the terminal-specific dot/dash pattern belonging to this linetype. 
	if(lt == LT_AXIS)
		; // LT_AXIS is a special linetype that may incorporate a dash pattern 
	else if(dt == DASHTYPE_CUSTOM)
		(pTerm->dashtype)(dt, &custom_dash_pattern);
	else if(dt == DASHTYPE_SOLID)
		(pTerm->dashtype)(dt, NULL);
	else if(dt >= 0) {
		// The null_dashtype() routine or a version 5 terminal's private
		// dashtype routine converts this into a call to term->linetype()
		// yielding the same result as in version 4 except possibly for a 
		// different line width.
		(pTerm->dashtype)(dt, NULL);
	}
	ApplyPm3DColor(pTerm, &colorspec); // Finally adjust the color of the line 
}

void term_start_multiplot()
{
	FPRINTF((stderr, "term_start_multiplot()\n"));
	GPO.MultiplotStart();
#ifdef USE_MOUSE
	UpdateStatusline();
#endif
}

void term_end_multiplot()
{
	FPRINTF((stderr, "term_end_multiplot()\n"));
	if(multiplot) {
		if(term_suspended) {
			if(term->resume)
				(*term->resume)();
			term_suspended = FALSE;
		}
		GPO.MultiplotEnd();
		term_end_plot();
#ifdef USE_MOUSE
		UpdateStatusline();
#endif
	}
}

void term_check_multiplot_okay(bool f_interactive)
{
	FPRINTF((stderr, "term_multiplot_okay(%d)\n", f_interactive));
	if(!term_initialised)
		return; // they've not started yet 
	// make sure that it is safe to issue an interactive prompt
	// it is safe if
	//   it is not an interactive read, or
	//   the terminal supports interactive multiplot, or
	//   we are not writing to stdout and terminal doesn't
	//   refuse multiplot outright
	if(!f_interactive || (term->flags & TERM_CAN_MULTIPLOT) || ((gpoutfile != stdout) && !(term->flags & TERM_CANNOT_MULTIPLOT))) {
		// it's okay to use multiplot here, but suspend first 
		term_suspend();
		return;
	}
	// uh oh: they're not allowed to be in multiplot here 
	term_end_multiplot();
	// at this point we know that it is interactive and that the
	// terminal can either only do multiplot when writing to
	// to a file, or it does not do multiplot at all
	if(term->flags & TERM_CANNOT_MULTIPLOT)
		GPO.IntError(NO_CARET, "This terminal does not support multiplot");
	else
		GPO.IntError(NO_CARET, "Must set output to a file or put all multiplot commands on one input line");
}

void write_multiline(termentry * pTerm, int x, int y, char * text, JUSTIFY hor/* horizontal ... */,
    VERT_JUSTIFY vert/* ... and vertical just - text in hor direction despite angle */, int angle/* assume term has already been set for this */,
    const char * pFont/* NULL or "" means use default */)
{
	//struct termentry * t = term;
	char * p = text;
	if(p) {
		// EAM 9-Feb-2003 - Set font before calculating sizes 
		if(!isempty(pFont))
			(pTerm->set_font)(pFont);
		if(vert != JUST_TOP) {
			// count lines and adjust y 
			int lines = 0; // number of linefeeds - one fewer than lines 
			while(*p) {
				if(*p++ == '\n')
					++lines;
			}
			if(angle)
				x -= (vert * lines * pTerm->ChrV) / 2;
			else
				y += (vert * lines * pTerm->ChrV) / 2;
		}
		for(;;) { // we will explicitly break out 
			if(text && (p = strchr(text, '\n')) != NULL)
				*p = 0; // terminate the string 
			if((pTerm->justify_text)(hor)) {
				if(on_page(x, y))
					(pTerm->put_text)(x, y, text);
			}
			else {
				int len = estimate_strlen(text, NULL);
				int hfix, vfix;
				if(angle == 0) {
					hfix = hor * pTerm->ChrH * len / 2;
					vfix = 0;
				}
				else {
					// Attention: This relies on the numeric values of enum JUSTIFY! 
					hfix = static_cast<int>(hor * pTerm->ChrH * len * cos(angle * DEG2RAD) / 2 + 0.5);
					vfix = static_cast<int>(hor * pTerm->ChrV * len * sin(angle * DEG2RAD) / 2 + 0.5);
				}
				if(on_page(x - hfix, y - vfix))
					(pTerm->put_text)(x - hfix, y - vfix, text);
			}
			if(angle == 90 || angle == TEXT_VERTICAL)
				x += pTerm->ChrV;
			else if(angle == -90 || angle == -TEXT_VERTICAL)
				x -= pTerm->ChrV;
			else
				y -= pTerm->ChrV;
			if(!p)
				break;
			else {
				// put it back 
				*p = '\n';
			}
			text = p + 1;
		} // unconditional branch back to the for(;;) - just a goto ! 
		if(!isempty(pFont))
			(pTerm->set_font)("");
	}
}

static void do_point(uint x, uint y, int number)
{
	int htic, vtic;
	struct termentry * t = term;
	// use solid lines for point symbols 
	if(term->dashtype != null_dashtype)
		term->dashtype(DASHTYPE_SOLID, NULL);
	if(number < 0) {        /* do dot */
		(*t->move)(x, y);
		(*t->vector)(x, y);
		return;
	}
	else {
		number %= POINT_TYPES;
		// should be in term_tbl[] in later version 
		htic = (term_pointsize * t->TicH / 2);
		vtic = (term_pointsize * t->TicV / 2);
		// point types 1..4 are same as in postscript, png and x11
		// point types 5..6 are "similar"
		// (note that (number) equals (pointtype-1)
		switch(number) {
			case 4:         /* do diamond */
				(*t->move)(x - htic, y);
				(*t->vector)(x, y - vtic);
				(*t->vector)(x + htic, y);
				(*t->vector)(x, y + vtic);
				(*t->vector)(x - htic, y);
				(*t->move)(x, y);
				(*t->vector)(x, y);
				break;
			case 0:         /* do plus */
				(*t->move)(x - htic, y);
				(*t->vector)(x - htic, y);
				(*t->vector)(x + htic, y);
				(*t->move)(x, y - vtic);
				(*t->vector)(x, y - vtic);
				(*t->vector)(x, y + vtic);
				break;
			case 3:         /* do box */
				(*t->move)(x - htic, y - vtic);
				(*t->vector)(x - htic, y - vtic);
				(*t->vector)(x + htic, y - vtic);
				(*t->vector)(x + htic, y + vtic);
				(*t->vector)(x - htic, y + vtic);
				(*t->vector)(x - htic, y - vtic);
				(*t->move)(x, y);
				(*t->vector)(x, y);
				break;
			case 1:         /* do X */
				(*t->move)(x - htic, y - vtic);
				(*t->vector)(x - htic, y - vtic);
				(*t->vector)(x + htic, y + vtic);
				(*t->move)(x - htic, y + vtic);
				(*t->vector)(x - htic, y + vtic);
				(*t->vector)(x + htic, y - vtic);
				break;
			case 5:         /* do triangle */
				(*t->move)(x, y + (4 * vtic / 3));
				(*t->vector)(x - (4 * htic / 3), y - (2 * vtic / 3));
				(*t->vector)(x + (4 * htic / 3), y - (2 * vtic / 3));
				(*t->vector)(x, y + (4 * vtic / 3));
				(*t->move)(x, y);
				(*t->vector)(x, y);
				break;
			case 2:         /* do star */
				(*t->move)(x - htic, y);
				(*t->vector)(x - htic, y);
				(*t->vector)(x + htic, y);
				(*t->move)(x, y - vtic);
				(*t->vector)(x, y - vtic);
				(*t->vector)(x, y + vtic);
				(*t->move)(x - htic, y - vtic);
				(*t->vector)(x - htic, y - vtic);
				(*t->vector)(x + htic, y + vtic);
				(*t->move)(x - htic, y + vtic);
				(*t->vector)(x - htic, y + vtic);
				(*t->vector)(x + htic, y - vtic);
				break;
		}
	}
}

static void do_pointsize(double size)
{
	term_pointsize = (size >= 0 ? size : 1);
}

/*
 * general point routine
 */
static void line_and_point(uint x, uint y, int number)
{
	/* temporary(?) kludge to allow terminals with bad linetypes to make nice marks */
	(*term->linetype)(NICE_LINE);
	do_point(x, y, number);
}

/*
 * general arrow routine
 *
 * I set the angle between the arrowhead and the line 15 degree.
 * The length of arrowhead varies depending on the line length
 * within the the range [0.3*(the-tic-length), 2*(the-tic-length)].
 * No head is printed if the arrow length is zero.
 *
 *            Yasu-hiro Yamazaki(hiro@rainbow.physics.utoronto.ca)
 *            Jul 1, 1993
 */

#define COS15 (0.96593)         /* cos of 15 degree */
#define SIN15 (0.25882)         /* sin of 15 degree */

#define HEAD_LONG_LIMIT  (2.0)  /* long  limit of arrowhead length */
#define HEAD_SHORT_LIMIT (0.3)  /* short limit of arrowhead length */
                                /* their units are the "tic" length */

#define HEAD_COEFF  (0.3)       /* default value of head/line length ratio */

int curr_arrow_headlength; /* access head length + angle without changing API */
double curr_arrow_headangle;    /* angle in degrees */
double curr_arrow_headbackangle;  /* angle in degrees */
arrowheadfill curr_arrow_headfilled;      /* arrow head filled or not */
bool curr_arrow_headfixedsize;        /* Adapt the head size for short arrows or not */

static void do_arrow(uint usx, uint usy/* start point */, uint uex, uint uey/* end point (point of arrowhead) */, int headstyle)
{
	// Clipping and angle calculations do not work if coords are unsigned! 
	int sx = (int)usx;
	int sy = (int)usy;
	int ex = (int)uex;
	int ey = (int)uey;
	struct termentry * t = term;
	double len_tic = ((double)(t->TicH + t->TicV)) / 2.0;
	// average of tic sizes 
	// (dx,dy) : vector from end to start 
	double dx = sx - ex;
	double dy = sy - ey;
	double len_arrow = sqrt(dx * dx + dy * dy);
	gpiPoint head_points[5];
	int xm = 0, ym = 0;
	BoundingBox * clip_save;
	// The arrow shaft was clipped already in draw_clip_arrow() but we still 
	// need to clip the head here. 
	clip_save = GPO.V.P_ClipArea;
	GPO.V.P_ClipArea = (term->flags & TERM_CAN_CLIP) ? NULL : &GPO.V.BbCanvas;
	// Calculate and draw arrow heads.
	// Draw no head for arrows with length = 0, or, to be more specific,
	// length < DBL_EPSILON, because len_arrow will almost always be != 0.
	if((headstyle & BOTH_HEADS) != NOHEAD && fabs(len_arrow) >= DBL_EPSILON) {
		int x1, y1, x2, y2;
		if(curr_arrow_headlength <= 0) {
			// An arrow head with the default size and angles 
			double coeff_shortest = len_tic * HEAD_SHORT_LIMIT / len_arrow;
			double coeff_longest = len_tic * HEAD_LONG_LIMIT / len_arrow;
			double head_coeff = MAX(coeff_shortest, MIN(HEAD_COEFF, coeff_longest));
			// we put the arrowhead marks at 15 degrees to line 
			x1 = (int)((COS15 * dx - SIN15 * dy) * head_coeff);
			y1 = (int)((SIN15 * dx + COS15 * dy) * head_coeff);
			x2 = (int)((COS15 * dx + SIN15 * dy) * head_coeff);
			y2 = (int)((-SIN15 * dx + COS15 * dy) * head_coeff);
			// backangle defaults to 90 deg 
			xm = (int)((x1 + x2)/2);
			ym = (int)((y1 + y2)/2);
		}
		else {
			// An arrow head with the length + angle specified explicitly.	
			// Assume that if the arrow is shorter than the arrowhead, this is	
			// because of foreshortening in a 3D plot.                      
			double alpha = curr_arrow_headangle * DEG2RAD;
			double beta = curr_arrow_headbackangle * DEG2RAD;
			double phi = atan2(-dy, -dx); /* azimuthal angle of the vector */
			double backlen;
			double dx2, dy2;
			double effective_length = curr_arrow_headlength;
			if(!curr_arrow_headfixedsize && (curr_arrow_headlength > len_arrow/2.)) {
				effective_length = len_arrow/2.;
				alpha = atan(tan(alpha)*((double)curr_arrow_headlength/effective_length));
				beta = atan(tan(beta)*((double)curr_arrow_headlength/effective_length));
			}
			backlen = sin(alpha) / sin(beta);
			// anticlock-wise head segment 
			x1 = -(int)(effective_length * cos(alpha - phi));
			y1 =  (int)(effective_length * sin(alpha - phi));
			// clock-wise head segment 
			dx2 = -effective_length * cos(phi + alpha);
			dy2 = -effective_length * sin(phi + alpha);
			x2 = (int)(dx2);
			y2 = (int)(dy2);
			// back point 
			xm = (int)(dx2 + backlen*effective_length * cos(phi + beta));
			ym = (int)(dy2 + backlen*effective_length * sin(phi + beta));
		}
		if((headstyle & END_HEAD) && !GPO.V.ClipPoint(ex, ey)) {
			head_points[0].x = ex + xm;
			head_points[0].y = ey + ym;
			head_points[1].x = ex + x1;
			head_points[1].y = ey + y1;
			head_points[2].x = ex;
			head_points[2].y = ey;
			head_points[3].x = ex + x2;
			head_points[3].y = ey + y2;
			head_points[4].x = ex + xm;
			head_points[4].y = ey + ym;
			if(!((headstyle & SHAFT_ONLY))) {
				if(curr_arrow_headfilled >= AS_FILLED) {
					// draw filled forward arrow head 
					head_points->style = FS_OPAQUE;
					if(t->filled_polygon)
						(*t->filled_polygon)(5, head_points);
				}
				// draw outline of forward arrow head 
				if(curr_arrow_headfilled == AS_NOFILL)
					draw_clip_polygon(term, 3, head_points+1);
				else if(curr_arrow_headfilled != AS_NOBORDER)
					draw_clip_polygon(term, 5, head_points);
			}
		}
		// backward arrow head 
		if((headstyle & BACKHEAD) && !GPO.V.ClipPoint(sx, sy)) {
			head_points[0].x = sx - xm;
			head_points[0].y = sy - ym;
			head_points[1].x = sx - x1;
			head_points[1].y = sy - y1;
			head_points[2].x = sx;
			head_points[2].y = sy;
			head_points[3].x = sx - x2;
			head_points[3].y = sy - y2;
			head_points[4].x = sx - xm;
			head_points[4].y = sy - ym;
			if(!((headstyle & SHAFT_ONLY))) {
				if(curr_arrow_headfilled >= AS_FILLED) {
					// draw filled backward arrow head 
					head_points->style = FS_OPAQUE;
					if(t->filled_polygon)
						(*t->filled_polygon)(5, head_points);
				}
				// draw outline of backward arrow head 
				if(curr_arrow_headfilled == AS_NOFILL)
					draw_clip_polygon(t, 3, head_points+1);
				else if(curr_arrow_headfilled != AS_NOBORDER)
					draw_clip_polygon(t, 5, head_points);
			}
		}
	}
	// Adjust the length of the shaft so that it doesn't overlap the head 
	if((headstyle & BACKHEAD) && (fabs(len_arrow) >= DBL_EPSILON) && (curr_arrow_headfilled != AS_NOFILL) ) {
		sx -= xm;
		sy -= ym;
	}
	if((headstyle & END_HEAD) && (fabs(len_arrow) >= DBL_EPSILON) && (curr_arrow_headfilled != AS_NOFILL) ) {
		ex += xm;
		ey += ym;
	}
	// Draw the line for the arrow. 
	if(!((headstyle & HEADS_ONLY)))
		GPO.DrawClipLine(t, sx, sy, ex, ey);
	GPO.V.P_ClipArea = clip_save; // Restore previous clipping box 
}
//
// Generic routine for drawing circles or circular arcs.          
// If this feature proves useful, we can add a new terminal entry 
// point term->arc() to the API and let terminals either provide  
// a private implementation or use this generic one.               
//
void do_arc(int cx, int cy/* Center */, double radius, double arc_start, double arc_end/* Limits of arc in degrees */, int style, bool wedge)
{
	gpiPoint vertex[250];
	int i, segments;
	double aspect;
	bool complete_circle;
	// Protect against out-of-range values 
	while(arc_start < 0)
		arc_start += 360.;
	while(arc_end > 360.)
		arc_end -= 360.;
	// Always draw counterclockwise 
	while(arc_end < arc_start)
		arc_end += 360.;
	// Choose how finely to divide this arc into segments 
	// Note: INC=2 caused problems for gnuplot_x11 
#define INC 3.
	segments = (arc_end - arc_start) / INC;
	SETMAX(segments, 1);
	// Calculate the vertices 
	aspect = (double)term->TicV / (double)term->TicH;
	for(i = 0; i<segments; i++) {
		vertex[i].x = cx + cos(DEG2RAD * (arc_start + i*INC)) * radius;
		vertex[i].y = cy + sin(DEG2RAD * (arc_start + i*INC)) * radius * aspect;
	}
#undef INC
	vertex[segments].x = cx + cos(DEG2RAD * arc_end) * radius;
	vertex[segments].y = cy + sin(DEG2RAD * arc_end) * radius * aspect;
	if(fabs(arc_end - arc_start) > 0.1 &&  fabs(arc_end - arc_start) < 359.9) {
		vertex[++segments].x = cx;
		vertex[segments].y = cy;
		vertex[++segments].x = vertex[0].x;
		vertex[segments].y = vertex[0].y;
		complete_circle = FALSE;
	}
	else
		complete_circle = TRUE;
	if(style) { // Fill in the center 
		gpiPoint fillarea[250];
		int in;
		GPO.V.ClipPolygon(vertex, fillarea, segments, &in);
		fillarea[0].style = style;
		if(term->filled_polygon)
			term->filled_polygon(in, fillarea);
	}
	else { // Draw the arc 
		if(!wedge && !complete_circle)
			segments -= 2;
		draw_clip_polygon(term, segments+1, vertex);
	}
}

#define TERM_PROTO
#define TERM_BODY
#define TERM_PUBLIC static

#include "term.h"

#undef TERM_PROTO
#undef TERM_BODY
#undef TERM_PUBLIC

/* Dummy functions for unavailable features */
/* return success if they asked for default - this simplifies code
 * where param is passed as a param. Client can first pass it here,
 * and only if it fails do they have to see what was trying to be done
 */

/* change angle of text.  0 is horizontal left to right.
 * 1 is vertical bottom to top (90 deg rotate)
 */
static int null_text_angle(int ang)
{
	return (ang == 0);
}

/* change justification of text.
 * modes are LEFT (flush left), CENTRE (centred), RIGHT (flush right)
 */
static int null_justify_text(enum JUSTIFY just)
{
	return (just == LEFT);
}
/*
 * Deprecated terminal function (pre-version 3)
 */
static int null_scale(double x, double y)
{
	GPO.IntError(NO_CARET, "Attempt to call deprecated terminal function");
	return FALSE; /* can't be done */
}

static void null_layer(t_termlayer layer)
{
}

static void options_null()
{
	term_options[0] = '\0'; /* we have no options */
}

static void graphics_null()
{
	fprintf(stderr, "WARNING: Plotting with an 'unknown' terminal.\nNo output will be generated. Please select a terminal with 'set terminal'.\n");
}

static void UNKNOWN_null()
{
}

static void MOVE_null(uint x, uint y)
{
}

static void LINETYPE_null(int t)
{
}

static void PUTTEXT_null(uint x, uint y, const char * s)
{
}

static void null_linewidth(double s)
{
}

static int null_set_font(const char * font)
{
	return FALSE; /* Never used!! */
}

static void null_set_color(struct t_colorspec * colorspec)
{
	if(colorspec->type == TC_LT)
		term->linetype(colorspec->lt);
}

static void null_dashtype(int type, t_dashtype * custom_dash_pattern)
{
	/*
	 * If the terminal does not support user-defined dashtypes all we can do
	 * do is fall through to the old (pre-v5) assumption that the dashtype,
	 * if any, is part of the linetype.  We also assume that the color will
	 * be adjusted after this.
	 */
	if(type <= 0)
		type = LT_SOLID;
	term->linetype(type);
}
// 
// setup the magic macros to compile in the right parts of the
// terminal drivers included by term.h
// 
#define TERM_TABLE
#define TERM_TABLE_START(x) ,{
#define TERM_TABLE_END(x)   }
// 
// term_tbl[] contains an entry for each terminal.  "unknown" must be the
//   first, since term is initialized to 0.
// 
static struct termentry term_tbl[] = {
	{"unknown", "Unknown terminal type - not a plotting device",
	 100, 100, 1, 1,
	 1, 1, options_null, UNKNOWN_null, UNKNOWN_null,
	 UNKNOWN_null, null_scale, graphics_null, MOVE_null, MOVE_null,
	 LINETYPE_null, PUTTEXT_null}

#include "term.h"
};

#define TERMCOUNT (sizeof(term_tbl) / sizeof(term_tbl[0]))

void list_terms()
{
	int i;
	char * line_buffer = (char*)gp_alloc(BUFSIZ, "list_terms");
	int sort_idxs[TERMCOUNT];
	/* sort terminal types alphabetically */
	for(i = 0; i < TERMCOUNT; i++)
		sort_idxs[i] = i;
	qsort(sort_idxs, TERMCOUNT, sizeof(int), termcomp);
	/* now sort_idxs[] contains the sorted indices */

	StartOutput();
	strcpy(line_buffer, "\nAvailable terminal types:\n");
	OutLine(line_buffer);
	for(i = 0; i < TERMCOUNT; i++) {
		sprintf(line_buffer, "  %15s  %s\n", term_tbl[sort_idxs[i]].name, term_tbl[sort_idxs[i]].description);
		OutLine(line_buffer);
	}

	EndOutput();
	SAlloc::F(line_buffer);
}

/* Return string with all terminal names.
   Note: caller must free the returned names after use.
 */
char* get_terminals_names()
{
	int i;
	char * buf = (char*)gp_alloc(TERMCOUNT*15, "all_term_names"); /* max 15 chars per name */
	int sort_idxs[TERMCOUNT];
	// sort terminal types alphabetically 
	for(i = 0; i < TERMCOUNT; i++)
		sort_idxs[i] = i;
	qsort(sort_idxs, TERMCOUNT, sizeof(int), termcomp);
	// now sort_idxs[] contains the sorted indices 
	strcpy(buf, " "); // let the string have leading and trailing " " in order to search via strstrt(GPVAL_TERMINALS, " png "); 
	for(i = 0; i < TERMCOUNT; i++)
		sprintf(buf+strlen(buf), "%s ", term_tbl[sort_idxs[i]].name);
	{
		char * names = (char*)gp_alloc(strlen(buf)+1, "all_term_names2");
		strcpy(names, buf);
		SAlloc::F(buf);
		return names;
	}
}

static int termcomp(const generic * arga, const generic * argb)
{
	const int * a = (const int*)arga;
	const int * b = (const int*)argb;
	return( strcasecmp(term_tbl[*a].name, term_tbl[*b].name) );
}
//
// set_term: get terminal number from name on command line
// will change 'term' variable if successful
// 
struct termentry * set_term()
{
	struct termentry * t = NULL;
	if(!GPO.Pgm.EndOfCommand()) {
		char * input_name = gp_input_line + GPO.Pgm.GetCurTokenStartIndex();
		t = change_term(input_name, GPO.Pgm.GetCurTokenLength());
		if(!t && GPO.Pgm.IsStringValue(GPO.Pgm.GetCurTokenIdx()) && (input_name = GPO.TryToGetString())) {
			if(strchr(input_name, ' '))
				*strchr(input_name, ' ') = '\0';
			t = change_term(input_name, strlen(input_name));
			SAlloc::F(input_name);
		}
		else {
			GPO.Pgm.Shift();
		}
	}
	if(!t) {
		change_term("unknown", 7);
		GPO.IntError(GPO.Pgm.GetPrevTokenIdx(), "unknown or ambiguous terminal type; type just 'set terminal' for a list");
	}
	// otherwise the type was changed 
	return (t);
}

/* change_term: get terminal number from name and set terminal type
 *
 * returns NULL for unknown or ambiguous, otherwise is terminal
 * driver pointer
 */
struct termentry * change_term(const char * origname, int length)                    
{
	int i;
	struct termentry * t = NULL;
	bool ambiguous = FALSE;
	/* For backwards compatibility only */
	char * name = (char*)origname;
	if(!strncmp(origname, "X11", length)) {
		name = "x11";
		length = 3;
	}
#ifdef HAVE_CAIROPDF
	/* To allow "set term eps" as short for "set term epscairo" */
	if(!strncmp(origname, "eps", length)) {
		name = "epscairo";
		length = 8;
	}
#endif
#ifdef HAVE_LIBGD
	/* To allow "set term sixel" as short for "set term sixelgd" */
	if(!strncmp(origname, "sixel", length)) {
		name = "sixelgd";
		length = 7;
	}
#endif
	for(i = 0; i < TERMCOUNT; i++) {
		if(!strncmp(name, term_tbl[i].name, length)) {
			if(t)
				ambiguous = TRUE;
			t = term_tbl + i;
			/* Exact match is always accepted */
			if(length == strlen(term_tbl[i].name)) {
				ambiguous = FALSE;
				break;
			}
		}
	}
	if(!t || ambiguous)
		return (NULL);
	/* Success: set terminal type now */
	term = t;
	term_initialised = FALSE;
	/* check that optional fields are initialised to something */
	if(term->text_angle == 0)
		term->text_angle = null_text_angle;
	if(term->justify_text == 0)
		term->justify_text = null_justify_text;
	if(term->point == 0)
		term->point = do_point;
	if(term->arrow == 0)
		term->arrow = do_arrow;
	if(term->pointsize == 0)
		term->pointsize = do_pointsize;
	if(term->linewidth == 0)
		term->linewidth = null_linewidth;
	if(term->layer == 0)
		term->layer = null_layer;
	if(term->tscale <= 0)
		term->tscale = 1.0;
	if(term->set_font == 0)
		term->set_font = null_set_font;
	if(term->set_color == 0) {
		term->set_color = null_set_color;
		term->flags |= TERM_NULL_SET_COLOR;
	}
	SETIFZ(term->dashtype, null_dashtype);
	if(interactive)
		fprintf(stderr, "\nTerminal type is now '%s'\n", term->name);
	invalidate_palette(); /* Invalidate any terminal-specific structures that may be active */
	return (t);
}
/*
 * Find an appropriate initial terminal type.
 * The environment variable GNUTERM is checked first; if that does
 * not exist, then the terminal hardware is checked, if possible,
 * and finally, we can check $TERM for some kinds of terminals.
 * A default can be set with -DDEFAULTTERM=myterm in the Makefile
 * or #define DEFAULTTERM myterm in term.h
 */
/* thanks to osupyr!alden (Dave Alden) for the original GNUTERM code */
void init_terminal()
{
	char * term_name = DEFAULTTERM;
#if defined(__BEOS__) || defined(X11)
	char * env_term = NULL; /* from TERM environment var */
#endif
#ifdef X11
	char * display = NULL;
#endif
	/* GNUTERM environment variable is primary */
	char * gnuterm = getenv("GNUTERM");
	if(gnuterm != (char*)NULL) {
		/* April 2017 - allow GNUTERM to include terminal options */
		char * set_term = "set term ";
		char * set_term_command = (char*)gp_alloc(strlen(set_term) + strlen(gnuterm) + 4, NULL);
		strcpy(set_term_command, set_term);
		strcat(set_term_command, gnuterm);
		do_string(set_term_command);
		SAlloc::F(set_term_command);
		/* replicate environmental variable GNUTERM for internal use */
		Gstring(&(GPO.Ev.AddUdvByName("GNUTERM")->udv_value), gp_strdup(gnuterm));
		return;
	}
	else {
#ifdef VMS
		term_name = vms_init();
#endif /* VMS */
		if(term_name == (char*)NULL && getenv("DOMTERM") != NULL)
			term_name = "domterm";

#ifdef __BEOS__
		env_term = getenv("TERM");
		if(term_name == (char*)NULL
		    && env_term != (char*)NULL && strcmp(env_term, "beterm") == 0)
			term_name = "be";
#endif /* BeOS */
#ifdef QTTERM
		SETIFZ(term_name, "qt");
#endif
#ifdef WXWIDGETS
		SETIFZ(term_name, "wxt");
#endif
#ifdef _WIN32
		SETIFZ(term_name, "win");
#endif /* _WIN32 */
#if defined(__APPLE__) && defined(__MACH__) && defined(HAVE_FRAMEWORK_AQUATERM)
		/* Mac OS X with AquaTerm installed */
		term_name = "aqua";
#endif
#ifdef X11
		env_term = getenv("TERM"); /* try $TERM */
		if(term_name == (char*)NULL && env_term != (char*)NULL && strcmp(env_term, "xterm") == 0)
			term_name = "x11";
		display = getenv("DISPLAY");
		if(term_name == (char*)NULL && display != (char*)NULL)
			term_name = "x11";
		if(X11_Display)
			term_name = "x11";
#endif /* x11 */
#ifdef DJGPP
		term_name = "svga";
#endif
#ifdef GRASS
		term_name = "grass";
#endif
	}
	// We have a name, try to set term type 
	if(term_name != NULL && *term_name != '\0') {
		int namelength = strlen(term_name);
		udvt_entry * name = GPO.Ev.AddUdvByName("GNUTERM");
		Gstring(&name->udv_value, gp_strdup(term_name));
		if(strchr(term_name, ' '))
			namelength = strchr(term_name, ' ') - term_name;
		/* Force the terminal to initialize default fonts, etc.	This prevents */
		/* segfaults and other strangeness if you set GNUTERM to "post" or    */
		/* "png" for example. However, calling X11_options() is expensive due */
		/* to the fork+execute of gnuplot_x11 and x11 can tolerate not being  */
		/* initialized until later.                                           */
		/* Note that gp_input_line[] is blank at this point.	              */
		if(change_term(term_name, namelength)) {
			if(strcmp(term->name, "x11"))
				term->options();
			return;
		}
		fprintf(stderr, "Unknown or ambiguous terminal name '%s'\n", term_name);
	}
	change_term("unknown", 7);
}
// 
// test terminal by drawing border and text 
// called from command test 
// 
//void test_term(termentry * pTerm)
void GnuPlot::TestTerminal(termentry * pTerm)
{
	static t_colorspec black = BLACK_COLORSPEC;
	//struct termentry * t = term;
	const char * str;
	int x, y, xl, yl, i;
	int xmax_t, ymax_t, x0, y0;
	char label[MAX_ID_LEN];
	int key_entry_height;
	int p_width;
	bool already_in_enhanced_text_mode = LOGIC(pTerm->flags & TERM_ENHANCED_TEXT);
	if(!already_in_enhanced_text_mode)
		do_string("set termopt enh");
	term_start_plot();
	screen_ok = FALSE;
	xmax_t = (pTerm->xmax * V.XSize);
	ymax_t = (pTerm->ymax * V.YSize);
	x0 = (V.XOffset * pTerm->xmax);
	y0 = (V.YOffset * pTerm->ymax);
	p_width = pointsize * pTerm->TicH;
	key_entry_height = pointsize * pTerm->TicV * 1.25;
	if(key_entry_height < pTerm->ChrV)
		key_entry_height = pTerm->ChrV;
	// Sync point for epslatex text positioning 
	(pTerm->layer)(TERM_LAYER_FRONTTEXT);
	// border linetype 
	(pTerm->linewidth)(1.0);
	(pTerm->linetype)(LT_BLACK);
	newpath(pTerm);
	(pTerm->move)(x0, y0);
	(pTerm->vector)(x0 + xmax_t - 1, y0);
	(pTerm->vector)(x0 + xmax_t - 1, y0 + ymax_t - 1);
	(pTerm->vector)(x0, y0 + ymax_t - 1);
	(pTerm->vector)(x0, y0);
	closepath(pTerm);
	// Echo back the current terminal type 
	if(!strcmp(pTerm->name, "unknown"))
		IntError(NO_CARET, "terminal type is unknown");
	else {
		char tbuf[64];
		(pTerm->justify_text)(LEFT);
		sprintf(tbuf, "%s  terminal test", pTerm->name);
		(pTerm->put_text)(x0 + pTerm->ChrH * 2, y0 + ymax_t - pTerm->ChrV, tbuf);
		sprintf(tbuf, "gnuplot version %s.%s  ", gnuplot_version, gnuplot_patchlevel);
		(pTerm->put_text)(x0 + pTerm->ChrH * 2, static_cast<uint>(y0 + ymax_t - pTerm->ChrV * 2.25), tbuf);
	}
	(pTerm->linetype)(LT_AXIS);
	(pTerm->move)(x0 + xmax_t / 2, y0);
	(pTerm->vector)(x0 + xmax_t / 2, y0 + ymax_t - 1);
	(pTerm->move)(x0, y0 + ymax_t / 2);
	(pTerm->vector)(x0 + xmax_t - 1, y0 + ymax_t / 2);
	// How well can we estimate width and height of characters?
	// Textbox fill shows true size, surrounding box shows the generic estimate
	// used to reserve space during plot layout.
	if(TRUE) {
		text_label sample; // = EMPTY_LABELSTRUCT;
		textbox_style save_opts = textbox_opts[0];
		textbox_style * textbox = &textbox_opts[0];
		sample.text = "12345678901234567890";
		sample.pos = CENTRE;
		sample.boxed = -1;
		textbox->opaque = TRUE;
		textbox->noborder = TRUE;
		textbox->fillcolor.type = TC_RGB;
		textbox->fillcolor.lt = 0xccccee;
		/* disable extra space around text */
		textbox->xmargin = 0;
		textbox->ymargin = 0;
		(pTerm->linetype)(LT_SOLID);
		write_label(pTerm, xmax_t/2, ymax_t/2, &sample);
		textbox_opts[0] = save_opts;
		sample.boxed = 0;
		sample.text = "true vs. estimated text dimensions";
		write_label(pTerm, xmax_t/2, ymax_t/2 + 1.5 * pTerm->ChrV, &sample);
		newpath(pTerm);
		(pTerm->move)(x0 + xmax_t / 2 - pTerm->ChrH * 10, y0 + ymax_t / 2 + pTerm->ChrV / 2);
		(pTerm->vector)(x0 + xmax_t / 2 + pTerm->ChrH * 10, y0 + ymax_t / 2 + pTerm->ChrV / 2);
		(pTerm->vector)(x0 + xmax_t / 2 + pTerm->ChrH * 10, y0 + ymax_t / 2 - pTerm->ChrV / 2);
		(pTerm->vector)(x0 + xmax_t / 2 - pTerm->ChrH * 10, y0 + ymax_t / 2 - pTerm->ChrV / 2);
		(pTerm->vector)(x0 + xmax_t / 2 - pTerm->ChrH * 10, y0 + ymax_t / 2 + pTerm->ChrV / 2);
		closepath(pTerm);
	}
	// Test for enhanced text 
	(pTerm->linetype)(LT_BLACK);
	if(pTerm->flags & TERM_ENHANCED_TEXT) {
		const char * tmptext1 =   "Enhanced text:   {x@_{0}^{n+1}}";
		const char * tmptext2 = "&{Enhanced text:  }{/:Bold Bold}{/:Italic  Italic}";
		(pTerm->put_text)(x0 + xmax_t * 0.5, y0 + ymax_t * 0.40, tmptext1);
		(pTerm->put_text)(x0 + xmax_t * 0.5, y0 + ymax_t * 0.35, tmptext2);
		(pTerm->set_font)("");
		if(!already_in_enhanced_text_mode)
			do_string("set termopt noenh");
	}
	/* test justification */
	(pTerm->justify_text)(LEFT);
	(pTerm->put_text)(x0 + xmax_t / 2, y0 + ymax_t / 2 + pTerm->ChrV * 6, "left justified");
	str = "centre+d text";
	if((pTerm->justify_text)(CENTRE))
		(pTerm->put_text)(x0 + xmax_t / 2, y0 + ymax_t / 2 + pTerm->ChrV * 5, str);
	else
		(pTerm->put_text)(x0 + xmax_t / 2 - strlen(str) * pTerm->ChrH / 2, y0 + ymax_t / 2 + pTerm->ChrV * 5, str);
	str = "right justified";
	if((pTerm->justify_text)(RIGHT))
		(pTerm->put_text)(x0 + xmax_t / 2, y0 + ymax_t / 2 + pTerm->ChrV * 4, str);
	else
		(pTerm->put_text)(x0 + xmax_t / 2 - strlen(str) * pTerm->ChrH, y0 + ymax_t / 2 + pTerm->ChrV * 4, str);
	// test tic size 
	(pTerm->linetype)(2);
	(pTerm->move)((uint)(x0 + xmax_t / 2 + pTerm->TicH * (1 + AxS[FIRST_X_AXIS].ticscale)), y0 + (uint)ymax_t - 1);
	(pTerm->vector)((uint)(x0 + xmax_t / 2 + pTerm->TicH * (1 + AxS[FIRST_X_AXIS].ticscale)),
	    (uint)(y0 + ymax_t - AxS[FIRST_X_AXIS].ticscale * pTerm->TicV));
	(pTerm->move)((uint)(x0 + xmax_t / 2), y0 + (uint)(ymax_t - pTerm->TicV * (1 + AxS[FIRST_X_AXIS].ticscale)));
	(pTerm->vector)((uint)(x0 + xmax_t / 2 + AxS[FIRST_X_AXIS].ticscale * pTerm->TicH), (uint)(y0 + ymax_t - pTerm->TicV * (1 + AxS[FIRST_X_AXIS].ticscale)));
	(pTerm->justify_text)(RIGHT);
	(pTerm->put_text)(x0 + (uint)(xmax_t / 2 - 1* pTerm->ChrH), y0 + (uint)(ymax_t - pTerm->ChrV), "show ticscale");
	(pTerm->justify_text)(LEFT);
	(pTerm->linetype)(LT_BLACK);
	// test line and point types 
	x = x0 + xmax_t - pTerm->ChrH * 7 - p_width;
	y = y0 + ymax_t - key_entry_height;
	(pTerm->pointsize)(pointsize);
	for(i = -2; y > y0 + key_entry_height; i++) {
		lp_style_type ls; // = DEFAULT_LP_STYLE_TYPE;
		ls.l_width = 1;
		load_linetype(pTerm, &ls, i+1);
		TermApplyLpProperties(pTerm, &ls);
		sprintf(label, "%d", i + 1);
		if((pTerm->justify_text)(RIGHT))
			(pTerm->put_text)(x, y, label);
		else
			(pTerm->put_text)(x - strlen(label) * pTerm->ChrH, y, label);
		(pTerm->move)(x + pTerm->ChrH, y);
		(pTerm->vector)(x + pTerm->ChrH * 5, y);
		if(i >= -1)
			(pTerm->point)(x + pTerm->ChrH * 6 + p_width / 2, y, i);
		y -= key_entry_height;
	}
	// test arrows (should line up with rotated text) 
	(pTerm->linewidth)(1.0);
	(pTerm->linetype)(0);
	(pTerm->dashtype)(DASHTYPE_SOLID, NULL);
	x = static_cast<int>(x0 + 2.0 * pTerm->ChrV);
	y = y0 + ymax_t/2;
	xl = pTerm->TicH * 7;
	yl = pTerm->TicV * 7;
	i = curr_arrow_headfilled;
	curr_arrow_headfilled = AS_NOBORDER;
	(pTerm->arrow)(x, y-yl, x, y+yl, BOTH_HEADS);
	curr_arrow_headfilled = AS_EMPTY;
	(pTerm->arrow)(x, y, x + xl, y + yl, END_HEAD);
	curr_arrow_headfilled = AS_NOFILL;
	(pTerm->arrow)(x, y, x + xl, y - yl, END_HEAD);
	curr_arrow_headfilled = (arrowheadfill)i;
	// test text angle (should match arrows) 
	(pTerm->linetype)(0);
	str = "rotated ce+ntred text";
	if((pTerm->text_angle)(TEXT_VERTICAL)) {
		if((pTerm->justify_text)(CENTRE))
			(pTerm->put_text)(x0 + pTerm->ChrV, y0 + ymax_t / 2, str);
		else
			(pTerm->put_text)(x0 + pTerm->ChrV, y0 + ymax_t / 2 - strlen(str) * pTerm->ChrH / 2, str);
		(pTerm->justify_text)(LEFT);
		str = "  rotate by +45";
		(pTerm->text_angle)(45);
		(pTerm->put_text)(x0 + pTerm->ChrV * 3, y0 + ymax_t / 2, str);
		(pTerm->justify_text)(LEFT);
		str = "  rotate by -45";
		(pTerm->text_angle)(-45);
		(pTerm->put_text)(x0 + pTerm->ChrV * 3, y0 + ymax_t / 2, str);
	}
	else {
		(pTerm->justify_text)(LEFT);
		(pTerm->put_text)(x0 + pTerm->ChrH * 2, y0 + ymax_t / 2, "cannot rotate text");
	}
	(pTerm->justify_text)(LEFT);
	(pTerm->text_angle)(0);
	// test line widths 
	(pTerm->justify_text)(LEFT);
	xl = xmax_t / 10;
	yl = ymax_t / 25;
	x = static_cast<int>(x0 + xmax_t * 0.075);
	y = y0 + yl;

	for(i = 1; i<7; i++) {
		(pTerm->linewidth)((double)(i)); (pTerm->linetype)(LT_BLACK);
		(pTerm->move)(x, y); 
		(pTerm->vector)(x+xl, y);
		sprintf(label, "  lw %1d", i);
		(pTerm->put_text)(x+xl, y, label);
		y += yl;
	}
	(pTerm->put_text)(x, y, "linewidth");
	// test native dashtypes (_not_ the 'set mono' sequence) 
	(pTerm->justify_text)(LEFT);
	xl = xmax_t / 10;
	yl = ymax_t / 25;
	x = static_cast<int>(x0 + xmax_t * 0.3);
	y = y0 + yl;
	for(i = 0; i<5; i++) {
		(pTerm->linewidth)(1.0);
		(pTerm->linetype)(LT_SOLID);
		(pTerm->dashtype)(i, NULL);
		(pTerm->set_color)(&black);
		(pTerm->move)(x, y); 
		(pTerm->vector)(x+xl, y);
		sprintf(label, "  dt %1d", i+1);
		(pTerm->put_text)(x+xl, y, label);
		y += yl;
	}
	(pTerm->put_text)(x, y, "dashtype");
	// test fill patterns 
	x = static_cast<int>(x0 + xmax_t * 0.5);
	y = y0;
	xl = xmax_t / 40;
	yl = ymax_t / 8;
	(pTerm->linewidth)(1.0);
	(pTerm->linetype)(LT_BLACK);
	(pTerm->justify_text)(CENTRE);
	(pTerm->put_text)(x+xl*7, y + yl+pTerm->ChrV*1.5, "pattern fill");
	for(i = 0; i < 9; i++) {
		const int style = ((i<<4) + FS_PATTERN);
		if(pTerm->fillbox)
			(pTerm->fillbox)(style, x, y, xl, yl);
		newpath(pTerm);
		(pTerm->move)(x, y);
		(pTerm->vector)(x, y+yl);
		(pTerm->vector)(x+xl, y+yl);
		(pTerm->vector)(x+xl, y);
		(pTerm->vector)(x, y);
		closepath(pTerm);
		sprintf(label, "%2d", i);
		(pTerm->put_text)(x+xl/2, y+yl+pTerm->ChrV*0.5, label);
		x += xl * 1.5;
	}
	{
		int cen_x = x0 + (int)(0.70 * xmax_t);
		int cen_y = y0 + (int)(0.83 * ymax_t);
		int radius = xmax_t / 20;
		// test pm3d -- filled_polygon(), but not set_color() 
		if(pTerm->filled_polygon) {
			int i, j;
#define NUMBER_OF_VERTICES 6
			int n = NUMBER_OF_VERTICES;
			gpiPoint corners[NUMBER_OF_VERTICES+1];
#undef  NUMBER_OF_VERTICES
			for(j = 0; j<=1; j++) {
				int ix = cen_x + j*radius;
				int iy = cen_y - j*radius/2;
				for(i = 0; i < n; i++) {
					corners[i].x = ix + radius * cos(2*M_PI*i/n);
					corners[i].y = iy + radius * sin(2*M_PI*i/n);
				}
				corners[n].x = corners[0].x;
				corners[n].y = corners[0].y;
				if(j == 0) {
					(pTerm->linetype)(2);
					corners->style = FS_OPAQUE;
				}
				else {
					(pTerm->linetype)(1);
					corners->style = FS_TRANSPARENT_SOLID + (50<<4);
				}
				term->filled_polygon(n+1, corners);
			}
			str = "filled polygons:";
		}
		else
			str = "No filled polygons";
		(pTerm->linetype)(LT_BLACK);
		i = ((pTerm->justify_text)(CENTRE)) ? 0 : pTerm->ChrH * strlen(str) / 2;
		(pTerm->put_text)(cen_x - i, static_cast<uint>(cen_y + radius + pTerm->ChrV * 0.5), str);
	}
	term_end_plot();
}
/*
 * This is an abstraction of the enhanced text mode originally written
 * for the postscript terminal driver by David Denholm and Matt Heffron.
 * I have split out a terminal-independent recursive syntax-parser
 * routine that can be shared by all drivers that want to add support
 * for enhanced text mode.
 *
 * A driver that wants to make use of this common framework must provide
 * three new entries in TERM_TABLE:
 *      void *enhanced_open   (char *fontname, double fontsize, double base,
 *                             bool widthflag, bool showflag,
 *                             int overprint)
 *      void *enhanced_writec (char c)
 *      void *enhanced_flush  ()
 *
 * Each driver also has a separate ENHXX_put_text() routine that replaces
 * the normal (term->put_text) routine while in enhanced mode.
 * This routine must initialize the following globals used by the shared code:
 *      enhanced_fontscale      converts font size to device resolution units
 *      enhanced_escape_format  used to process octal escape characters \xyz
 *
 * I bent over backwards to make the output of the revised code identical
 * to the output of the original postscript version.  That means there is
 * some cruft left in here (enhanced_max_height for one thing) that is
 * probably irrelevant to any new drivers using the code.
 *
 * Ethan A Merritt - November 2003
 */
#ifdef DEBUG_ENH
	#define ENH_DEBUG(x) printf x;
#else
	#define ENH_DEBUG(x)
#endif

void do_enh_writec(int c)
{
	// Guard against buffer overflow 
	if(enhanced_cur_text >= ENHANCED_TEXT_MAX)
		return;
	// note: c is meant to hold a char, but is actually an int, for
	// the same reasons applying to putc() and friends 
	*enhanced_cur_text++ = c;
}
// 
// Process a bit of string, and return the last character used.
// p is start of string
// brace is TRUE to keep processing to }, FALSE to do one character only
// fontname & fontsize are obvious
// base is the current baseline
// widthflag is TRUE if the width of this should count, FALSE for zero width boxes
// showflag is TRUE if this should be shown, FALSE if it should not be shown (like TeX \phantom)
// overprint is 0 for normal operation,
//   1 for the underprinted text (included in width calculation),
//   2 for the overprinted text (not included in width calc)
//   (overprinted text is centered horizontally on underprinted text
// 
const char * enhanced_recursion(const char * p, bool brace, char * fontname, double fontsize, double base, bool widthflag, bool showflag, int overprint)
{
	// Keep track of the style of the font passed in at this recursion level 
	bool wasitalic = (strstr(fontname, ":Italic") != NULL);
	bool wasbold = (strstr(fontname, ":Bold") != NULL);
	FPRINTF((stderr, "RECURSE WITH \"%s\", %d %s %.1f %.1f %d %d %d", p, brace, fontname, fontsize, base, widthflag, showflag, overprint));
	// Start each recursion with a clean string 
	(term->enhanced_flush)();
	if(base + fontsize > enhanced_max_height) {
		enhanced_max_height = base + fontsize;
		ENH_DEBUG(("Setting max height to %.1f\n", enhanced_max_height));
	}
	if(base < enhanced_min_height) {
		enhanced_min_height = base;
		ENH_DEBUG(("Setting min height to %.1f\n", enhanced_min_height));
	}
	while(*p) {
		double shift;
		// 
		// EAM Jun 2009 - treating bytes one at a time does not work for multibyte
		// encodings, including utf-8. If we hit a byte with the high bit set, test
		// whether it starts a legal UTF-8 sequence and if so copy the whole thing.
		// Other multibyte encodings are still a problem.
		// Gnuplot's other defined encodings are all single-byte; for those we
		// really do want to treat one byte at a time.
		// 
		if((*p & 0x80) && (encoding == S_ENC_DEFAULT || encoding == S_ENC_UTF8)) {
			ulong utf8char;
			const char * nextchar = p;
			(term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, overprint);
			if(utf8toulong(&utf8char, &nextchar)) { /* Legal UTF8 sequence */
				while(p < nextchar)
					(term->enhanced_writec)(*p++);
				p--;
			}
			else {                          /* Some other multibyte encoding? */
				(term->enhanced_writec)(*p);
			}
/* shige : for Shift_JIS */
		}
		else if((*p & 0x80) && (encoding == S_ENC_SJIS)) {
			(term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, overprint);
			(term->enhanced_writec)(*(p++));
			(term->enhanced_writec)(*p);
		}
		else
			switch(*p) {
				case '}':
				    /*{{{  deal with it*/
				    if(brace)
					    return (p);
				    GPO.IntWarn(NO_CARET, "enhanced text parser - spurious }");
				    break;
				/*}}}*/
				case '_':
				case '^':
				    /*{{{  deal with super/sub script*/
				    shift = (*p == '^') ? 0.5 : -0.3;
				    (term->enhanced_flush)();
				    p = enhanced_recursion(p + 1, FALSE, fontname, fontsize * 0.8, base + shift * fontsize, widthflag, showflag, overprint);
				    break;
				/*}}}*/
				case '{':
			    {
				    bool isitalic = FALSE, isbold = FALSE, isnormal = FALSE;
				    const char * start_of_fontname = NULL;
				    const char * end_of_fontname = NULL;
				    char * localfontname = NULL;
				    char ch;
				    double f = fontsize, ovp;
				    // Mar 2014 - this will hold "fontfamily{:Italic}{:Bold}" 
				    char * styledfontname = NULL;
				    /*{{{  recurse (possibly with a new font) */
				    ENH_DEBUG(("Dealing with {\n"));
				    /* 30 Sep 2016:  Remove incorrect whitespace-eating loop going */
				    /* waaay back to 31-May-2000 */        /* while (*++p == ' '); */
				    ++p;
				    /* get vertical offset (if present) for overprinted text */
				    if(overprint == 2) {
					    char * end;
					    ovp = strtod(p, &end);
					    p = end;
					    if(term->flags & TERM_IS_POSTSCRIPT)
						    base = ovp*f;
					    else
						    base += ovp*f;
				    }
				    --p;
				    if(*++p == '/') {
					    /* then parse a fontname, optional fontsize */
					    while(*++p == ' ')
						    ; /* do nothing */
					    if(*p=='-') {
						    while(*++p == ' ')
							    ; /* do nothing */
					    }
					    start_of_fontname = p;
					    /* Allow font name to be in quotes.
					     * This makes it possible to handle font names containing spaces.
					     */
					    if(*p == '\'' || *p == '"') {
						    ++p;
						    while(*p != '\0' && *p != '}' && *p != *start_of_fontname)
							    ++p;
						    if(*p != *start_of_fontname) {
							    GPO.IntWarn(NO_CARET, "cannot interpret font name %s", start_of_fontname);
							    break;
						    }
						    start_of_fontname++;
						    end_of_fontname = p++;
						    ch = *p;
					    }
					    else {
						    /* Normal unquoted font name */
						    while((ch = *p) > ' ' && ch != '=' && ch != '*' && ch != '}' && ch != ':')
							    ++p;
						    end_of_fontname = p;
					    }
					    do {
						    if(ch == '=') {
							    /* get optional font size */
							    char * end;
							    p++;
							    ENH_DEBUG(("Calling strtod(\"%s\") ...", p));
							    f = strtod(p, &end);
							    p = end;
							    ENH_DEBUG(("Returned %.1f and \"%s\"\n", f, p));
							    if(f == 0)
								    f = fontsize;
							    else
								    f *= enhanced_fontscale; /* remember the scaling */
							    ENH_DEBUG(("Font size %.1f\n", f));
						    }
						    else if(ch == '*') {
							    /* get optional font size scale factor */
							    char * end;
							    p++;
							    ENH_DEBUG(("Calling strtod(\"%s\") ...", p));
							    f = strtod(p, &end);
							    p = end;
							    ENH_DEBUG(("Returned %.1f and \"%s\"\n", f, p));
							    if(f != 0.0)
								    f *= fontsize; /* apply the scale factor */
							    else
								    f = fontsize;
							    ENH_DEBUG(("Font size %.1f\n", f));
						    }
						    else if(ch == ':') {
							    /* get optional style markup attributes */
							    p++;
							    if(!strncmp(p, "Bold", 4))
								    isbold = TRUE;
							    if(!strncmp(p, "Italic", 6))
								    isitalic = TRUE;
							    if(!strncmp(p, "Normal", 6))
								    isnormal = TRUE;
							    while(isalpha((uchar)*p)) {
								    p++;
							    }
						    }
					    } while(((ch = *p) == '=') || (ch == ':') || (ch == '*'));
					    if(ch == '}')
						    GPO.IntWarn(NO_CARET, "bad syntax in enhanced text string");
					    if(*p == ' ') /* Eat up a single space following a font spec */
						    ++p;
					    if(!start_of_fontname || (start_of_fontname == end_of_fontname)) {
						    /* Use the font name passed in to us */
						    localfontname = gp_strdup(fontname);
					    }
					    else {
						    /* We found a new font name {/Font ...} */
						    int len = end_of_fontname - start_of_fontname;
						    localfontname = (char*)gp_alloc(len+1, "localfontname");
						    strncpy(localfontname, start_of_fontname, len);
						    localfontname[len] = '\0';
					    }
				    }
				    /*}}}*/
				    /* Collect cumulative style markup before passing it in the font name */
				    isitalic = (wasitalic || isitalic) && !isnormal;
				    isbold = (wasbold || isbold) && !isnormal;
				    styledfontname = stylefont(localfontname ? localfontname : fontname, isbold, isitalic);
				    p = enhanced_recursion(p, TRUE, styledfontname, f, base, widthflag, showflag, overprint);
				    (term->enhanced_flush)();
				    SAlloc::F(styledfontname);
				    SAlloc::F(localfontname);
				    break;
			    } /* case '{' */
				case '@':
				    /*{{{  phantom box - prints next 'char', then restores currentpoint */
				    (term->enhanced_flush)();
				    (term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, 3);
				    p = enhanced_recursion(++p, FALSE, fontname, fontsize, base, widthflag, showflag, overprint);
				    (term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, 4);
				    break;
				/*}}}*/

				case '&':
				    /*{{{  character skip - skips space equal to length of character(s) */
				    (term->enhanced_flush)();
				    p = enhanced_recursion(++p, FALSE, fontname, fontsize, base, widthflag, FALSE, overprint);
				    break;
				/*}}}*/

				case '~':
				    /*{{{ overprinted text */
				    /* the second string is overwritten on the first, centered
				     * horizontally on the first and (optionally) vertically
				     * shifted by an amount specified (as a fraction of the
				     * current fontsize) at the beginning of the second string

				     * Note that in this implementation neither the under- nor
				     * overprinted string can contain syntax that would result
				     * in additional recursions -- no subscripts,
				     * superscripts, or anything else, with the exception of a
				     * font definition at the beginning of the text */

				    (term->enhanced_flush)();
				    p = enhanced_recursion(++p, FALSE, fontname, fontsize, base, widthflag, showflag, 1);
				    (term->enhanced_flush)();
				    if(!*p)
					    break;
				    p = enhanced_recursion(++p, FALSE, fontname, fontsize, base, FALSE, showflag, 2);
				    overprint = 0; /* may not be necessary, but just in case . . . */
				    break;
				/*}}}*/

				case '(':
				case ')':
				    /*{{{  an escape and print it */
				    /* special cases */
				    (term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, overprint);
				    if(term->flags & TERM_IS_POSTSCRIPT)
					    (term->enhanced_writec)('\\');
				    (term->enhanced_writec)(*p);
				    break;
				/*}}}*/

				case '\\':
				    /*{{{  various types of escape sequences, some context-dependent */
				    (term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, overprint);

				    /*     Unicode represented as \U+hhhhh where hhhhh is hexadecimal code point.
				     *     For UTF-8 encoding we translate hhhhh to a UTF-8 byte sequence and
				     *     output the bytes one by one.
				     */
				    if(p[1] == 'U' && p[2] == '+') {
					    if(encoding == S_ENC_UTF8) {
						    uint32_t codepoint;
						    uchar utf8char[8];
						    int i, length;
						    if(strlen(&(p[3])) < 4)
							    break;
						    if(sscanf(&(p[3]), "%5x", &codepoint) != 1)
							    break;
						    length = ucs4toutf8(codepoint, utf8char);
						    p += (codepoint > 0xFFFF) ? 7 : 6;
						    for(i = 0; i<length; i++)
							    (term->enhanced_writec)(utf8char[i]);
						    break;
					    }

					    /*     FIXME: non-utf8 environments not yet supported.
					     *     Note that some terminals may have an alternative way to handle
					     *unicode
					     *     escape sequences that is not dependent on encoding.
					     *     E.g. svg and html output could convert to xml sequences &#xhhhh;
					     *     For these cases we must retain the leading backslash so that the
					     *     unicode escape sequence can be recognized by the terminal driver.
					     */
					    (term->enhanced_writec)(p[0]);
					    break;
				    }

				    /* Enhanced mode always uses \xyz as an octal character representation
				     * but each terminal type must give us the actual output format wanted.
				     * pdf.trm wants the raw character code, which is why we use strtol();
				     * most other terminal types want some variant of "\\%o".
				     */
				    if(p[1] >= '0' && p[1] <= '7') {
					    char * e, escape[16], octal[4] = {'\0', '\0', '\0', '\0'};
					    octal[0] = *(++p);
					    if(p[1] >= '0' && p[1] <= '7') {
						    octal[1] = *(++p);
						    if(p[1] >= '0' && p[1] <= '7')
							    octal[2] = *(++p);
					    }
					    sprintf(escape, enhanced_escape_format, strtol(octal, NULL, 8));
					    for(e = escape; *e; e++) {
						    (term->enhanced_writec)(*e);
					    }
					    break;
				    }

				    /* This was the original (prior to version 4) enhanced text code specific
				     * to the reserved characters of PostScript.
				     */
				    if(term->flags & TERM_IS_POSTSCRIPT) {
					    if(p[1]=='\\' || p[1]=='(' || p[1]==')') {
						    (term->enhanced_writec)('\\');
					    }
					    else if(strchr("^_@&~{}", p[1]) == NULL) {
						    (term->enhanced_writec)('\\');
						    (term->enhanced_writec)('\\');
						    break;
					    }
				    }

				    /* Step past the backslash character in the input stream */
				    ++p;

				    /* HBB: Avoid broken output if there's a \ exactly at the end of the line */
				    if(*p == '\0') {
					    GPO.IntWarn(NO_CARET, "enhanced text parser -- spurious backslash");
					    break;
				    }

				    /* SVG requires an escaped '&' to be passed as something else */
				    /* FIXME: terminal-dependent code does not belong here */
				    if(*p == '&' && encoding == S_ENC_DEFAULT && !strcmp(term->name, "svg")) {
					    (term->enhanced_writec)('\376');
					    break;
				    }
				    /* print the character following the backslash */
				    (term->enhanced_writec)(*p);
				    break;
				/*}}}*/

				default:
				    /*{{{  print it */
				    (term->enhanced_open)(fontname, fontsize, base, widthflag, showflag, overprint);
				    (term->enhanced_writec)(*p);
				    /*}}}*/
			}/* switch (*p) */

		/* like TeX, we only do one character in a recursion, unless it's
		 * in braces
		 */

		if(!brace) {
			(term->enhanced_flush)();
			return(p); /* the ++p in the outer copy will increment us */
		}
		if(*p) /* only not true if { not terminated, I think */
			++p;
	} /* while (*p) */
	(term->enhanced_flush)();
	return p;
}

/* Strip off anything trailing the requested font name,
 * then add back markup requests.
 */
char * stylefont(const char * fontname, bool isbold, bool isitalic)
{
	int div;
	char * markup = (char*)gp_alloc(strlen(fontname) + 16, "font markup");
	strcpy(markup, fontname);
	/* base font name can be followed by ,<size> or :Variant */
	if((div = strcspn(markup, ",:")))
		markup[div] = '\0';
	if(isbold)
		strcat(markup, ":Bold");
	if(isitalic)
		strcat(markup, ":Italic");
	FPRINTF((stderr, "MARKUP FONT: %s -> %s\n", fontname, markup));
	return markup;
}

/* Called after the end of recursion to check for errors */
void enh_err_check(const char * str)
{
	if(*str == '}')
		GPO.IntWarn(NO_CARET, "enhanced text mode parser - ignoring spurious }");
	else
		GPO.IntWarn(NO_CARET, "enhanced text mode parsing error");
}

/*
 * Text strings containing control information for enhanced text mode
 * contain more characters than will actually appear in the output.
 * This makes it hard to estimate how much horizontal space on the plot
 * (e.g. in the key box) must be reserved to hold them.  To approximate
 * the eventual length we switch briefly to the dummy terminal driver
 * "estimate.trm" and then switch back to the current terminal.
 * If better, perhaps terminal-specific methods of estimation are
 * developed later they can be slotted into this one call site.
 *
 * Dec 2019: height is relative to original font size
 *		DEBUG: currently pegged at 10pt - we should do better!
 */
int estimate_strlen(const char * text, double * height)
{
	int len;
	char * s;
	double estimated_fontheight = 1.0;
	if(term->flags & TERM_IS_LATEX)
		return strlen_tex(text);
#ifdef GP_ENH_EST
	if(strchr(text, '\n') || (term->flags & TERM_ENHANCED_TEXT)) {
		struct termentry * tsave = term;
		term = &ENHest;
		term->put_text(0, 0, text);
		len = term->xmax;
		estimated_fontheight = term->ymax / 10.;
		term = tsave;
		// Assume that unicode escape sequences  \U+xxxx will generate a single character 
		// ENHest_plaintext is filled in by the put_text() call to estimate.trm           
		s = ENHest_plaintext;
		while((s = contains_unicode(s)) != NULL) {
			len -= 6;
			s += 6;
		}
		FPRINTF((stderr, "Estimating length %d height %g for enhanced text \"%s\"", len, estimated_fontheight, text));
		FPRINTF((stderr, "  plain text \"%s\"\n", ENHest_plaintext));
	}
	else if(encoding == S_ENC_UTF8)
		len = strwidth_utf8(text);
	else
#endif
	len = strlen(text);
	ASSIGN_PTR(height, estimated_fontheight);
	return len;
}

/*
 * Use estimate.trm to mock up a non-enhanced approximation of the
 * original string.
 */
char * estimate_plaintext(char * enhancedtext)
{
	if(enhancedtext == NULL)
		return NULL;
	estimate_strlen(enhancedtext, NULL);
	return ENHest_plaintext;
}

void ignore_enhanced(bool flag)
{
	ignore_enhanced_text = flag;
}
// 
// Simple-minded test for whether the point (x,y) is in bounds for the current terminal.
// Some terminals can do their own clipping, and can clip partial objects.
// If the flag TERM_CAN_CLIP is set, we skip this relative crude test and let the
// driver or the hardware handle clipping.
// 
bool on_page(int x, int y)
{
	if(term->flags & TERM_CAN_CLIP)
		return TRUE;
	if((0 < x && x < term->xmax) && (0 < y && y < term->ymax))
		return TRUE;
	return FALSE;
}
// 
// Utility routine for drivers to accept an explicit size for the output image.
// 
//GpSizeUnits parse_term_size(float * pXSize, float * pYSize, GpSizeUnits default_units)
GpSizeUnits GnuPlot::ParseTermSize(float * pXSize, float * pYSize, GpSizeUnits default_units)
{
	GpSizeUnits units = default_units;
	if(Pgm.EndOfCommand())
		IntErrorCurToken("size requires two numbers:  xsize, ysize");
	*pXSize = RealExpression();
	if(Pgm.AlmostEqualsCur("in$ches")) {
		Pgm.Shift();
		units = INCHES;
	}
	else if(Pgm.EqualsCur("cm")) {
		Pgm.Shift();
		units = CM;
	}
	switch(units) {
		case INCHES: *pXSize *= gp_resolution; break;
		case CM:     *pXSize *= (float)gp_resolution / 2.54f; break;
		case PIXELS:
		default:     break;
	}
	if(!Pgm.EqualsCurShift(","))
		IntErrorCurToken("size requires two numbers:  xsize, ysize");
	*pYSize = RealExpression();
	if(Pgm.AlmostEqualsCur("in$ches")) {
		Pgm.Shift();
		units = INCHES;
	}
	else if(Pgm.EqualsCur("cm")) {
		Pgm.Shift();
		units = CM;
	}
	switch(units) {
		case INCHES: *pYSize *= gp_resolution; break;
		case CM:     *pYSize *= (float)gp_resolution / 2.54f; break;
		case PIXELS:
		default:     break;
	}
	if(*pXSize < 1.0f || *pYSize < 1.0f)
		IntErrorCurToken("size: out of range");
	return units;
}
// 
// Wrappers for newpath and closepath
// 
void FASTCALL newpath(termentry * pTerm)
{
	if(pTerm->path)
		(*pTerm->path)(0);
}

void FASTCALL closepath(termentry * pTerm)
{
	if(pTerm->path)
		(*pTerm->path)(1);
}
// 
// Squeeze all fill information into the old style parameter.
// The terminal drivers know how to extract the information.
// We assume that the style (int) has only 16 bit, therefore we take
// 4 bits for the style and allow 12 bits for the corresponding fill parameter.
// This limits the number of styles to 16 and the fill parameter's
// values to the range 0...4095, which seems acceptable.
// 
int style_from_fill(const fill_style_type * fs)
{
	int fillpar, style;
	switch(fs->fillstyle) {
		case FS_SOLID:
		case FS_TRANSPARENT_SOLID:
		    fillpar = fs->filldensity;
		    style = ((fillpar & 0xfff) << 4) + fs->fillstyle;
		    break;
		case FS_PATTERN:
		case FS_TRANSPARENT_PATTERN:
		    fillpar = fs->fillpattern;
		    style = ((fillpar & 0xfff) << 4) + fs->fillstyle;
		    break;
		case FS_EMPTY:
		default: style = FS_EMPTY; break; // solid fill with background color 
	}
	return style;
}
/*
 * Load dt with the properties of a user-defined dashtype.
 * Return: DASHTYPE_SOLID or DASHTYPE_CUSTOM or a positive number
 * if no user-defined dashtype was found.
 */
int load_dashtype(t_dashtype * dt, int tag)
{
	t_dashtype loc_dt = DEFAULT_DASHPATTERN;
	for(custom_dashtype_def * p_this = first_custom_dashtype; p_this;) {
		if(p_this->tag == tag) {
			*dt = p_this->dashtype;
			memcpy(dt->dstring, p_this->dashtype.dstring, sizeof(dt->dstring));
			return p_this->d_type;
		}
		else
			p_this = p_this->next;
	}
	// not found, fall back to default, terminal-dependent dashtype 
	*dt = loc_dt;
	return tag - 1;
}

void lp_use_properties(lp_style_type * lp, int tag)
{
	// This function looks for a linestyle defined by 'tag' and copies its data into the structure 'lp'.
	int save_flags = lp->flags;
	for(linestyle_def * p_this = first_linestyle; p_this;) {
		if(p_this->tag == tag) {
			*lp = p_this->lp_properties;
			lp->flags = save_flags;
			return;
		}
		else
			p_this = p_this->next;
	}
	load_linetype(term, lp, tag); // No user-defined style with p_this tag; fall back to default line type. 
}
// 
// Load lp with the properties of a user-defined linetype
// 
void load_linetype(termentry * pTerm, lp_style_type * lp, int tag)
{
	linestyle_def * p_this;
	bool recycled = false;
recycle:
	if((tag > 0) && (monochrome || (pTerm && (pTerm->flags & TERM_MONOCHROME)))) {
		for(p_this = first_mono_linestyle; p_this; p_this = p_this->next) {
			if(tag == p_this->tag) {
				*lp = p_this->lp_properties;
				return;
			}
		}
		// This linetype wasn't defined explicitly.		
		// Should we recycle one of the first N linetypes?	
		if(tag > mono_recycle_count && mono_recycle_count > 0) {
			tag = (tag-1) % mono_recycle_count + 1;
			goto recycle;
		}
		return;
	}
	p_this = first_perm_linestyle;
	while(p_this != NULL) {
		if(p_this->tag == tag) {
			/* Always load color, width, and dash properties */
			lp->l_type = p_this->lp_properties.l_type;
			lp->l_width = p_this->lp_properties.l_width;
			lp->pm3d_color = p_this->lp_properties.pm3d_color;
			lp->d_type = p_this->lp_properties.d_type;
			lp->custom_dash_pattern = p_this->lp_properties.custom_dash_pattern;
			// Needed in version 5.0 to handle old terminals (pbm hpgl ...) 
			// with no support for user-specified colors 
			if(pTerm && pTerm->set_color == null_set_color)
				lp->l_type = tag;
			// Do not recycle point properties. 
			// FIXME: there should be a separate command "set pointtype cycle N" 
			if(!recycled) {
				lp->p_type = p_this->lp_properties.p_type;
				lp->p_interval = p_this->lp_properties.p_interval;
				lp->p_size = p_this->lp_properties.p_size;
				memcpy(lp->p_char, p_this->lp_properties.p_char, sizeof(lp->p_char));
			}
			return;
		}
		else {
			p_this = p_this->next;
		}
	}
	// This linetype wasn't defined explicitly.		
	// Should we recycle one of the first N linetypes?	
	if(tag > linetype_recycle_count && linetype_recycle_count > 0) {
		tag = (tag-1) % linetype_recycle_count + 1;
		recycled = TRUE;
		goto recycle;
	}
	// No user-defined linetype with p_this tag; fall back to default line type. 
	// NB: We assume that the remaining fields of lp have been initialized. 
	lp->l_type = tag - 1;
	lp->pm3d_color.type = TC_LT;
	lp->pm3d_color.lt = lp->l_type;
	lp->d_type = DASHTYPE_SOLID;
	lp->p_type = (tag <= 0) ? -1 : tag - 1;
}
// 
// Version 5 maintains a parallel set of linetypes for "set monochrome" mode.
// This routine allocates space and initializes the default set.
// 
void init_monochrome()
{
	//lp_style_type mono_default[] = DEFAULT_MONO_LINETYPES;
	lp_style_type mono_default[6];//= DEFAULT_MONO_LINETYPES;
	for(uint i = 0; i < SIZEOFARRAY(mono_default); i++) {
		mono_default[i].SetDefault2();
		mono_default[i].pm3d_color.SetBlack();
	}
	mono_default[1].d_type = 1;
	mono_default[2].d_type = 2;
	mono_default[3].d_type = 3;
	mono_default[4].d_type = 0;
	mono_default[4].l_width = 2.0;
	mono_default[5].d_type = DASHTYPE_CUSTOM;
	mono_default[5].l_width = 1.2;
	mono_default[5].custom_dash_pattern.SetPattern(16.f, 8.0f, 2.0f, 5.0f, 2.0f, 5.0f, 2.0f, 8.0f);
	if(first_mono_linestyle == NULL) {
		int n = sizeof(mono_default) / sizeof(struct lp_style_type);
		// copy default list into active list 
		for(int i = n; i > 0; i--) {
			linestyle_def * p_new = (linestyle_def *)gp_alloc(sizeof(linestyle_def), NULL);
			p_new->next = first_mono_linestyle;
			p_new->lp_properties = mono_default[i-1];
			p_new->tag = i;
			first_mono_linestyle = p_new;
		}
	}
}

/*
 * Totally bogus estimate of TeX string lengths.
 * Basically
 * - don't count anything inside square braces
 * - count regexp \[a-zA-z]* as a single character
 * - ignore characters {}$^_
 */
int strlen_tex(const char * str)
{
	const char * s = str;
	int len = 0;
	if(!strpbrk(s, "{}$[]\\")) {
		len = strlen(s);
		FPRINTF((stderr, "strlen_tex(\"%s\") = %d\n", s, len));
		return len;
	}
	while(*s) {
		switch(*s) {
			case '[':
			    while(*s && *s != ']') s++;
			    if(*s) s++;
			    break;
			case '\\':
			    s++;
			    while(*s && isalpha((uchar)*s)) s++;
			    len++;
			    break;
			case '{':
			case '}':
			case '$':
			case '_':
			case '^':
			    s++;
			    break;
			default:
			    s++;
			    len++;
		}
	}

	FPRINTF((stderr, "strlen_tex(\"%s\") = %d\n", str, len));
	return len;
}

/* The check for asynchronous events such as hotkeys and mouse clicks is
 * normally done in term->waitforinput() while waiting for the next input
 * from the command line.  If input is currently coming from a file or
 * pipe instead, as with a "load" command, then this path would not be
 * triggered automatically and these events would back up until input
 * returned to the command line.  These code paths can explicitly call
 * check_for_mouse_events() so that event processing is handled sooner.
 */
void check_for_mouse_events()
{
#ifdef USE_MOUSE
	if(term_initialised && term->waitforinput) {
		term->waitforinput(TERM_ONLY_CHECK_MOUSING);
	}
#endif
#ifdef _WIN32
	/* Process windows GUI events (e.g. for text window, or wxt and windows terminals) */
	WinMessageLoop();
	/* On Windows, Ctrl-C only sets this flag. */
	/* The next block duplicates the behaviour of inter(). */
	if(ctrlc_flag) {
		ctrlc_flag = FALSE;
		term_reset();
		putc('\n', stderr);
		fprintf(stderr, "Ctrl-C detected!\n");
		bail_to_command_line(); /* return to prompt */
	}
#endif
}

char * escape_reserved_chars(const char * str, const char * reserved)
{
	int i;
	int newsize = strlen(str);
	/* Count number of reserved characters */
	for(i = 0; str[i] != '\0'; i++) {
		if(strchr(reserved, str[i]))
			newsize++;
	}
	char * escaped_str = (char*)gp_alloc(newsize + 1, NULL);
	/* Prefix each reserved character with a backslash */
	for(i = 0, newsize = 0; str[i] != '\0'; i++) {
		if(strchr(reserved, str[i]))
			escaped_str[newsize++] = '\\';
		escaped_str[newsize++] = str[i];
	}
	escaped_str[newsize] = '\0';
	return escaped_str;
}