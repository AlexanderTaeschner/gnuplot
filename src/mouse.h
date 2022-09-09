/* GNUPLOT - mouse.h */

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


#ifndef _HAVE_MOUSE_H
#define _HAVE_MOUSE_H

#include "mousecmn.h"
#include "syscfg.h"
#include "eval.h"

/* Zoom queue
*/
struct t_zoom {
  double xmin, ymin, xmax, ymax;
  double x2min, y2min, x2max, y2max;
  struct t_zoom *prev, *next;
};

typedef struct mouse_setting_t {
    int on;                /* ...                                         */
    int doubleclick;       /* Button1 double / single click resolution    */
    int annotate_zoom_box; /* draw coordinates at zoom box                */
    int label;             /* draw real gnuplot labels on Button 2        */
    int polardistance;     /* display dist. to ruler in polar coordinates */
    int verbose;           /* display ipc commands                        */
    int warp_pointer;      /* warp pointer after starting a zoom box      */
    double xmzoom_factor;  /* scale factor for +/- zoom on x		  */
    double ymzoom_factor;  /* scale factor for +/- zoom on y		  */
    char *fmt;             /* fprintf format for printing numbers         */
    char *labelopts;       /* label options                               */
} mouse_setting_t;

/* start with mouse on by default */
#define DEFAULT_MOUSE_MODE    1
#define DEFAULT_MOUSE_SETTING { \
    DEFAULT_MOUSE_MODE,         \
    300, /* ms */               \
    1, 0, 0, 0, 0,              \
    1.0, 1.0,			\
    mouse_fmt_default,          \
    NULL                        \
}

extern long mouse_mode;
extern char* mouse_alt_string;
extern mouse_setting_t default_mouse_setting;
extern mouse_setting_t mouse_setting;
extern char mouse_fmt_default[];
extern udft_entry mouse_readout_function;

enum {
    MOUSE_COORDINATES_REAL = 0,
    MOUSE_COORDINATES_REAL1, /* w/o brackets */
    MOUSE_COORDINATES_FRACTIONAL,
    MOUSE_COORDINATES_TIMEFMT,
    MOUSE_COORDINATES_XDATE,
    MOUSE_COORDINATES_XTIME,
    MOUSE_COORDINATES_XDATETIME,
    MOUSE_COORDINATES_ALT,    /* alternative format as specified by the user */
    MOUSE_COORDINATES_FUNCTION = 8 /* value needed in term.c even if no USE_MOUSE */
};

void event_plotdone(void);
void recalc_statusline(void);
void update_ruler(void);
void set_ruler(TBOOLEAN on, int mx, int my);
void UpdateStatusline(void);
void do_event(struct gp_event_t *ge);
TBOOLEAN exec_event(char type, int mx, int my, int par1, int par2, int winid); /* wrapper for do_event() */
int plot_mode(int mode);
void event_reset(struct gp_event_t *ge);

/* bind prototype(s) */

void bind_process(char* lhs, char* rhs, TBOOLEAN allwindows);
void bind_remove_all(void);

/* mechanism for the core code to query the last-known mouse coordinates */
extern void get_last_mouse_xy( double *x, double *y );

#endif /* !_HAVE_MOUSE_H */
