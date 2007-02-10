#ifndef lint
static char *RCSid() { return RCSid("$Id: mouse.c,v 1.85 2006/07/18 05:20:50 mikulik Exp $"); }
#endif

/* GNUPLOT - mouse.c */

/* driver independent mouse part. */

/*[
 * Permission to use, copy, and distribute this software and its
 * documentation for any purpose with or without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.
 *
 * Permission to modify the software is granted, but not the right to
 * distribute the complete modified source code.  Modifications are to
 * be distributed as patches to the released version.  Permission to
 * distribute binaries produced by compiling modified sources is granted,
 * provided you
 *   1. distribute the corresponding source modifications from the
 *    released version in the form of a patch file along with the binaries,
 *   2. add special version identification to distinguish your version
 *    in addition to the base release version number,
 *   3. provide your name and address as the primary contact for the
 *    support of your modified version, and
 *   4. retain our contact information in regard to use of the base
 *    software.
 * Permission to distribute the released version of the source code along
 * with corresponding source modifications in the form of a patch file is
 * granted with same provisions 2 through 4 for binary distributions.
 *
 * This software is provided "as is" without express or implied warranty
 * to the extent permitted by applicable law.
]*/

/*
 * AUTHORS
 *
 *   Original Software (October 1999 - January 2000):
 *     Pieter-Tjerk de Boer <ptdeboer@cs.utwente.nl>
 *     Petr Mikulik <mikulik@physics.muni.cz>
 *     Johannes Zellner <johannes@zellner.org>
 */

#include "syscfg.h"
#include "stdfn.h"
#include "gp_types.h"

#define _MOUSE_C		/* FIXME HBB 20010207: violates Codestyle */
#ifdef USE_MOUSE		/* comment out whole file, otherwise... */

#include "mouse.h"
#include "pm3d.h"
#include "alloc.h"
#include "axis.h"
#include "command.h"
#include "datafile.h"
#include "gadgets.h"
#include "gp_time.h"
#include "graphics.h"
#include "graph3d.h"
#include "plot3d.h"
#include "readline.h"
#include "term_api.h"
#include "util3d.h"

#ifdef _Windows
# include "win/winmain.h"
#endif

#ifdef OS2
#include "os2/pm_msgs.h"
#endif

/********************** variables ***********************************************************/

mouse_setting_t mouse_setting = {
#ifdef OS2
    0 /* don't start with mouse on default -- clashes with arrow keys on command line */,
#else
    1 /* start with mouse on by default */,
#endif
    300 /* ms */,
    1, 0, 0, 0, 0,
    "% #g",
    "point pt 1"
};

/* "usual well-known" keycodes, i.e. those not listed in special_keys in mouse.h
*/
static const struct gen_table usual_special_keys[] =
{
    { "BackSpace", GP_BackSpace},
    { "Tab", GP_Tab},
    { "KP_Enter", GP_KP_Enter},
    { "Return", GP_Return},
    { "Escape", GP_Escape},
    { "Delete", GP_Delete},
    { NULL, 0}
};

/* the status of the shift, ctrl and alt keys
*/
static int modifier_mask = 0;

/* Structure for the ruler: on/off, position,...
 */
static struct {
    TBOOLEAN on;
    double x, y, x2, y2;	/* ruler position in real units of the graph */
    long px, py;		/* ruler position in the viewport units */
} ruler = {
    FALSE, 0, 0, 0, 0, 0, 0
};


/* the coordinates of the mouse cursor in gnuplot's internal coordinate system
 */
static int mouse_x, mouse_y;


/* the "real" coordinates of the mouse cursor, i.e., in the user's coordinate
 * system(s)
 */
static double real_x, real_y, real_x2, real_y2;


/* mouse_polar_distance is set to TRUE if user wants to see the
 * distance between the ruler and mouse pointer in polar coordinates
 * too (otherwise, distance in cartesian coordinates only is shown) */
/* int mouse_polar_distance = FALSE; */
/* moved to the struct mouse_setting_t (joze) */


/* status of buttons; button i corresponds to bit (1<<i) of this variable
 */
static int button = 0;


/* variables for setting the zoom region:
 */
/* flag, TRUE while user is outlining the zoom region */
static TBOOLEAN setting_zoom_region = FALSE;
/* coordinates of the first corner of the zoom region, in the internal
 * coordinate system */
static int setting_zoom_x, setting_zoom_y;


/* variables for changing the 3D view:
*/
/* do we allow motion to result in a replot right now? */
TBOOLEAN allowmotion = TRUE;	/* used by pm.trm, too */
/* did we already postpone a replot because allowmotion was FALSE ? */
static TBOOLEAN needreplot = FALSE;
/* mouse position when dragging started */
static int start_x, start_y;
/* ButtonPress sets this to 0, ButtonMotion to 1 */
static int motion = 0;
/* values for rot_x and rot_z corresponding to zero position of mouse */
static float zero_rot_x, zero_rot_z;


/* bind related stuff */

typedef struct bind_t {
    struct bind_t *prev;
    int key;
    char modifier;
    char *command;
    char *(*builtin) (struct gp_event_t * ge);
    TBOOLEAN allwindows;
    struct bind_t *next;
} bind_t;

static bind_t *bindings = (bind_t *) 0;
static const int NO_KEY = -1;
static TBOOLEAN trap_release = FALSE;

/* forward declarations */
static void alert __PROTO((void));
static void MousePosToGraphPosReal __PROTO((int xx, int yy, double *x, double *y, double *x2, double *y2));
static char *xy_format __PROTO((void));
static char *zoombox_format __PROTO((void));
static char *xy1_format __PROTO((char *leader));
static char *GetAnnotateString __PROTO((char *s, double x, double y, int mode, char *fmt));
static char *xDateTimeFormat __PROTO((double x, char *b, int mode));
#ifdef OLD_STATUS_LINE
static char *GetCoordinateString __PROTO((char *s, double x, double y));
#endif
static void GetRulerString __PROTO((char *p, double x, double y));
static void apply_zoom __PROTO((struct t_zoom * z));
static void do_zoom __PROTO((double xmin, double ymin, double x2min,
			     double y2min, double xmax, double ymax, double x2max, double y2max));
static void ZoomNext __PROTO((void));
static void ZoomPrevious __PROTO((void));
static void ZoomUnzoom __PROTO((void));
static void incr_mousemode __PROTO((const int amount));
static void incr_clipboardmode __PROTO((const int amount));
static void UpdateStatuslineWithMouseSetting __PROTO((mouse_setting_t * ms));

static void event_keypress __PROTO((struct gp_event_t * ge, TBOOLEAN current));
static void ChangeView __PROTO((int x, int z));
static void event_buttonpress __PROTO((struct gp_event_t * ge));
static void event_buttonrelease __PROTO((struct gp_event_t * ge));
static void event_motion __PROTO((struct gp_event_t * ge));
static void event_modifier __PROTO((struct gp_event_t * ge));
static void do_save_3dplot __PROTO((struct surface_points *, int, int));
static void load_mouse_variables __PROTO((double, double, TBOOLEAN, int));

/* builtins */
static char *builtin_autoscale __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_border __PROTO((struct gp_event_t * ge));
static char *builtin_replot __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_grid __PROTO((struct gp_event_t * ge));
static char *builtin_help __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_log __PROTO((struct gp_event_t * ge));
static char *builtin_nearest_log __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_mouse __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_ruler __PROTO((struct gp_event_t * ge));
static char *builtin_decrement_mousemode __PROTO((struct gp_event_t * ge));
static char *builtin_increment_mousemode __PROTO((struct gp_event_t * ge));
static char *builtin_decrement_clipboardmode __PROTO((struct gp_event_t * ge));
static char *builtin_increment_clipboardmode __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_polardistance __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_verbose __PROTO((struct gp_event_t * ge));
static char *builtin_toggle_ratio __PROTO((struct gp_event_t * ge));
static char *builtin_zoom_next __PROTO((struct gp_event_t * ge));
static char *builtin_zoom_previous __PROTO((struct gp_event_t * ge));
static char *builtin_unzoom __PROTO((struct gp_event_t * ge));
static char *builtin_rotate_right __PROTO((struct gp_event_t * ge));
static char *builtin_rotate_up __PROTO((struct gp_event_t * ge));
static char *builtin_rotate_left __PROTO((struct gp_event_t * ge));
static char *builtin_rotate_down __PROTO((struct gp_event_t * ge));
static char *builtin_cancel_zoom __PROTO((struct gp_event_t * ge));

/* prototypes for bind stuff
 * which are used only here. */
static void bind_install_default_bindings __PROTO((void));
static void bind_clear __PROTO((bind_t * b));
static int lookup_key __PROTO((char *ptr, int *len));
static int bind_scan_lhs __PROTO((bind_t * out, const char *in));
static char *bind_fmt_lhs __PROTO((const bind_t * in));
static int bind_matches __PROTO((const bind_t * a, const bind_t * b));
static void bind_display_one __PROTO((bind_t * ptr));
static void bind_display __PROTO((char *lhs));
static void bind_all __PROTO((char *lhs));
static void bind_remove __PROTO((bind_t * b));
static void bind_append __PROTO((char *lhs, char *rhs, char *(*builtin) (struct gp_event_t * ge)));
static void recalc_ruler_pos __PROTO((void));
static void turn_ruler_off __PROTO((void));
static int nearest_label_tag __PROTO((int x, int y, struct termentry * t));
static void remove_label __PROTO((int x, int y));
static void put_label __PROTO((char *label, double x, double y));

/********* functions ********************************************/

/* produce a beep */
static void
alert()
{
# ifdef GNUPMDRV
    DosBeep(444, 111);
# else
#  ifdef HAVE_LIBREADLINE
    rl_ding();
    fflush(rl_outstream);
#  else
    fprintf(stderr, "\a");
#  endif
# endif
}

/* always include the prototype. The prototype might even not be
 * declared if the system supports stpcpy(). E.g. on Linux I would
 * have to define __USE_GNU before including string.h to get the
 * prototype (joze) */
/* HBB 20001109: *BUT* if a prototype is there, this one may easily
 * conflict with it... */
char *stpcpy __PROTO((char *s, const char *p));

# ifndef HAVE_STPCPY
/* handy function for composing strings; note: some platforms may
 * already provide it, how should we handle that? autoconf? -- ptdb */
char *
stpcpy(char *s, const char *p)
{
    strcpy(s, p);
    return s + strlen(p);
}
# endif


/* a macro to check whether 2D functionality is allowed:
   either the plot is a 2D plot, or it is a suitably oriented 3D plot
*/
# define ALMOST2D      \
    ( !is_3d_plot ||  \
      ( fabs(fmod(surface_rot_z,90.0))<0.1  \
        && (surface_rot_x>179.9 || surface_rot_x<0.1) ) )


/* main job of transformation, which is not device dependent
*/
static void
MousePosToGraphPosReal(int xx, int yy, double *x, double *y, double *x2, double *y2)
{
    if (!is_3d_plot) {
# if 0
	printf("POS: plot_bounds.xleft=%i, plot_bounds.xright=%i, plot_bounds.ybot=%i, plot_bounds.ytop=%i\n", plot_bounds.xleft, plot_bounds.xright, plot_bounds.ybot, plot_bounds.ytop);
# endif
	if (plot_bounds.xright == plot_bounds.xleft)
	    *x = *x2 = 1e38;	/* protection */
	else {
	    *x = AXIS_MAPBACK(FIRST_X_AXIS, xx);
	    *x2 = AXIS_MAPBACK(SECOND_X_AXIS, xx);
	}
	if (plot_bounds.ytop == plot_bounds.ybot)
	    *y = *y2 = 1e38;	/* protection */
	else {
	    *y = AXIS_MAPBACK(FIRST_Y_AXIS, yy);
	    *y2 = AXIS_MAPBACK(SECOND_Y_AXIS, yy);
	}
#if 0
	printf("POS: xx=%i, yy=%i  =>  x=%g  y=%g\n", xx, yy, *x, *y);
#endif
    } else {
	/* for 3D plots, we treat the mouse position as if it is
	 * in the bottom plane, i.e., the plane of the x and y axis */
	/* note: at present, this projection is only correct if
	 * surface_rot_z is a multiple of 90 degrees! */
	/* HBB 20010522: added protection against division by zero
	 * for cases like 'set view 90,0' */
	xx -= axis3d_o_x;
	yy -= axis3d_o_y;
	if (abs(axis3d_x_dx) > abs(axis3d_x_dy)) {
	    *x = axis_array[FIRST_X_AXIS].min
		+ ((double) xx) / axis3d_x_dx * (axis_array[FIRST_X_AXIS].max -
						 axis_array[FIRST_X_AXIS].min);
	} else if (axis3d_x_dy != 0) {
	    *x = axis_array[FIRST_X_AXIS].min
		+ ((double) yy) / axis3d_x_dy * (axis_array[FIRST_X_AXIS].max -
						 axis_array[FIRST_X_AXIS].min);
	} else {
	    /* both diffs are zero (x axis points into the screen */
	    *x = 1e38;
	}

	if (abs(axis3d_y_dx) > abs(axis3d_y_dy)) {
	    *y = axis_array[FIRST_Y_AXIS].min
		+ ((double) xx) / axis3d_y_dx * (axis_array[FIRST_Y_AXIS].max -
						 axis_array[FIRST_Y_AXIS].min);
	} else if (axis3d_y_dy != 0) {
	    *y = axis_array[FIRST_Y_AXIS].min
		+ ((double) yy) / axis3d_y_dy * (axis_array[FIRST_Y_AXIS].max -
						 axis_array[FIRST_Y_AXIS].min);
	} else {
	    /* both diffs are zero (y axis points into the screen */
	    *y = 1e38;
	}

	*x2 = *y2 = 1e38;	/* protection */
    }
    /*
       Note: there is plot_bounds.xleft+0.5 in "#define map_x" in graphics.c, which
       makes no major impact here. It seems that the mistake of the real
       coordinate is at about 0.5%, which corresponds to the screen resolution.
       It would be better to round the distance to this resolution, and thus
       *x = xmin + rounded-to-screen-resolution (xdistance)
     */

    /* Now take into account possible log scales of x and y axes */
    *x = AXIS_DE_LOG_VALUE(FIRST_X_AXIS, *x);
    *y = AXIS_DE_LOG_VALUE(FIRST_Y_AXIS, *y);
    if (!is_3d_plot) {
	*x2 = AXIS_DE_LOG_VALUE(SECOND_X_AXIS, *x2);
	*y2 = AXIS_DE_LOG_VALUE(SECOND_Y_AXIS, *y2);
    }
}

static char *
xy_format()
{
    static char format[0xff];
    format[0] = NUL;
    strcat(format, mouse_setting.fmt);
    strcat(format, ", ");
    strcat(format, mouse_setting.fmt);
    return format;
}

static char *
zoombox_format()
{
    static char format[0xff];
    format[0] = NUL;
    strcat(format, mouse_setting.fmt);
    strcat(format, "\r");
    strcat(format, mouse_setting.fmt);
    return format;
}

static char *
xy1_format(char *leader)
{
    static char format[0xff];
    format[0] = NUL;
    strcat(format, leader);
    strcat(format, mouse_setting.fmt);
    return format;
}

/* formats the information for an annotation (middle mouse button clicked)
 */
static char *
GetAnnotateString(char *s, double x, double y, int mode, char *fmt)
{
    if (mode == MOUSE_COORDINATES_XDATE || mode == MOUSE_COORDINATES_XTIME || mode == MOUSE_COORDINATES_XDATETIME || mode == MOUSE_COORDINATES_TIMEFMT) {	/* time is on the x axis */
	char buf[0xff];
	char format[0xff] = "[%s, ";
	strcat(format, mouse_setting.fmt);
	strcat(format, "]");
	s += sprintf(s, format, xDateTimeFormat(x, buf, mode), y);
    } else if (mode == MOUSE_COORDINATES_FRACTIONAL) {
	double xrange = axis_array[FIRST_X_AXIS].max - axis_array[FIRST_X_AXIS].min;
	double yrange = axis_array[FIRST_Y_AXIS].max - axis_array[FIRST_Y_AXIS].min;
	/* calculate fractional coordinates.
	 * prevent division by zero */
	if (xrange) {
	    char format[0xff] = "/";
	    strcat(format, mouse_setting.fmt);
	    s += sprintf(s, format, (x - axis_array[FIRST_X_AXIS].min) / xrange);
	} else {
	    s += sprintf(s, "/(undefined)");
	}
	if (yrange) {
	    char format[0xff] = ", ";
	    strcat(format, mouse_setting.fmt);
	    strcat(format, "/");
	    s += sprintf(s, format, (y - axis_array[FIRST_Y_AXIS].min) / yrange);
	} else {
	    s += sprintf(s, ", (undefined)/");
	}
    } else if (mode == MOUSE_COORDINATES_REAL1) {
	s += sprintf(s, xy_format(), x, y);	/* w/o brackets */
    } else if (mode == MOUSE_COORDINATES_ALT && fmt) {
	s += sprintf(s, fmt, x, y);	/* user defined format */
    } else {
	s += sprintf(s, xy_format(), x, y);	/* usual x,y values */
    }
    return s;
}


/* Format x according to the date/time mouse mode. Uses and returns b as
   a buffer
 */
static char *
xDateTimeFormat(double x, char *b, int mode)
{
# ifndef SEC_OFFS_SYS
#  define SEC_OFFS_SYS 946684800
# endif
    time_t xtime_position = SEC_OFFS_SYS + x;
    struct tm *pxtime_position = gmtime(&xtime_position);
    switch (mode) {
    case MOUSE_COORDINATES_XDATE:
	sprintf(b, "%d. %d. %04d", pxtime_position->tm_mday, (pxtime_position->tm_mon) + 1,
# if 1
		(pxtime_position->tm_year) + ((pxtime_position->tm_year <= 68) ? 2000 : 1900)
# else
		((pxtime_position->tm_year) < 100) ? (pxtime_position->tm_year) : (pxtime_position->tm_year) - 100
		/*              (pxtime_position->tm_year)+1900 */
# endif
	    );
	break;
    case MOUSE_COORDINATES_XTIME:
	sprintf(b, "%d:%02d", pxtime_position->tm_hour, pxtime_position->tm_min);
	break;
    case MOUSE_COORDINATES_XDATETIME:
	sprintf(b, "%d. %d. %04d %d:%02d", pxtime_position->tm_mday, (pxtime_position->tm_mon) + 1,
# if 1
		(pxtime_position->tm_year) + ((pxtime_position->tm_year <= 68) ? 2000 : 1900),
# else
		((pxtime_position->tm_year) < 100) ? (pxtime_position->tm_year) : (pxtime_position->tm_year) - 100,
		/*              (pxtime_position->tm_year)+1900, */
# endif
		pxtime_position->tm_hour, pxtime_position->tm_min);
	break;
    case MOUSE_COORDINATES_TIMEFMT:
	/* FIXME HBB 20000507: timefmt is for *reading* timedata, not
	 * for writing them! */
	gstrftime(b, 0xff, axis_array[FIRST_X_AXIS].timefmt, x);
	break;
    default:
	sprintf(b, mouse_setting.fmt, x);
    }
    return b;
}



/* HBB 20000507: fixed a construction error. Was using the 'timefmt'
 * string (which is for reading, not writing time data) to output the
 * value. Code is now closer to what setup_tics does. */
#define MKSTR(sp,x,axis)					\
do {								\
    if (axis_array[axis].is_timedata) { 			\
	char *format = copy_or_invent_formatstring(axis);	\
	while (strchr(format,'\n'))				\
	     *(strchr(format,'\n')) = ' ';			\
	sp+=gstrftime(sp, 40, format, x);			\
    } else							\
	sp+=sprintf(sp, mouse_setting.fmt ,x);			\
} while (0)


/* formats the information for an annotation (middle mouse button clicked)
 * returns pointer to the end of the string, for easy concatenation
*/
# ifdef OLD_STATUS_LINE
static char *
GetCoordinateString(char *s, double x, double y)
{
    char *sp;
    s[0] = '[';
    sp = s + 1;
    MKSTR(sp, x, FIRST_X_AXIS);
    *sp++ = ',';
    *sp++ = ' ';
    MKSTR(sp, y, FIRST_Y_AXIS);
    *sp++ = ']';
    *sp = 0;
    return sp;
}
# endif


/* ratio for log, distance for linear */
# define DIST(x,rx,axis)			\
   (axis_array[axis].log)			\
    ? ( (rx==0) ? 99999 : x / rx )		\
    : (x - rx)


/* formats the ruler information (position, distance,...) into string p
	(it must be sufficiently long)
   x, y is the current mouse position in real coords (for the calculation
	of distance)
*/
static void
GetRulerString(char *p, double x, double y)
{
    double dx, dy;

    char format[0xff] = "  ruler: [";
    strcat(format, mouse_setting.fmt);
    strcat(format, ", ");
    strcat(format, mouse_setting.fmt);
    strcat(format, "]  distance: ");
    strcat(format, mouse_setting.fmt);
    strcat(format, ", ");
    strcat(format, mouse_setting.fmt);

    dx = DIST(x, ruler.x, FIRST_X_AXIS);
    dy = DIST(y, ruler.y, FIRST_Y_AXIS);
    sprintf(p, format, ruler.x, ruler.y, dx, dy);

    /* Previously, the following "if" let the polar coordinates to be shown only
       for lin-lin plots:
	    if (mouse_setting.polardistance && !axis_array[FIRST_X_AXIS].log && !axis_array[FIRST_Y_AXIS].log) ...
       Now, let us support also semilog and log-log plots.
       Values of mouse_setting.polardistance are:
	    0 (no polar coordinates), 1 (polar coordinates), 2 (tangent instead of angle).
    */
    if (mouse_setting.polardistance) {
	double rho, phi, rx, ry;
	char ptmp[69];
	x = AXIS_LOG_VALUE(FIRST_X_AXIS, x);
	y = AXIS_LOG_VALUE(FIRST_Y_AXIS, y);
	rx = AXIS_LOG_VALUE(FIRST_X_AXIS, ruler.x);
	ry = AXIS_LOG_VALUE(FIRST_Y_AXIS, ruler.y);
	format[0] = '\0';
	strcat(format, " (");
	strcat(format, mouse_setting.fmt);
	rho = sqrt((x - rx) * (x - rx) + (y - ry) * (y - ry)); /* distance */
	if (mouse_setting.polardistance == 1) { /* (distance, angle) */
	    phi = (180 / M_PI) * atan2(y - ry, x - rx);
# ifdef OS2
	    strcat(format, ";% #.4g�)");
# else
	    strcat(format, ", % #.4gdeg)");
# endif
	} else { /* mouse_setting.polardistance==2: (distance, tangent) */
	    phi = x - rx;
	    phi = (phi == 0) ? ((y-ry>0) ? 1e308:-1e308) : (y - ry)/phi;
	    sprintf(format+strlen(format), ", tangent=%s)", mouse_setting.fmt);
	}
	sprintf(ptmp, format, rho, phi);
	strcat(p, ptmp);
    }
}


static struct t_zoom *zoom_head = NULL, *zoom_now = NULL;
/* Applies the zoom rectangle of  z  by sending the appropriate command
   to gnuplot
*/

static void
apply_zoom(struct t_zoom *z)
{
    char s[1024];		/* HBB 20011005: made larger */
    /* HBB 20011004: new variable, fixing 'unzoom' back to autoscaled range */
    static t_autoscale autoscale_copies[AXIS_ARRAY_SIZE];
    int is_splot_map = (is_3d_plot && (splot_map == TRUE));
    int flip = 0;

    if (zoom_now != NULL) {	/* remember the current zoom */
	zoom_now->xmin = axis_array[FIRST_X_AXIS].set_min;
	zoom_now->xmax = axis_array[FIRST_X_AXIS].set_max;
	zoom_now->x2min = axis_array[SECOND_X_AXIS].set_min;
	zoom_now->x2max = axis_array[SECOND_X_AXIS].set_max;
	zoom_now->was_splot_map = is_splot_map;
	if (!is_splot_map) { /* 2D plot */
	    zoom_now->ymin = axis_array[FIRST_Y_AXIS].set_min;
	    zoom_now->ymax = axis_array[FIRST_Y_AXIS].set_max;
	    zoom_now->y2min = axis_array[SECOND_Y_AXIS].set_min;
	    zoom_now->y2max = axis_array[SECOND_Y_AXIS].set_max;
	} else { /* the opposite, i.e. case 'set view map' */
	    zoom_now->ymin = axis_array[FIRST_Y_AXIS].set_max;
	    zoom_now->ymax = axis_array[FIRST_Y_AXIS].set_min;
	    zoom_now->y2min = axis_array[SECOND_Y_AXIS].set_max;
	    zoom_now->y2max = axis_array[SECOND_Y_AXIS].set_min;
	}

    }

    /* HBB 20011004: implement save/restore of autoscaling if returning to
     * original setting */
    if (zoom_now == zoom_head) {
	/* previous current zoom is the head --> we're leaving
	 * original non-moused status --> save autoscales */
	AXIS_INDEX i;
	for (i = 0; i < AXIS_ARRAY_SIZE; i++)
	    autoscale_copies[i] = axis_array[i].set_autoscale;
    }

    zoom_now = z;
    if (zoom_now == NULL) {
	alert();
	return;
    }

    flip = (is_splot_map && zoom_now->was_splot_map);
#ifdef HAVE_LOCALE_H
    if (strcmp(localeconv()->decimal_point,".")) {
	char *save_locale = gp_strdup(setlocale(LC_NUMERIC,NULL));
	setlocale(LC_NUMERIC,"C");
	sprintf(s, "set xr[%.12g:%.12g]; set yr[%.12g:%.12g]",
	       zoom_now->xmin, zoom_now->xmax, 
	       (flip) ? zoom_now->ymax : zoom_now->ymin,
	       (flip) ? zoom_now->ymin : zoom_now->ymax);
	setlocale(LC_NUMERIC,save_locale);
	free(save_locale);
    } else
#endif
	sprintf(s, "set xr[%.12g:%.12g]; set yr[%.12g:%.12g]",
	       zoom_now->xmin, zoom_now->xmax, 
	       (flip) ? zoom_now->ymax : zoom_now->ymin,
	       (flip) ? zoom_now->ymin : zoom_now->ymax);

    /* HBB 20011004: the final part of 'unzoom to autoscale mode'.
     * Optionally appends 'set autoscale' commands to the string to be
     * sent, to restore the saved settings. */
#define OUTPUT_AUTOSCALE(axis)						     \
    {					     \
	t_autoscale autoscale = autoscale_copies[axis];			     \
	t_autoscale fix = autoscale & (AUTOSCALE_FIXMIN | AUTOSCALE_FIXMAX); \
									     \
	autoscale &= AUTOSCALE_BOTH;					     \
									     \
	if (autoscale) {						     \
	    sprintf(s + strlen(s), "; set au %s%s",			     \
		    axis_defaults[axis].name,				     \
		    autoscale == AUTOSCALE_MIN ? "min"			     \
		    : autoscale == AUTOSCALE_MAX ? "max"		     \
		    : "");						     \
	    if (fix)							     \
		sprintf(s + strlen(s), "; set au %sfix%s",		     \
			axis_defaults[axis].name,			     \
			fix == AUTOSCALE_FIXMIN ? "min"			     \
			: fix == AUTOSCALE_FIXMAX ? "max"		     \
			: "");						     \
	}								     \
    }

    if (zoom_now == zoom_head) {
	/* new current zoom is the head --> returning to unzoomed
	 * settings --> restore autoscale */
	OUTPUT_AUTOSCALE(FIRST_X_AXIS);
	OUTPUT_AUTOSCALE(FIRST_Y_AXIS);
    }

    if (!is_3d_plot) {
#ifdef HAVE_LOCALE_H
	if (strcmp(localeconv()->decimal_point,".")) {
	    char *save_locale = gp_strdup(setlocale(LC_NUMERIC,NULL));
	    setlocale(LC_NUMERIC,"C");
	    sprintf(s + strlen(s), "; set x2r[% #g:% #g]; set y2r[% #g:% #g]",
		zoom_now->x2min, zoom_now->x2max,
		zoom_now->y2min, zoom_now->y2max);
	    setlocale(LC_NUMERIC,save_locale);
	    free(save_locale);
	} else
#endif
	    sprintf(s + strlen(s), "; set x2r[% #g:% #g]; set y2r[% #g:% #g]",
		zoom_now->x2min, zoom_now->x2max,
		zoom_now->y2min, zoom_now->y2max);
	if (zoom_now == zoom_head) {
	    /* new current zoom is the head --> returning to unzoomed
	     * settings --> restore autoscale */
	    OUTPUT_AUTOSCALE(SECOND_X_AXIS);
	    OUTPUT_AUTOSCALE(SECOND_Y_AXIS);
	}
#undef OUTPUT_AUTOSCALE
    }
    do_string_replot(s);
}


/* makes a zoom: update zoom history, call gnuplot to set ranges + replot
*/

static void
do_zoom(double xmin, double ymin, double x2min, double y2min, double xmax, double ymax, double x2max, double y2max)
{
    struct t_zoom *z;
    if (zoom_head == NULL) {	/* queue not yet created, thus make its head */
	zoom_head = gp_alloc(sizeof(struct t_zoom), "mouse zoom history head");
	zoom_head->prev = NULL;
	zoom_head->next = NULL;
    }
    if (zoom_now == NULL)
	zoom_now = zoom_head;
    if (zoom_now->next == NULL) {	/* allocate new item */
	z = gp_alloc(sizeof(struct t_zoom), "mouse zoom history element");
	z->prev = zoom_now;
	z->next = NULL;
	zoom_now->next = z;
	z->prev = zoom_now;
    } else			/* overwrite next item */
	z = zoom_now->next;
    z->xmin = xmin;
    z->ymin = ymin;
    z->x2min = x2min;
    z->y2min = y2min;
    z->xmax = xmax;
    z->ymax = ymax;
    z->x2max = x2max;
    z->y2max = y2max;
    z->was_splot_map = (is_3d_plot && (splot_map == TRUE)); /* see is_splot_map in apply_zoom() */ 
    apply_zoom(z);
}


static void
ZoomNext()
{
    if (zoom_now == NULL || zoom_now->next == NULL)
	alert();
    else
	apply_zoom(zoom_now->next);
    if (display_ipc_commands()) {
	fprintf(stderr, "next zoom.\n");
    }
}

static void
ZoomPrevious()
{
    if (zoom_now == NULL || zoom_now->prev == NULL)
	alert();
    else
	apply_zoom(zoom_now->prev);
    if (display_ipc_commands()) {
	fprintf(stderr, "previous zoom.\n");
    }
}

static void
ZoomUnzoom()
{
    if (zoom_head == NULL || zoom_now == zoom_head)
	alert();
    else
	apply_zoom(zoom_head);
    if (display_ipc_commands()) {
	fprintf(stderr, "unzoom.\n");
    }
}

static void
incr_mousemode(const int amount)
{
    long int old = mouse_mode;
    mouse_mode += amount;
    if (MOUSE_COORDINATES_ALT == mouse_mode && !mouse_alt_string) {
	mouse_mode += amount;	/* stepping over */
    }
    if (mouse_mode > MOUSE_COORDINATES_ALT) {
	mouse_mode = MOUSE_COORDINATES_REAL;
    } else if (mouse_mode < MOUSE_COORDINATES_REAL) {
	mouse_mode = MOUSE_COORDINATES_ALT;
	if (MOUSE_COORDINATES_ALT == mouse_mode && !mouse_alt_string) {
	    mouse_mode--;	/* stepping over */
	}
    }
    UpdateStatusline();
    if (display_ipc_commands()) {
	fprintf(stderr, "switched mouse format from %ld to %ld\n", old, mouse_mode);
    }
}

static void
incr_clipboardmode(const int amount)
{
    long int old = clipboard_mode;
    clipboard_mode += amount;
    if (MOUSE_COORDINATES_ALT == clipboard_mode && !clipboard_alt_string) {
	clipboard_mode += amount;	/* stepping over */
    }
    if (clipboard_mode > MOUSE_COORDINATES_ALT) {
	clipboard_mode = MOUSE_COORDINATES_REAL;
    } else if (clipboard_mode < MOUSE_COORDINATES_REAL) {
	clipboard_mode = MOUSE_COORDINATES_ALT;
	if (MOUSE_COORDINATES_ALT == clipboard_mode && !clipboard_alt_string) {
	    clipboard_mode--;	/* stepping over */
	}
    }
    if (display_ipc_commands()) {
	fprintf(stderr, "switched clipboard format from %ld to %ld\n", old, clipboard_mode);
    }
}

# define TICS_ON(ti) (((ti)&TICS_MASK)!=NO_TICS)

void
UpdateStatusline()
{
    UpdateStatuslineWithMouseSetting(&mouse_setting);
}

static void
UpdateStatuslineWithMouseSetting(mouse_setting_t * ms)
{
    char s0[256], *sp;
    if (!term_initialised)
	return;
    if (!ms->on) {
	s0[0] = 0;
    } else if (!ALMOST2D) {
	char format[0xff];
	format[0] = '\0';
	strcat(format, "view: ");
	strcat(format, ms->fmt);
	strcat(format, ", ");
	strcat(format, ms->fmt);
	strcat(format, "   scale: ");
	strcat(format, ms->fmt);
	strcat(format, ", ");
	strcat(format, ms->fmt);
	sprintf(s0, format, surface_rot_x, surface_rot_z, surface_scale, surface_zscale);
    } else if (!TICS_ON(axis_array[SECOND_X_AXIS].ticmode) && !TICS_ON(axis_array[SECOND_Y_AXIS].ticmode)) {
	/* only first X and Y axis are in use */
# ifdef OLD_STATUS_LINE
	sp = GetCoordinateString(s0, real_x, real_y);
# else
	sp = GetAnnotateString(s0, real_x, real_y, mouse_mode, mouse_alt_string);
# endif
	if (ruler.on) {
# if 0
	    MousePosToGraphPosReal(ruler.px, ruler.py, &ruler.x, &ruler.y, &ruler.x2, &ruler.y2);
# endif
	    GetRulerString(sp, real_x, real_y);
	}
    } else {
	/* X2 and/or Y2 are in use: use more verbose format */
	sp = s0;
	if (TICS_ON(axis_array[FIRST_X_AXIS].ticmode)) {
	    sp = stpcpy(sp, "x=");
	    MKSTR(sp, real_x, FIRST_X_AXIS);
	    *sp++ = ' ';
	}
	if (TICS_ON(axis_array[FIRST_Y_AXIS].ticmode)) {
	    sp = stpcpy(sp, "y=");
	    MKSTR(sp, real_y, FIRST_Y_AXIS);
	    *sp++ = ' ';
	}
	if (TICS_ON(axis_array[SECOND_X_AXIS].ticmode)) {
	    sp = stpcpy(sp, "x2=");
	    MKSTR(sp, real_x2, SECOND_X_AXIS);
	    *sp++ = ' ';
	}
	if (TICS_ON(axis_array[SECOND_Y_AXIS].ticmode)) {
	    sp = stpcpy(sp, "y2=");
	    MKSTR(sp, real_y2, SECOND_Y_AXIS);
	    *sp++ = ' ';
	}
	if (ruler.on) {
	    /* ruler on? then also print distances to ruler */
# if 0
	    MousePosToGraphPosReal(ruler.px, ruler.py, &ruler.x, &ruler.y, &ruler.x2, &ruler.y2);
# endif
	    if (TICS_ON(axis_array[FIRST_X_AXIS].ticmode))
		sp += sprintf(sp, xy1_format("dx="), DIST(real_x, ruler.x, FIRST_X_AXIS));
	    if (TICS_ON(axis_array[FIRST_Y_AXIS].ticmode))
		sp += sprintf(sp, xy1_format("dy="), DIST(real_y, ruler.y, FIRST_Y_AXIS));
	    if (TICS_ON(axis_array[SECOND_X_AXIS].ticmode))
		sp += sprintf(sp, xy1_format("dx2="), DIST(real_x2, ruler.x2, SECOND_X_AXIS));
	    if (TICS_ON(axis_array[SECOND_Y_AXIS].ticmode))
		sp += sprintf(sp, xy1_format("dy2="), DIST(real_y2, ruler.y2, SECOND_Y_AXIS));
	}
	*--sp = 0;		/* delete trailing space */
    }
    if (term->put_tmptext) {
	(term->put_tmptext) (0, s0);
    }
}
#undef MKSTR


void
recalc_statusline()
{
    MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);
    UpdateStatusline();
}

/****************** handlers for user's actions ******************/

static char *
builtin_autoscale(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-autoscale` (set autoscale keepfix; replot)";
    }
    do_string_replot("set autoscale keepfix");
    return (char *) 0;
}

static char *
builtin_toggle_border(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-border`";
    }
    if (is_3d_plot) {
	if (draw_border == 4095)
	    do_string_replot("set border");
	else
	    do_string_replot("set border 4095 lw 0.5");
    } else {
	if (draw_border == 15)
	    do_string_replot("set border");
	else
	    do_string_replot("set border 15 lw 0.5");
    }
    return (char *) 0;
}

static char *
builtin_replot(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-replot`";
    }
    do_string_replot("");
    return (char *) 0;
}

static char *
builtin_toggle_grid(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-grid`";
    }
    if (! some_grid_selected())
	do_string_replot("set grid");
    else
	do_string_replot("unset grid");
    return (char *) 0;
}

static char *
builtin_help(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-help`";
    }
    fprintf(stderr, "\n");
    bind_display((char *) 0);	/* display all bindings */
    restore_prompt();
    return (char *) 0;
}

static char *
builtin_toggle_log(struct gp_event_t *ge)
{
    if (!ge)
	return "`builtin-toggle-log` y logscale for plots, z and cb logscale for splots";
    if (is_3d_plot) {
	if (Z_AXIS.log || CB_AXIS.log)
	    do_string_replot("unset log zcb");
	else
	    do_string_replot("set log zcb");
    } else {
#ifdef WITH_IMAGE
	/* set log cb or log y whether using "with (rgb)image" plot or not */
	if (is_cb_plot) {
	    if (CB_AXIS.log)
		do_string_replot("unset log cb");
	    else
		do_string_replot("set log cb");
	} else {
#endif
	if (axis_array[FIRST_Y_AXIS].log)
	    do_string_replot("unset log y");
	else
	    do_string_replot("set log y");
#ifdef WITH_IMAGE
	}
#endif
    }
    return (char *) 0;
}

static char *
builtin_nearest_log(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-nearest-log` toggle logscale of axis nearest cursor";
    }
    if (is_3d_plot) {
	/* 3D-plot: toggle lin/log z axis */
	if (Z_AXIS.log || CB_AXIS.log)
	    do_string_replot("unset log zcb");
	else
	    do_string_replot("set log zcb");
    } else {
	/* 2D-plot: figure out which axis/axes is/are
	 * close to the mouse cursor, and toggle those lin/log */
	/* note: here it is assumed that the x axis is at
	 * the bottom, x2 at top, y left and y2 right; it
	 * would be better to derive that from the ..tics settings */
	TBOOLEAN change = FALSE;
	if (mouse_y < plot_bounds.ybot + (plot_bounds.ytop - plot_bounds.ybot) / 4 && mouse_x > plot_bounds.xleft && mouse_x < plot_bounds.xright) {
	    do_string(axis_array[FIRST_X_AXIS].log ? "unset log x" : "set log x");
	    change = TRUE;
	}
	if (mouse_y > plot_bounds.ytop - (plot_bounds.ytop - plot_bounds.ybot) / 4 && mouse_x > plot_bounds.xleft && mouse_x < plot_bounds.xright) {
	    do_string(axis_array[SECOND_X_AXIS].log ? "unset log x2" : "set log x2");
	    change = TRUE;
	}
	if (mouse_x < plot_bounds.xleft + (plot_bounds.xright - plot_bounds.xleft) / 4 && mouse_y > plot_bounds.ybot && mouse_y < plot_bounds.ytop) {
	    do_string(axis_array[FIRST_Y_AXIS].log ? "unset log y" : "set log y");
	    change = TRUE;
	}
	if (mouse_x > plot_bounds.xright - (plot_bounds.xright - plot_bounds.xleft) / 4 && mouse_y > plot_bounds.ybot && mouse_y < plot_bounds.ytop) {
	    do_string(axis_array[SECOND_Y_AXIS].log ? "unset log y2" : "set log y2");
	    change = TRUE;
	}
	if (change)
	    do_string_replot("");
    }
    return (char *) 0;
}

static char *
builtin_toggle_mouse(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-mouse`";
    }
    if (!mouse_setting.on) {
	mouse_setting.on = 1;
	if (display_ipc_commands()) {
	    fprintf(stderr, "turning mouse on.\n");
	}
    } else {
	mouse_setting.on = 0;
	if (display_ipc_commands()) {
	    fprintf(stderr, "turning mouse off.\n");
	}
# if 0
	if (ruler.on) {
	    ruler.on = FALSE;
	    (*term->set_ruler) (-1, -1);
	}
# endif
    }
# ifdef OS2
    PM_update_menu_items();
# endif
    UpdateStatusline();
    return (char *) 0;
}

static char *
builtin_toggle_ruler(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-ruler`";
    }
    if (!term->set_ruler)
	return (char *) 0;
    if (ruler.on) {
	turn_ruler_off();
	if (display_ipc_commands())
	    fprintf(stderr, "turning ruler off.\n");
    } else if (ALMOST2D) {
	/* only allow ruler, if the plot
	 * is 2d or a 3d `map' */
	struct udvt_entry *u;
	ruler.on = TRUE;
	ruler.px = ge->mx;
	ruler.py = ge->my;
	MousePosToGraphPosReal(ruler.px, ruler.py, &ruler.x, &ruler.y, &ruler.x2, &ruler.y2);
	(*term->set_ruler) (ruler.px, ruler.py);
	if ((u = add_udv_by_name("MOUSE_RULER_X"))) {
	    u->udv_undef = FALSE;
	    Gcomplex(&u->udv_value,ruler.x,0);
	}
	if ((u = add_udv_by_name("MOUSE_RULER_Y"))) {
	    u->udv_undef = FALSE;
	    Gcomplex(&u->udv_value,ruler.y,0);
	}
	if (display_ipc_commands()) {
	    fprintf(stderr, "turning ruler on.\n");
	}
    }
    UpdateStatusline();
    return (char *) 0;
}

static char *
builtin_decrement_mousemode(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-decrement-mousemode`";
    }
    incr_mousemode(-1);
    return (char *) 0;
}

static char *
builtin_increment_mousemode(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-increment-mousemode`";
    }
    incr_mousemode(1);
    return (char *) 0;
}

static char *
builtin_decrement_clipboardmode(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-decrement-clipboardmode`";
    }
    incr_clipboardmode(-1);
    return (char *) 0;
}

static char *
builtin_increment_clipboardmode(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-increment-clipboardmode`";
    }
    incr_clipboardmode(1);
    return (char *) 0;
}

static char *
builtin_toggle_polardistance(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-polardistance`";
    }
    if (++mouse_setting.polardistance > 2) mouse_setting.polardistance = 0;
	/* values: 0 (no polar coordinates), 1 (polar coordinates), 2 (tangent instead of angle) */
    term->set_cursor((mouse_setting.polardistance ? -3:-4), ge->mx, ge->my); /* change cursor type */
# ifdef OS2
    PM_update_menu_items();
# endif
    UpdateStatusline();
    if (display_ipc_commands()) {
	fprintf(stderr, "distance to ruler will %s be shown in polar coordinates.\n", mouse_setting.polardistance ? "" : "not");
    }
    return (char *) 0;
}

static char *
builtin_toggle_verbose(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-verbose`";
    }
    /* this is tricky as the command itself modifies
     * the state of display_ipc_commands() */
    if (display_ipc_commands()) {
	fprintf(stderr, "echoing of communication commands is turned off.\n");
    }
    toggle_display_of_ipc_commands();
    if (display_ipc_commands()) {
	fprintf(stderr, "communication commands will be echoed.\n");
    }
    return (char *) 0;
}

static char *
builtin_toggle_ratio(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-toggle-ratio`";
    }
    if (aspect_ratio == 0)
	do_string_replot("set size ratio -1");
    else if (aspect_ratio == 1)
	do_string_replot("set size nosquare");
    else
	do_string_replot("set size square");
    return (char *) 0;
}

static char *
builtin_zoom_next(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-zoom-next` go to next zoom in the zoom stack";
    }
    ZoomNext();
    return (char *) 0;
}

static char *
builtin_zoom_previous(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-zoom-previous` go to previous zoom in the zoom stack";
    }
    ZoomPrevious();
    return (char *) 0;
}

static char *
builtin_unzoom(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-unzoom`";
    }
    ZoomUnzoom();
    return (char *) 0;
}

static char *
builtin_rotate_right(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-rotate-right` only for splots; <shift> increases amount";
    }
    if (is_3d_plot)
	ChangeView(0, -1);
    return (char *) 0;
}

static char *
builtin_rotate_left(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-rotate-left` only for splots; <shift> increases amount";
    }
    if (is_3d_plot)
	ChangeView(0, 1);
    return (char *) 0;
}

static char *
builtin_rotate_up(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-rotate-up` only for splots; <shift> increases amount";
    }
    if (is_3d_plot)
	ChangeView(1, 0);
    return (char *) 0;
}

static char *
builtin_rotate_down(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-rotate-down` only for splots; <shift> increases amount";
    }
    if (is_3d_plot)
	ChangeView(-1, 0);
    return (char *) 0;
}

static char *
builtin_cancel_zoom(struct gp_event_t *ge)
{
    if (!ge) {
	return "`builtin-cancel-zoom` cancel zoom region";
    }
    if (!setting_zoom_region)
	return (char *) 0;
    if (term->set_cursor)
	term->set_cursor(0, 0, 0);
    setting_zoom_region = FALSE;
    if (display_ipc_commands()) {
	fprintf(stderr, "zooming cancelled.\n");
    }
    return (char *) 0;
}

static void
event_keypress(struct gp_event_t *ge, TBOOLEAN current)
{
    int x, y;
    int c, par2;
    bind_t *ptr;
    bind_t keypress;

    c = ge->par1;
    par2 = ge->par2;
    x = ge->mx;
    y = ge->my;

    if (!bindings) {
	bind_install_default_bindings();
    }

    if (modifier_mask & Mod_Shift) {
	c = toupper(c);
    }

    bind_clear(&keypress);
    keypress.key = c;
    keypress.modifier = modifier_mask;

    /*
     * On 'pause mouse keypress' in active window export current keypress 
     * and mouse coords to user variables. A key with 'bind all' terminates 
     * a pause even from non-active windows.
     * Ignore NULL keypress.
     *
     * If we are paused for a keystroke, this takes precendence over normal
     * key bindings. Otherwise, for example typing 'm' would turn off mousing,
     * which is a bad thing if you are in the  middle of a mousing operation.
     */

#ifdef _Windows
    if (paused_for_mouse & PAUSE_KEYSTROKE)
	kill_pending_Pause_dialog();
#endif
    
    if ((paused_for_mouse & PAUSE_KEYSTROKE) && (c > '\0') && current) {
	load_mouse_variables(x, y, FALSE, c);
	return;
    }

    for (ptr = bindings; ptr; ptr = ptr->next) {
	if (bind_matches(&keypress, ptr)) {
	    struct udvt_entry *keywin;
	    if ((keywin = add_udv_by_name("MOUSE_KEY_WINDOW"))) {
		keywin->udv_undef = FALSE;
		Ginteger(&keywin->udv_value, ge->winid);
	    }
	    /* Always honor keys set with "bind all" */
	    if (ptr->allwindows && ptr->command) {
		if (current)
		    load_mouse_variables(x, y, FALSE, c);
		else
		    /* FIXME - Better to clear MOUSE_[XY] than to set it wrongly. */
		    /*         This may be worth a separate subroutine.           */
		    load_mouse_variables(0, 0, FALSE, c);
		do_string(ptr->command);
		/* Treat as a current event after we return to x11.trm */
		ge->type = GE_keypress;
		break;
	    /* But otherwise ignore inactive windows */
	    } else if (!current) {
		break;
	    /* Let user defined bindings overwrite the builtin bindings */
	    } else if ((par2 & 1) == 0 && ptr->command) {
		do_string(ptr->command);
		break;
	    } else if (ptr->builtin) {
		ptr->builtin(ge);
	    } else {
		fprintf(stderr, "%s:%d protocol error\n", __FILE__, __LINE__);
	    }
	}
    }

}


static void
ChangeView(int x, int z)
{
    if (modifier_mask & Mod_Shift) {
	x *= 10;
	z *= 10;
    }

    if (x) {
	surface_rot_x += x;
	if (surface_rot_x < 0)
	    surface_rot_x = 0;
	if (surface_rot_x > 180)
	    surface_rot_x = 180;
    }
    if (z) {
	surface_rot_z += z;
	if (surface_rot_z < 0)
	    surface_rot_z += 360;
	if (surface_rot_z > 360)
	    surface_rot_z -= 360;
    }

    if (display_ipc_commands()) {
	fprintf(stderr, "changing view to %f, %f.\n", surface_rot_x, surface_rot_z);
    }

    do_save_3dplot(first_3dplot, plot3d_num, 0 /* not quick */ );

    if (ALMOST2D) {
	/* 2D plot, or suitably aligned 3D plot: update statusline */
	if (!term->put_tmptext)
	    return;
	recalc_statusline();
    }
}


static void
event_buttonpress(struct gp_event_t *ge)
{
    int b;

    motion = 0;

    b = ge->par1;
    mouse_x = ge->mx;
    mouse_y = ge->my;

    button |= (1 << b);

    FPRINTF((stderr, "(event_buttonpress) mouse_x = %d\tmouse_y = %d\n", mouse_x, mouse_y));

    MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);

    if (ALMOST2D) {
	if (!setting_zoom_region) {
	    if (1 == b) {
		/* not bound in 2d graphs */
	    } else if (2 == b) {
		/* not bound in 2d graphs */
	    } else if (3 == b && !replot_disabled && !(paused_for_mouse & PAUSE_CLICK)) {
		/* start zoom; but ignore it when
		 *   - replot is disabled, e.g. with inline data, or
		 *   - during 'pause mouse'
		 * allow zooming during 'pause mouse key' */
		setting_zoom_x = mouse_x;
		setting_zoom_y = mouse_y;
		setting_zoom_region = TRUE;
		if (term->set_cursor) {
		    int mv_mouse_x, mv_mouse_y;
		    if (mouse_setting.annotate_zoom_box && term->put_tmptext) {
			double real_x, real_y, real_x2, real_y2;
			char s[64];
			/* tell driver annotations */
			MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);
			sprintf(s, zoombox_format(), real_x, real_y);
			term->put_tmptext(1, s);
			term->put_tmptext(2, s);
		    }
		    /* displace mouse in order not to start with an empty zoom box */
		    mv_mouse_x = term->xmax / 20;
		    mv_mouse_y = (term->xmax == term->ymax) ? mv_mouse_x : (int) ((mv_mouse_x * (double) term->ymax) / term->xmax);
		    mv_mouse_x += mouse_x;
		    mv_mouse_y += mouse_y;

		    /* change cursor type */
		    term->set_cursor(3, 0, 0);

		    /* warp pointer */
		    if (mouse_setting.warp_pointer)
			term->set_cursor(-2, mv_mouse_x, mv_mouse_y);

		    /* turn on the zoom box */
		    term->set_cursor(-1, setting_zoom_x, setting_zoom_y);
		}
		if (display_ipc_commands()) {
		    fprintf(stderr, "starting zoom region.\n");
		}
	    }
	} else {

	    /* complete zoom (any button
	     * finishes zooming.) */

	    /* the following variables are used to check,
	     * if the box is big enough to be considered
	     * as zoom box. */
	    int dist_x = setting_zoom_x - mouse_x;
	    int dist_y = setting_zoom_y - mouse_y;
	    int dist = sqrt((double)(dist_x * dist_x + dist_y * dist_y));

	    if (1 == b || 2 == b) {
		/* zoom region is finished by the `wrong' button.
		 * `trap' the next button-release event so that
		 * it won't trigger the actions which are bound
		 * to these events.
		 */
		trap_release = TRUE;
	    }

	    if (term->set_cursor) {
		term->set_cursor(0, 0, 0);
		if (mouse_setting.annotate_zoom_box && term->put_tmptext) {
		    term->put_tmptext(1, "");
		    term->put_tmptext(2, "");
		}
	    }

	    if (dist > 10 /* more ore less arbitrary */ ) {

		double xmin, ymin, x2min, y2min;
		double xmax, ymax, x2max, y2max;

		MousePosToGraphPosReal(setting_zoom_x, setting_zoom_y, &xmin, &ymin, &x2min, &y2min);
		xmax = real_x;
		x2max = real_x2;
		ymax = real_y;
		y2max = real_y2;
		/* keep the axes (no)reversed as they are now */
#define sgn(x) (x==0 ? 0 : (x>0 ? 1 : -1))
#define rev(a1,a2,A) if (sgn(a2-a1) != sgn(axis_array[A].max-axis_array[A].min)) \
			    { double tmp = a1; a1 = a2; a2 = tmp; }
		rev(xmin,  xmax,  FIRST_X_AXIS);
		rev(ymin,  ymax,  FIRST_Y_AXIS);
		rev(x2min, x2max, SECOND_X_AXIS);
		rev(y2min, y2max, SECOND_Y_AXIS);
#undef rev
#undef sgn
		do_zoom(xmin, ymin, x2min, y2min, xmax, ymax, x2max, y2max);
		if (display_ipc_commands()) {
		    fprintf(stderr, "zoom region finished.\n");
		}
	    } else {
		/* silently ignore a tiny zoom box. This might
		 * happen, if the user starts and finishes the
		 * zoom box at the same position. */
	    }
	    setting_zoom_region = FALSE;
	}
    } else {
	if (term->set_cursor) {
	    if (button & (1 << 1))
		term->set_cursor(1, 0, 0);
	    else if (button & (1 << 2))
		term->set_cursor(2, 0, 0);
	}
    }
    start_x = mouse_x;
    start_y = mouse_y;
    zero_rot_z = surface_rot_z + 360.0 * mouse_x / term->xmax;
    zero_rot_x = surface_rot_x - 180.0 * mouse_y / term->ymax;
}


static void
event_buttonrelease(struct gp_event_t *ge)
{
    int b, doubleclick;

    b = ge->par1;
    mouse_x = ge->mx;
    mouse_y = ge->my;
    doubleclick = ge->par2;

    button &= ~(1 << b);	/* remove button */

    if (setting_zoom_region) {
	return;
    }
    if (TRUE == trap_release) {
	trap_release = FALSE;
	return;
    }

    MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);

    FPRINTF((stderr, "MOUSE.C: doublclick=%i, set=%i, motion=%i, ALMOST2D=%i\n", (int) doubleclick, (int) mouse_setting.doubleclick,
	     (int) motion, (int) ALMOST2D));

    if (ALMOST2D) {
	char s0[256];
	if (b == 1 && term->set_clipboard && ((doubleclick <= mouse_setting.doubleclick)
					      || !mouse_setting.doubleclick)) {

	    /* put coordinates to clipboard. For 3d plots this takes
	     * only place, if the user didn't drag (rotate) the plot */

	    if (!is_3d_plot || !motion) {
		GetAnnotateString(s0, real_x, real_y, clipboard_mode, clipboard_alt_string);
		term->set_clipboard(s0);
		if (display_ipc_commands()) {
		    fprintf(stderr, "put `%s' to clipboard.\n", s0);
		}
	    }
	}
	if (b == 2) {

	    /* draw temporary annotation or label. For 3d plots this takes
	     * only place, if the user didn't drag (scale) the plot */

	    if (!is_3d_plot || !motion) {

		GetAnnotateString(s0, real_x, real_y, mouse_mode, mouse_alt_string);
		if (mouse_setting.label) {
		    if (modifier_mask & Mod_Ctrl) {
			remove_label(mouse_x, mouse_y);
		    } else {
			put_label(s0, real_x, real_y);
		    }
		} else {
		    int dx, dy;
		    int x = mouse_x;
		    int y = mouse_y;
		    dx = term->h_tic;
		    dy = term->v_tic;
		    (term->linewidth) (border_lp.l_width);
		    (term->linetype) (border_lp.l_type);
		    (term->move) (x - dx, y);
		    (term->vector) (x + dx, y);
		    (term->move) (x, y - dy);
		    (term->vector) (x, y + dy);
		    (term->justify_text) (LEFT);
		    (term->put_text) (x + dx / 2, y + dy / 2 + term->v_char / 3, s0);
		    (term->text) ();
		}
	    }
	}
    }
    if (is_3d_plot && (b == 1 || b == 2)) {
	if (!!(modifier_mask & Mod_Ctrl) && !needreplot) {
	    /* redraw the 3d plot if its last redraw was 'quick'
	     * (only axes) because modifier key was pressed */
	    do_save_3dplot(first_3dplot, plot3d_num, 0);
	}
	if (term->set_cursor)
	    term->set_cursor(0, 0, 0);
    }

    /* Export current mouse coords to user-accessible variables also */
    load_mouse_variables(mouse_x, mouse_y, TRUE, b);
    UpdateStatusline();

#ifdef _Windows
    if (paused_for_mouse & PAUSE_CLICK) {
	/* remove pause message box after 'pause mouse' */
	paused_for_mouse = 0;
	kill_pending_Pause_dialog();
    }
#endif
}

static void
event_motion(struct gp_event_t *ge)
{
    motion = 1;

    mouse_x = ge->mx;
    mouse_y = ge->my;

    if (is_3d_plot
	&& (splot_map == FALSE) /* Rotate the surface if it is 3D graph but not "set view map". */
	) {

	TBOOLEAN redraw = FALSE;

	if (button & (1 << 1)) {
	    /* dragging with button 1 -> rotate */
#if 0				/* HBB 20001109: what's rint()? */
	    surface_rot_x = rint(zero_rot_x + 180.0 * mouse_y / term->ymax);
#else
	    surface_rot_x = floor(0.5 + zero_rot_x + 180.0 * mouse_y / term->ymax);
#endif
	    if (surface_rot_x < 0)
		surface_rot_x = 0;
	    if (surface_rot_x > 180)
		surface_rot_x = 180;
#if 0				/* HBB 20001109: what's rint()? */
	    surface_rot_z = rint(fmod(zero_rot_z - 360.0 * mouse_x / term->xmax, 360));
#else
	    surface_rot_z = floor(0.5 + fmod(zero_rot_z - 360.0 * mouse_x / term->xmax, 360));
#endif
	    if (surface_rot_z < 0)
		surface_rot_z += 360;
	    redraw = TRUE;
	} else if (button & (1 << 2)) {
	    /* dragging with button 2 -> scale or changing ticslevel.
	     * we compare the movement in x and y direction, and
	     * change either scale or zscale */
	    double relx, rely;
	    relx = fabs(mouse_x - start_x) / term->h_tic;
	    rely = fabs(mouse_y - start_y) / term->v_tic;

# if 0
	    /* threshold: motion should be at least 3 pixels.
	     * We've to experiment with this. */
	    if (relx < 3 && rely < 3)
		return;
# endif
	    if (modifier_mask & Mod_Shift) {
		xyplane.ticslevel += (1 + fabs(xyplane.ticslevel))
		    * (mouse_y - start_y) * 2.0 / term->ymax;
	    } else {

		if (relx > rely) {
		    surface_scale += (mouse_x - start_x) * 2.0 / term->xmax;
		    if (surface_scale < 0)
			surface_scale = 0;
		} else {
		    surface_zscale += (mouse_y - start_y) * 2.0 / term->ymax;
		    if (surface_zscale < 0)
			surface_zscale = 0;
		}
	    }
	    /* reset the start values */
	    start_x = mouse_x;
	    start_y = mouse_y;
	    redraw = TRUE;
	} /* if (mousebutton 2 is down) */

	if (!ALMOST2D) {
	    turn_ruler_off();
	}

	if (redraw) {
	    if (allowmotion) {
		/* is processing of motions allowed right now?
		 * then replot while
		 * disabling further replots until it completes */
		allowmotion = FALSE;
		do_save_3dplot(first_3dplot, plot3d_num, !!(modifier_mask & Mod_Ctrl));
	    } else {
		/* postpone the replotting */
		needreplot = TRUE;
	    }
	}
    } /* if (3D plot) */


    if (ALMOST2D) {
	/* 2D plot, or suitably aligned 3D plot: update
	 * statusline and possibly the zoombox annotation */
	if (!term->put_tmptext)
	    return;
	MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);
	UpdateStatusline();

	if (setting_zoom_region && mouse_setting.annotate_zoom_box) {
	    double real_x, real_y, real_x2, real_y2;
	    char s[64];
	    MousePosToGraphPosReal(mouse_x, mouse_y, &real_x, &real_y, &real_x2, &real_y2);
	    sprintf(s, zoombox_format(), real_x, real_y);
	    term->put_tmptext(2, s);
	}
    }
}


static void
event_modifier(struct gp_event_t *ge)
{
    modifier_mask = ge->par1;

    if (modifier_mask == 0 && is_3d_plot && (button & ((1 << 1) | (1 << 2))) && !needreplot) {
	/* redraw the 3d plot if modifier key released */
	do_save_3dplot(first_3dplot, plot3d_num, 0);
    }
}


void
event_plotdone()
{
    if (needreplot) {
	needreplot = FALSE;
	do_save_3dplot(first_3dplot, plot3d_num, !!(modifier_mask & Mod_Ctrl));
    } else {
	allowmotion = TRUE;
    }
}


void
event_reset(struct gp_event_t *ge)
{
    modifier_mask = 0;
    button = 0;
    builtin_cancel_zoom(ge);
    if (term && term->set_cursor) {
	term->set_cursor(0, 0, 0);
	if (mouse_setting.annotate_zoom_box && term->put_tmptext) {
	    term->put_tmptext(1, "");
	    term->put_tmptext(2, "");
	}
    }

    if (paused_for_mouse) {
	paused_for_mouse = 0;
#ifdef _Windows
	/* remove pause message box after 'pause mouse' */
	kill_pending_Pause_dialog();
#endif
	/* This hack is necessary for X11 in order to prevent one character */
	/* of input from being swallowed when the plot window is closed.    */
	if (term && !strncmp("x11",term->name,3))
	    ungetc('\n',stdin);
    }
}

void
do_event(struct gp_event_t *ge)
{
    if (!term)
	return;

    if (multiplot && ge->type != GE_fontprops)
	/* only informational event processing for multiplot */
	return;

    /* disable `replot` when some data were sent through stdin */
    replot_disabled = plotted_data_from_stdin;

    if (ge->type) {
	FPRINTF((stderr, "(do_event) type       = %d\n", ge->type));
	FPRINTF((stderr, "           mx, my     = %d, %d\n", ge->mx, ge->my));
	FPRINTF((stderr, "           par1, par2 = %d, %d\n", ge->par1, ge->par2));
    }

    switch (ge->type) {
    case GE_plotdone:
	event_plotdone();
	break;
    case GE_keypress:
	event_keypress(ge, TRUE);
	break;
    case GE_keypress_old:
	event_keypress(ge, FALSE);
	break;
    case GE_modifier:
	event_modifier(ge);
	break;
    case GE_motion:
	if (!mouse_setting.on)
	    break;
	event_motion(ge);
	break;
    case GE_buttonpress:
	if (!mouse_setting.on)
	    break;
	event_buttonpress(ge);
	break;
    case GE_buttonrelease:
	if (!mouse_setting.on)
	    break;
	event_buttonrelease(ge);
	break;
    case GE_replot:
	/* used only by ggi.trm */
	do_string("replot");
	break;
    case GE_reset:
	event_reset(ge);
	break;
    case GE_fontprops:
	term->h_char = ge->par1;
	term->v_char = ge->par2;
	/* Update aspect ratio based on current window size */
	term->v_tic = term->h_tic * (double)ge->mx / (double)ge->my;
	/* EAM FIXME - We could also update term->xmax and term->ymax here, */
	/*             but the existing code doesn't expect it to change.   */
	FPRINTF((stderr, "mouse do_event: window size %d X %d, font hchar %d vchar %d\n",
		ge->mx, ge->my, ge->par1,ge->par2));
	break;
    default:
	fprintf(stderr, "%s:%d protocol error\n", __FILE__, __LINE__);
	break;
    }

    replot_disabled = FALSE;	/* enable replot again */
}

static void
do_save_3dplot(struct surface_points *plots, int pcount, int quick)
{
#define M_TEST_AXIS(A) \
     (A.log && ((!(A.set_autoscale & AUTOSCALE_MIN) && A.set_min <= 0) || \
		(!(A.set_autoscale & AUTOSCALE_MAX) && A.set_max <= 0)))

    if (!plots) {
	/* this might happen after the `reset' command for example
	 * which was reported by Franz Bakan.  replotrequest()
	 * should set up again everything. */
	replotrequest();
    } else {
	if (M_TEST_AXIS(X_AXIS) || M_TEST_AXIS(Y_AXIS) || M_TEST_AXIS(Z_AXIS)
	    || M_TEST_AXIS(CB_AXIS)
	    ) {
		graph_error("axis ranges must be above 0 for log scale!");
		return;
	}
	do_3dplot(plots, pcount, quick);
    }

#undef M_TEST_AXIS
}


/*
 * bind related functions
 */

static void
bind_install_default_bindings()
{
    bind_remove_all();
    bind_append("a", (char *) 0, builtin_autoscale);
    bind_append("b", (char *) 0, builtin_toggle_border);
    bind_append("e", (char *) 0, builtin_replot);
    bind_append("g", (char *) 0, builtin_toggle_grid);
    bind_append("h", (char *) 0, builtin_help);
    bind_append("l", (char *) 0, builtin_toggle_log);
    bind_append("L", (char *) 0, builtin_nearest_log);
    bind_append("m", (char *) 0, builtin_toggle_mouse);
    bind_append("r", (char *) 0, builtin_toggle_ruler);
    bind_append("1", (char *) 0, builtin_decrement_mousemode);
    bind_append("2", (char *) 0, builtin_increment_mousemode);
    bind_append("3", (char *) 0, builtin_decrement_clipboardmode);
    bind_append("4", (char *) 0, builtin_increment_clipboardmode);
    bind_append("5", (char *) 0, builtin_toggle_polardistance);
    bind_append("6", (char *) 0, builtin_toggle_verbose);
    bind_append("7", (char *) 0, builtin_toggle_ratio);
    bind_append("n", (char *) 0, builtin_zoom_next);
    bind_append("p", (char *) 0, builtin_zoom_previous);
    bind_append("u", (char *) 0, builtin_unzoom);
    bind_append("Right", (char *) 0, builtin_rotate_right);
    bind_append("Up", (char *) 0, builtin_rotate_up);
    bind_append("Left", (char *) 0, builtin_rotate_left);
    bind_append("Down", (char *) 0, builtin_rotate_down);
    bind_append("Escape", (char *) 0, builtin_cancel_zoom);
}

static void
bind_clear(bind_t * b)
{
    b->key = NO_KEY;
    b->modifier = 0;
    b->command = (char *) 0;
    b->builtin = 0;
    b->prev = (struct bind_t *) 0;
    b->next = (struct bind_t *) 0;
}

/* returns the enum which corresponds to the
 * string (ptr) or NO_KEY if ptr matches not
 * any of special_keys. */
static int
lookup_key(char *ptr, int *len)
{
    char **keyptr;
    /* first, search in the table of "usual well-known" keys */
    int what = lookup_table_nth(usual_special_keys, ptr);
    if (what >= 0) {
	*len = strlen(usual_special_keys[what].key);
	return usual_special_keys[what].value;
    }
    /* second, search in the table of other keys */
    for (keyptr = special_keys; *keyptr; ++keyptr) {
	if (!strncasecmp(ptr, *keyptr, (*len = strlen(*keyptr)))) {
	    return keyptr - special_keys + GP_FIRST_KEY;
	}
    }
    return NO_KEY;
}

/* returns 1 on success, else 0. */
static int
bind_scan_lhs(bind_t * out, const char *in)
{
    static const char DELIM = '-';
    int itmp = NO_KEY;
    char *ptr;
    int len;
    bind_clear(out);
    if (!in) {
	return 0;
    }
    for (ptr = (char *) in; ptr && *ptr; /* EMPTY */ ) {
	if (!strncasecmp(ptr, "alt-", 4)) {
	    out->modifier |= Mod_Alt;
	    ptr += 4;
	} else if (!strncasecmp(ptr, "ctrl-", 5)) {
	    out->modifier |= Mod_Ctrl;
	    ptr += 5;
	} else if (NO_KEY != (itmp = lookup_key(ptr, &len))) {
	    out->key = itmp;
	    ptr += len;
	} else if ((out->key = *ptr++) && *ptr && *ptr != DELIM) {
	    fprintf(stderr, "bind: cannot parse %s\n", in);
	    return 0;
	}
    }
    if (NO_KEY == out->key)
	return 0;		/* failed */
    else
	return 1;		/* success */
}

/* note, that this returns a pointer
 * to the static char* `out' which is
 * modified on subsequent calls.
 */
static char *
bind_fmt_lhs(const bind_t * in)
{
    static char out[0x40];
    out[0] = '\0';		/* empty string */
    if (!in)
	return out;
    if (in->modifier & Mod_Ctrl) {
	sprintf(out, "Ctrl-");
    }
    if (in->modifier & Mod_Alt) {
	sprintf(out, "%sAlt-", out);
    }
    if (in->key > GP_FIRST_KEY && in->key < GP_LAST_KEY) {
	sprintf(out, "%s%s", out, special_keys[in->key - GP_FIRST_KEY]);
    } else {
	int k = 0;
	for ( ; usual_special_keys[k].value > 0; k++) {
	    if (usual_special_keys[k].value == in->key) {
		sprintf(out, "%s%s", out, usual_special_keys[k].key);
		k = -1;
		break;
	    }
	}
	if (k >= 0)
	sprintf(out, "%s%c", out, in->key);
    }
    return out;
}

static int
bind_matches(const bind_t * a, const bind_t * b)
{
    /* discard Shift modifier */
    int a_mod = a->modifier & (Mod_Ctrl | Mod_Alt);
    int b_mod = b->modifier & (Mod_Ctrl | Mod_Alt);

    if (a->key == b->key && a_mod == b_mod)
	return 1;
    else
	return 0;
}

static void
bind_display_one(bind_t * ptr)
{
    fprintf(stderr, " %-12s ", bind_fmt_lhs(ptr));
    fprintf(stderr, "%c ", ptr->allwindows ? '*' : ' ');
    if (ptr->command) {
	fprintf(stderr, "`%s'\n", ptr->command);
    } else if (ptr->builtin) {
	fprintf(stderr, "%s\n", ptr->builtin(0));
    } else {
	fprintf(stderr, "`%s:%d oops.'\n", __FILE__, __LINE__);
    }
}

static void
bind_display(char *lhs)
{
    bind_t *ptr;
    bind_t lhs_scanned;

    if (!bindings) {
	bind_install_default_bindings();
    }

    if (!lhs) {
	/* display all bindings */
	char fmt[] = " %-17s  %s\n";
	fprintf(stderr, "\n");
	fprintf(stderr, fmt, "2x<B1>",
		"print coordinates to clipboard using `clipboardformat`\n                    (see keys '3', '4')");
	fprintf(stderr, fmt, "<B2>", "annotate the graph using `mouseformat` (see keys '1', '2')");
	fprintf(stderr, fmt, "", "or draw labels if `set mouse labels is on`");
	fprintf(stderr, fmt, "<Ctrl-B2>", "remove label close to pointer if `set mouse labels` is on");
	fprintf(stderr, fmt, "<B3>", "mark zoom region (only for 2d-plots and maps).");
	fprintf(stderr, fmt, "<B1-Motion>", "change view (rotation). Use <ctrl> to rotate the axes only.");
	fprintf(stderr, fmt, "<B2-Motion>", "change view (scaling). Use <ctrl> to scale the axes only.");
	fprintf(stderr, fmt, "<Shift-B2-Motion>", "vertical motion -- change xyplane");
	fprintf(stderr, "\n");
	fprintf(stderr, " %-12s   %s\n", "Space", "raise gnuplot console window");
	fprintf(stderr, " %-12s * %s\n", "q", "close this X11 plot window");
	fprintf(stderr, "\n");
	for (ptr = bindings; ptr; ptr = ptr->next) {
	    bind_display_one(ptr);
	}
	fprintf(stderr, "\n");
	fprintf(stderr, "              * indicates this key is active from all plot windows\n");
	fprintf(stderr, "\n");
	return;
    }

    if (!bind_scan_lhs(&lhs_scanned, lhs)) {
	return;
    }
    for (ptr = bindings; ptr; ptr = ptr->next) {
	if (bind_matches(&lhs_scanned, ptr)) {
	    bind_display_one(ptr);
	    break;		/* only one match */
	}
    }
}

static void
bind_remove(bind_t * b)
{
    if (!b) {
	return;
    } else if (b->builtin) {
	/* don't remove builtins, just remove the overriding command */
	if (b->command) {
	    free(b->command);
	    b->command = (char *) 0;
	}
	return;
    }
    if (b->prev)
	b->prev->next = b->next;
    if (b->next)
	b->next->prev = b->prev;
    else
	bindings->prev = b->prev;
    if (b->command) {
	free(b->command);
	b->command = (char *) 0;
    }
    if (b == bindings) {
	bindings = b->next;
	if (bindings && bindings->prev) {
	    bindings->prev->next = (bind_t *) 0;
	}
    }
    free(b);
}

static void
bind_append(char *lhs, char *rhs, char *(*builtin) (struct gp_event_t * ge))
{
    bind_t *new = (bind_t *) gp_alloc(sizeof(bind_t), "bind_append->new");

    if (!bind_scan_lhs(new, lhs)) {
	free(new);
	return;
    }

    if (!bindings) {
	/* first binding */
	bindings = new;
    } else {

	bind_t *ptr;
	for (ptr = bindings; ptr; ptr = ptr->next) {
	    if (bind_matches(new, ptr)) {
		/* overwriting existing binding */
		if (!rhs) {
		    ptr->builtin = builtin;
		} else if (*rhs) {
		    if (ptr->command) {
			free(ptr->command);
			ptr->command = (char *) 0;
		    }
		    ptr->command = rhs;
		} else {	/* rhs is an empty string, so remove the binding */
		    bind_remove(ptr);
		}
		free(new);	/* don't need it any more */
		return;
	    }
	}
	/* if we're here, the binding does not exist yet */
	/* append binding ... */
	bindings->prev->next = new;
	new->prev = bindings->prev;
    }

    bindings->prev = new;
    new->next = (struct bind_t *) 0;
    new->allwindows = FALSE;	/* Can be explicitly set later */
    if (!rhs) {
	new->builtin = builtin;
    } else if (*rhs) {
	new->command = rhs;	/* was allocated in command.c */
    } else {
	bind_remove(new);
    }
}

void
bind_process(char *lhs, char *rhs, TBOOLEAN allwindows)
{
    if (!bindings) {
	bind_install_default_bindings();
    }
    if (!rhs) {
	bind_display(lhs);
    } else {
	bind_append(lhs, rhs, 0);
	if (allwindows)
	    bind_all(lhs);
    }
    if (lhs)
	free(lhs);
}

void
bind_all(char *lhs)
{
    bind_t *ptr;
    bind_t keypress;

    if (!bind_scan_lhs(&keypress, lhs))
	return;

    for (ptr = bindings; ptr; ptr = ptr->next) {
	if (bind_matches(&keypress, ptr))
	    ptr->allwindows = TRUE;
    }
}

void
bind_remove_all()
{
    bind_t *ptr;
    bind_t *safe;
    for (ptr = bindings; ptr; safe = ptr, ptr = ptr->next, free(safe)) {
	if (ptr->command) {
	    free(ptr->command);
	    ptr->command = (char *) 0;
	}
    }
    bindings = (bind_t *) 0;
}

/* Ruler is on, thus recalc its (px,py) from (x,y) for the current zoom and
   log axes.
*/
static void
recalc_ruler_pos()
{
    double P, dummy;
    if (is_3d_plot) {
	/* To be exact, it is 'set view map' splot. */
	unsigned int ppx, ppy;
	dummy = 1.0; /* dummy value, but not 0.0 for the fear of log z-axis */
	map3d_xy(ruler.x, ruler.y, dummy, &ppx, &ppy);
	ruler.px = ppx;
	ruler.py = ppy;
	return;
    }
    /* It is 2D plot. */
    if (axis_array[FIRST_X_AXIS].log && ruler.x < 0)
	ruler.px = -1;
    else {
	P = AXIS_LOG_VALUE(FIRST_X_AXIS, ruler.x);
	ruler.px = AXIS_MAP(FIRST_X_AXIS, P);
    }
    if (axis_array[FIRST_Y_AXIS].log && ruler.y < 0)
	ruler.py = -1;
    else {
	P = AXIS_LOG_VALUE(FIRST_Y_AXIS, ruler.y);
	ruler.py = AXIS_MAP(FIRST_Y_AXIS, P);
    }
    MousePosToGraphPosReal(ruler.px, ruler.py, &dummy, &dummy, &ruler.x2, &ruler.y2);
}


/* Recalculate and replot the ruler after a '(re)plot'. Called from term.c.
*/
void
update_ruler()
{
    if (!term->set_ruler || !ruler.on)
	return;
    (*term->set_ruler) (-1, -1);
    recalc_ruler_pos();
    (*term->set_ruler) (ruler.px, ruler.py);
}

/* Set ruler on/off, and set its position.
   Called from set.c for 'set mouse ruler ...' command.
*/
void
set_ruler(TBOOLEAN on, int mx, int my)
{
    struct gp_event_t ge;
    if (ruler.on == FALSE && on == FALSE)
	return;
    if (ruler.on == TRUE && on == TRUE && (mx < 0 || my < 0))
	return;
    if (ruler.on == TRUE) /* ruler is on => switch it off */
	builtin_toggle_ruler(&ge);
    /* now the ruler is off */
    if (on == FALSE) /* want ruler off */
	return;
    if (mx>=0 && my>=0) { /* change ruler position */
	ge.mx = mx;
	ge.my = my;
    } else { /* don't change ruler position */
	ge.mx = ruler.px;
	ge.my = ruler.py;
    }
    builtin_toggle_ruler(&ge);
}

/* for checking if we change from plot to splot (or vice versa) */
int
plot_mode(int set)
{
    static int mode = MODE_PLOT;
    if (MODE_PLOT == set || MODE_SPLOT == set) {
	if (mode != set) {
	    turn_ruler_off();
	}
	mode = set;
    }
    return mode;
}

static void
turn_ruler_off()
{
    if (ruler.on) {
	struct udvt_entry *u;
	ruler.on = FALSE;
	if (term && term->set_ruler) {
	    (*term->set_ruler) (-1, -1);
	}
	if ((u = add_udv_by_name("MOUSE_RULER_X")))
	    u->udv_undef = TRUE;
	if ((u = add_udv_by_name("MOUSE_RULER_Y")))
	    u->udv_undef = TRUE;
	if (display_ipc_commands()) {
	    fprintf(stderr, "turning ruler off.\n");
	}
    }
}

static int
nearest_label_tag(int xref, int yref, struct termentry *t)
{
    double min = -1;
    int min_tag = -1;
    double diff_squared;
    unsigned int x, y;
    struct text_label *this_label;
    int xd;
    int yd;

    for (this_label = first_label; this_label != NULL; this_label = this_label->next) {
	if (is_3d_plot) {
	    map3d_position(&this_label->place, &xd, &yd, "label");
	    xd -= xref;
	    yd -= yref;
	} else {
	    map_position(&this_label->place, &x, &y, "label");
	    xd = (int) x - (int) xref;
	    yd = (int) y - (int) yref;
	}
	diff_squared = xd * xd + yd * yd;
	if (-1 == min || min > diff_squared) {
	    /* now we check if we're within a certain
	     * threshold around the label */
	    double tic_diff_squared;
	    int htic, vtic;
	    get_offsets(this_label, t, &htic, &vtic);
	    tic_diff_squared = htic * htic + vtic * vtic;
	    if (diff_squared < tic_diff_squared) {
		min = diff_squared;
		min_tag = this_label->tag;
	    }
	}
    }

    return min_tag;
}

static void
remove_label(int x, int y)
{
    int tag = nearest_label_tag(x, y, term);
    if (-1 != tag) {
	char cmd[0x40];
	sprintf(cmd, "unset label %d", tag);
	do_string_replot(cmd);
    }
}

static void
put_label(char *label, double x, double y)
{
    char cmd[0xff];
    sprintf(cmd, "set label \"%s\" at %g,%g %s", label, x, y, mouse_setting.labelopts);
    do_string_replot(cmd);
}

#ifdef OS2
/* routine required by pm.trm: fill in information needed for (un)checking
   menu items in the Presentation Manager terminal
*/
void 
PM_set_gpPMmenu __PROTO((struct t_gpPMmenu * gpPMmenu))
{
    gpPMmenu->use_mouse = mouse_setting.on;
    if (zoom_now == NULL)
	gpPMmenu->where_zoom_queue = 0;
    else {
	gpPMmenu->where_zoom_queue = (zoom_now == zoom_head) ? 0 : 1;
	if (zoom_now->prev != NULL)
	    gpPMmenu->where_zoom_queue |= 2;
	if (zoom_now->next != NULL)
	    gpPMmenu->where_zoom_queue |= 4;
    }
    gpPMmenu->polar_distance = mouse_setting.polardistance;
}
#endif

/* Save current mouse position to user-accessible variables.
 * Save the keypress or mouse button that triggered this in MOUSE_KEY,
 * and define MOUSE_BUTTON if it was a button click.
 */
static void
load_mouse_variables(double x, double y, TBOOLEAN button, int c)
{
    struct udvt_entry *current;

    MousePosToGraphPosReal(x, y, &real_x, &real_y, &real_x2, &real_y2);

    if ((current = add_udv_by_name("MOUSE_BUTTON"))) {
	current->udv_undef = !button;
	Ginteger(&current->udv_value, button?c:-1);
    }
    if ((current = add_udv_by_name("MOUSE_KEY"))) {
	current->udv_undef = FALSE;
	Ginteger(&current->udv_value,c);
    }
#ifdef GP_STRING_VARS
    if ((current = add_udv_by_name("MOUSE_CHAR"))) {
	char *keychar = gp_alloc(2,"key_char");
	keychar[0] = c;
	keychar[1] = '\0';
	if (!current->udv_undef)
	    gpfree_string(&current->udv_value);
	current->udv_undef = FALSE;
	Gstring(&current->udv_value,keychar);
    }
#endif

    if ((current = add_udv_by_name("MOUSE_X"))) {
	current->udv_undef = FALSE;
	Gcomplex(&current->udv_value,real_x,0);
    }
    if ((current = add_udv_by_name("MOUSE_Y"))) {
	current->udv_undef = FALSE;
	Gcomplex(&current->udv_value,real_y,0);
    }
    if ((current = add_udv_by_name("MOUSE_X2"))) {
	current->udv_undef = FALSE;
	Gcomplex(&current->udv_value,real_x2,0);
    }
    if ((current = add_udv_by_name("MOUSE_Y2"))) {
	current->udv_undef = FALSE;
	Gcomplex(&current->udv_value,real_y2,0);
    }
    if ((current = add_udv_by_name("MOUSE_SHIFT"))) {
	current->udv_undef = FALSE;
	Ginteger(&current->udv_value, modifier_mask & Mod_Shift);
    }
    if ((current = add_udv_by_name("MOUSE_ALT"))) {
	current->udv_undef = FALSE;
	Ginteger(&current->udv_value, modifier_mask & Mod_Alt);
    }
    if ((current = add_udv_by_name("MOUSE_CTRL"))) {
	current->udv_undef = FALSE;
	Ginteger(&current->udv_value, modifier_mask & Mod_Ctrl);
    }
    return;
}

#endif /* USE_MOUSE */
