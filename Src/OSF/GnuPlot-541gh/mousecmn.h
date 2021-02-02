// GNUPLOT - mousecnm.h 
// Copyright: Petr Mikulik <mikulik@physics.muni.cz>, since 1999
//
#ifndef MOUSECMN_H
#define MOUSECMN_H
/*
 * Definitions that are used by both gnuplot core and standalone terminals.
 */
/*
 * Structure for reporting mouse events to the main program
 */
struct gp_event_t {
	int type;       /* see below */
	int mx, my;     /* current mouse coordinates */
	int par1, par2; /* other parameters, depending on the event type */
	int winid;      /* ID of window in which the event occurred */
};

/* event types:
 */
#define GE_EVT_LIST(_) \
	_(GE_motion)        /* mouse has moved */ \
	_(GE_buttonpress)   /* mouse button has been pressed; par1 = number of the button (1, 2, 3...) */ \
	_(GE_buttonrelease) /* mouse button has been released; par1 = number of the button (1, 2, 3...); par2 = time
	                       (ms) since previous button release */ \
	_(GE_keypress)      /* keypress; par1 = keycode (either ASCII, or one of the GP_ enums defined below); par2 = (
	                       |1 .. don't pass through bindings )*/ \
	_(GE_buttonpress_old) /* same as GE_buttonpress but triggered from inactive window */ \
	_(GE_buttonrelease_old) /* same as GE_buttonrelease but triggered from inactive window */ \
	_(GE_keypress_old)  /* same as GE_keypress but triggered from inactive window */ \
	_(GE_modifier)      /* shift/ctrl/alt key pressed or released; par1 = is new mask, see Mod_ enums below */ \
	_(GE_plotdone)      /* acknowledgement of plot completion (for synchronization) */ \
	_(GE_replot)        /* used only by ggi.trm */ \
	_(GE_reset)         /* reset to a well-defined state (e.g.  after an X11 error occured) */ \
	_(GE_fontprops)     /* par1 = hchar par2 = vchar */ \
	_(GE_pending)       /* signal gp_exec_event() to send pending events */ \
	_(GE_raise)         /* raise console window */ \

#define GE_EVT_DEFINE_ENUM(name) name,
enum {
	GE_EVT_LIST(GE_EVT_DEFINE_ENUM)
	GE_EVT_NUM
};

/* the status of the shift, ctrl and alt keys
 * Mod_Opt is used by the "bind" mechanism to indicate that the
 * Ctrl or Alt key is allowed but not required
 */
enum { Mod_Shift = (1), Mod_Ctrl = (1 << 1), Mod_Alt = (1 << 2),
       Mod_Opt = (1 << 3) };

/* the below depends on the ascii character set lying in the
 * range from 0 to 255 (below 1000) */
enum { /* special keys with "usual well-known" keycodes */
	GP_BackSpace = 0x08,
	GP_Tab = 0x09,
	GP_KP_Enter = 0x0A,
	GP_Return = 0x0D,
	GP_Escape = 0x1B,
	GP_Delete = 127
};

enum { /* other special keys */
	GP_FIRST_KEY = 1000,
	GP_Linefeed,
	GP_Clear,
	GP_Pause,
	GP_Scroll_Lock,
	GP_Sys_Req,
	GP_Insert,
	GP_Home,
	GP_Left,
	GP_Up,
	GP_Right,
	GP_Down,
	GP_PageUp,
	GP_PageDown,
	GP_End,
	GP_Begin,
	GP_KP_Space,
	GP_KP_Tab,
	GP_KP_F1,
	GP_KP_F2,
	GP_KP_F3,
	GP_KP_F4,

	GP_KP_Insert, /* ~ KP_0 */
	GP_KP_End,   /* ~ KP_1 */
	GP_KP_Down,  /* ~ KP_2 */
	GP_KP_Page_Down, /* ~ KP_3 */
	GP_KP_Left,  /* ~ KP_4 */
	GP_KP_Begin, /* ~ KP_5 */
	GP_KP_Right, /* ~ KP_6 */
	GP_KP_Home,  /* ~ KP_7 */
	GP_KP_Up,    /* ~ KP_8 */
	GP_KP_Page_Up, /* ~ KP_9 */

	GP_KP_Delete,
	GP_KP_Equal,
	GP_KP_Multiply,
	GP_KP_Add,
	GP_KP_Separator,
	GP_KP_Subtract,
	GP_KP_Decimal,
	GP_KP_Divide,
	GP_KP_0,
	GP_KP_1,
	GP_KP_2,
	GP_KP_3,
	GP_KP_4,
	GP_KP_5,
	GP_KP_6,
	GP_KP_7,
	GP_KP_8,
	GP_KP_9,
	GP_F1,
	GP_F2,
	GP_F3,
	GP_F4,
	GP_F5,
	GP_F6,
	GP_F7,
	GP_F8,
	GP_F9,
	GP_F10,
	GP_F11,
	GP_F12,
	GP_Cancel,
	GP_Button1, /* Buttons must come last */
	GP_Button2,
	GP_Button3,
	GP_LAST_KEY
};

#endif /* MOUSECMN_H */