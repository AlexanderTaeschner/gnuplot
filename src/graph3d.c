#ifndef lint
static char *RCSid() { return RCSid("$Id: graph3d.c,v 1.106 2004/11/22 00:43:04 sfeam Exp $"); }
#endif

/* GNUPLOT - graph3d.c */

/*[
 * Copyright 1986 - 1993, 1998, 2004   Thomas Williams, Colin Kelley
 *
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
 *   Original Software:
 *       Gershon Elber and many others.
 *
 * 19 September 1992  Lawrence Crowl  (crowl@cs.orst.edu)
 * Added user-specified bases for log scaling.
 *
 * 3.6 - split graph3d.c into graph3d.c (graph),
 *                            util3d.c (intersections, etc)
 *                            hidden3d.c (hidden-line removal code)
 *
 */

#include "graph3d.h"

#include "alloc.h"
#include "axis.h"
#include "gadgets.h"
/*  #include "graphics.h" */		/* HBB 20000506: put in again, for label_width() */
#include "hidden3d.h"
#include "misc.h"
/*  #include "setshow.h" */
#include "term_api.h"
#include "util3d.h"
#include "util.h"

#ifdef PM3D
#   include "pm3d.h"
#   include "plot3d.h"
#   include "color.h"
#endif
#ifdef WITH_IMAGE
#include "plot.h"
#endif

/* HBB NEW 20040311: PM3D did already split up grid drawing into two
 * parts, one before, the other after drawing the main surfaces, as a
 * poor-man's depth-sorting algorithm.  Make this independent of
 * PM3D. Turn the new option on by default. */
#if defined(PM3D) || !defined(USE_GRID_LAYERS)
# define USE_GRID_LAYERS 1
#endif


static int p_height;
static int p_width;		/* pointsize * t->h_tic */
static int key_entry_height;	/* bigger of t->v_size, pointsize*t->v_tick */

/* is contouring wanted ? */
t_contour_placement draw_contour = CONTOUR_NONE;
/* different linestyles are used for contours when set */
TBOOLEAN label_contours = TRUE;

/* Want to draw surfaces? FALSE mainly useful in contouring mode */
TBOOLEAN draw_surface = TRUE;

/* Was hidden3d display selected by user? */
TBOOLEAN hidden3d = FALSE;

/* Rotation and scale of the 3d view, as controlled by 'set view': */
float surface_rot_z = 30.0;
float surface_rot_x = 60.0;
float surface_scale = 1.0;
float surface_zscale = 1.0;
/* Set by 'set view map': */
int splot_map = FALSE;

/* position of the base plane, as given by 'set ticslevel' */
float ticslevel = 0.5;

/* 'set isosamples' settings */
int iso_samples_1 = ISO_SAMPLES;
int iso_samples_2 = ISO_SAMPLES;

double xscale3d, yscale3d, zscale3d;

typedef enum { ALLGRID, FRONTGRID, BACKGRID } WHICHGRID;

static void plot3d_impulses __PROTO((struct surface_points * plot));
static void plot3d_lines __PROTO((struct surface_points * plot));
static void plot3d_points __PROTO((struct surface_points * plot, /* FIXME PM3D: */ int p_type));
static void plot3d_vectors __PROTO((struct surface_points * plot));
#ifdef PM3D
/* no pm3d for impulses */
static void plot3d_lines_pm3d __PROTO((struct surface_points * plot));
static void plot3d_points_pm3d __PROTO((struct surface_points * plot, int p_type));
static void get_surface_cbminmax __PROTO((struct surface_points *plot, double *cbmin, double *cbmax));
#endif
static void cntr3d_impulses __PROTO((struct gnuplot_contours * cntr,
				     struct lp_style_type * lp));
static void cntr3d_lines __PROTO((struct gnuplot_contours * cntr,
				  struct lp_style_type * lp));
/* HBB UNUSED 20031219 */
/* static void cntr3d_linespoints __PROTO((struct gnuplot_contours * cntr, */
/* 					struct lp_style_type * lp)); */
static void cntr3d_points __PROTO((struct gnuplot_contours * cntr,
				   struct lp_style_type * lp));
static void cntr3d_dots __PROTO((struct gnuplot_contours * cntr));
static void check_corner_height __PROTO((struct coordinate GPHUGE * point,
					 double height[2][2], double depth[2][2]));
static void setup_3d_box_corners __PROTO((void));
static void draw_3d_graphbox __PROTO((struct surface_points * plot,
				      int plot_count,
				      WHICHGRID whichgrid));
/* HBB 20010118: these should be static, but can't --- HP-UX assembler bug */
void xtick_callback __PROTO((AXIS_INDEX, double place, char *text,
			     struct lp_style_type grid));
void ytick_callback __PROTO((AXIS_INDEX, double place, char *text,
			     struct lp_style_type grid));
void ztick_callback __PROTO((AXIS_INDEX, double place, char *text,
			     struct lp_style_type grid));

static int find_maxl_cntr __PROTO((struct gnuplot_contours * contours, int *count));
static int find_maxl_keys3d __PROTO((struct surface_points *plots, int count, int *kcnt));
static void boundary3d __PROTO((struct surface_points * plots, int count));

/* put entries in the key */
static void key_sample_line __PROTO((int xl, int yl));
static void key_sample_point __PROTO((int xl, int yl, int pointtype));
#ifdef PM3D
static void key_sample_line_pm3d __PROTO((struct surface_points *plot, int xl, int yl));
static void key_sample_point_pm3d __PROTO((struct surface_points *plot, int xl, int yl, int pointtype));
#endif
static void key_text __PROTO((int xl, int yl, char *text));

static TBOOLEAN get_arrow3d __PROTO((struct arrow_def*, int*, int*, int*, int*));
static void place_arrows3d __PROTO((int));
static void place_labels3d __PROTO((struct text_label * listhead, int layer));
static int map3d_getposition __PROTO((struct position* pos, const char* what, double* xpos, double* ypos, double* zpos));

/*
 * The Amiga SAS/C 6.2 compiler moans about macro envocations causing
 * multiple calls to functions. I converted these macros to inline
 * functions coping with the problem without loosing speed.
 * (MGR, 1993)
 */
#ifdef AMIGA_SC_6_1
GP_INLINE static TBOOLEAN
i_inrange(int z, int min, int max)
{
    return ((min < max)
	    ? ((z >= min) && (z <= max))
	    : ((z >= max) && (z <= min)));
}

GP_INLINE static double
f_max(double a, double b)
{
    return (max(a, b));
}

GP_INLINE static double
f_min(double a, double b)
{
    return (min(a, b));
}

#else
# define f_max(a,b) GPMAX((a),(b))
# define f_min(a,b) GPMIN((a),(b))
# define i_inrange(z,a,b) inrange((z),(a),(b))
#endif

#define apx_eq(x,y) (fabs(x-y) < 0.001)
#define ABS(x) ((x) >= 0 ? (x) : -(x))
#define SQR(x) ((x) * (x))

/* Define the boundary of the plot
 * These are computed at each call to do_plot, and are constant over
 * the period of one do_plot. They actually only change when the term
 * type changes and when the 'set size' factors change.
 */

int xmiddle, ymiddle, xscaler, yscaler;
static int ptitl_cnt;
static int max_ptitl_len;
static int titlelin;
static int key_sample_width, key_rows, key_cols, key_col_wth, yl_ref;
static int ktitle_lines = 0;


/* Boundary and scale factors, in user coordinates */

/* There are several z's to take into account - I hope I get these
 * right !
 *
 * ceiling_z is the highest z in use
 * floor_z   is the lowest z in use
 * base_z is the z of the base
 * min3d_z is the lowest z of the graph area
 * max3d_z is the highest z of the graph area
 *
 * ceiling_z is either max3d_z or base_z, and similarly for floor_z
 * There should be no part of graph drawn outside
 * min3d_z:max3d_z  - apart from arrows, perhaps
 */

double floor_z;
double ceiling_z, base_z;	/* made exportable for PM3D */

transform_matrix trans_mat;

/* x and y input range endpoints where the three axes are to be
 * displayed (left, front-left, and front-right edges of the cube) */
static double xaxis_y, yaxis_x, zaxis_x, zaxis_y;

/* ... and the same for the back, right, and front corners */
static double back_x, back_y;
static double right_x, right_y;
static double front_x, front_y;

#ifdef USE_MOUSE
int axis3d_o_x, axis3d_o_y, axis3d_x_dx, axis3d_x_dy, axis3d_y_dx, axis3d_y_dy;
#endif

/* the penalty for convenience of using tic_gen to make callbacks
 * to tick routines is that we cannot pass parameters very easily.
 * We communicate with the tick_callbacks using static variables
 */

/* unit vector (terminal coords) */
static double tic_unitx, tic_unity, tic_unitz;

/* calculate the number and max-width of the keys for an splot.
 * Note that a blank line is issued after each set of contours
 */
static int
find_maxl_keys3d(struct surface_points *plots, int count, int *kcnt)
{
    int mlen, len, surf, cnt;
    struct surface_points *this_plot;

    mlen = cnt = 0;
    this_plot = plots;
    for (surf = 0; surf < count; this_plot = this_plot->next_sp, surf++) {

	/* we draw a main entry if there is one, and we are
	 * drawing either surface, or unlabelled contours
	 */
	if (this_plot->title && *this_plot->title &&
	    (draw_surface || (draw_contour && !label_contours))) {
	    ++cnt;
	    len = strlen(this_plot->title);
	    if (len > mlen)
		mlen = len;
	}
	if (draw_contour && label_contours && this_plot->contours != NULL) {
	    len = find_maxl_cntr(this_plot->contours, &cnt);
	    if (len > mlen)
		mlen = len;
	}
    }

    if (kcnt != NULL)
	*kcnt = cnt;
    return (mlen);
}

static int
find_maxl_cntr(struct gnuplot_contours *contours, int *count)
{
    int cnt;
    int mlen, len;
    struct gnuplot_contours *cntrs = contours;

    mlen = cnt = 0;
    while (cntrs) {
	if (label_contours && cntrs->isNewLevel) {
	    len = strlen(cntrs->label);
	    if (len)
		cnt++;
	    if (len > mlen)
		mlen = len;
	}
	cntrs = cntrs->next;
    }
    *count += cnt;
    return (mlen);
}


/* borders of plotting area */
/* computed once on every call to do_plot */
static void
boundary3d(struct surface_points *plots, int count)
{
    legend_key *key = &keyT;
    struct termentry *t = term;
    int ytlen, i;

    titlelin = 0;

    p_height = pointsize * t->v_tic;
    p_width = pointsize * t->h_tic;
    if (key->swidth >= 0)
	key_sample_width = key->swidth * t->h_char + pointsize * t->h_tic;
    else
	key_sample_width = 0;
    key_entry_height = pointsize * t->v_tic * 1.25 * key->vert_factor;
    if (key_entry_height < t->v_char) {
	/* is this reasonable ? */
	key_entry_height = t->v_char * key->vert_factor;
    }
    /* count max_len key and number keys (plot-titles and contour labels) with len > 0 */
    max_ptitl_len = find_maxl_keys3d(plots, count, &ptitl_cnt);
    if ((ytlen = label_width(key->title, &ktitle_lines)) > max_ptitl_len)
	max_ptitl_len = ytlen;
    key_col_wth = (max_ptitl_len + 4) * t->h_char + key_sample_width;

    /* luecken@udel.edu modifications
       sizes the plot to take up more of available resolution */
    if (lmargin >= 0)
	xleft = t->h_char * lmargin;
    else
	xleft = t->h_char * 2 + t->h_tic;
    xright = xsize * t->xmax - t->h_char * 2 - t->h_tic;
    key_rows = ptitl_cnt;
    key_cols = 1;
    if (key->flag == KEY_AUTO_PLACEMENT && key->vpos == TUNDER) {
	if (ptitl_cnt > 0) {
	    /* calculate max no cols, limited by label-length */
	    key_cols = (int) (xright - xleft) / ((max_ptitl_len + 4) * t->h_char + key_sample_width);
	    /* HBB 991019: fix division by zero problem */
	    if (key_cols == 0)
		key_cols = 1;
	    key_rows = (int) ((ptitl_cnt - 1)/ key_cols) + 1;
	    /* now calculate actual no cols depending on no rows */
	    key_cols = (int) ((ptitl_cnt - 1)/ key_rows) + 1;
	    key_col_wth = (int) (xright - xleft) / key_cols;
	    /* key_rows += ktitle_lines; - messes up key - div */
	} else {
	    key_rows = key_cols = key_col_wth = 0;
	}
    }
    /* this should also consider the view and number of lines in
     * xformat || yformat || xlabel || ylabel */

    /* an absolute 1, with no terminal-dependent scaling ? */
    ybot = t->v_char * 2.5 + 1;
    if (key_rows && key->flag == KEY_AUTO_PLACEMENT && key->vpos == TUNDER)
	ybot += key_rows * key_entry_height + ktitle_lines * t->v_char;

    if (strlen(title.text)) {
	titlelin++;
	for (i = 0; i < strlen(title.text); i++) {
	    if (title.text[i] == '\\')
		titlelin++;
	}
    }
    ytop = ysize * t->ymax - t->v_char * (titlelin + 1.5) - 1;
    if (key->flag == KEY_AUTO_PLACEMENT && key->vpos != TUNDER) {
	/* calculate max no rows, limited be ytop-ybot */
	i = (int) (ytop - ybot) / t->v_char - 1 - ktitle_lines;
	/* HBB 20030321: div by 0 fix like above */
	if (i == 0)
	    i = 1;
	if (ptitl_cnt > i) {
	    key_cols = (int) ((ptitl_cnt - 1)/ i) + 1;
	    /* now calculate actual no rows depending on no cols */
	    key_rows = (int) ((ptitl_cnt - 1) / key_cols) + 1;
	}
	key_rows += ktitle_lines;
    }
    if (key->flag == KEY_AUTO_PLACEMENT && key->hpos == TOUT) {
	xright -= key_col_wth * (key_cols - 1) + key_col_wth - 2 * t->h_char;
    }
    xleft += t->xmax * xoffset;
    xright += t->xmax * xoffset;
    ytop += t->ymax * yoffset;
    ybot += t->ymax * yoffset;
    xmiddle = (xright + xleft) / 2;
    ymiddle = (ytop + ybot) / 2;
    /* HBB 980308: sigh... another 16bit glitch: on term's with more than
     * 8000 pixels in either direction, these calculations produce garbage
     * results if done in (16bit) ints */
    xscaler = ((xright - xleft) * 4L) / 7L;	/* HBB: Magic number alert! */
    yscaler = ((ytop - ybot) * 4L) / 7L;

    /* HBB 20011011: 'set size {square|ratio}' for splots */
    if (aspect_ratio != 0.0) {
	double current_aspect_ratio;

	if (aspect_ratio < 0
	    && (X_AXIS.max - X_AXIS.min) != 0.0
	    ) {
	    current_aspect_ratio = - aspect_ratio
		* fabs((Y_AXIS.max - Y_AXIS.min) /
		       (X_AXIS.max - X_AXIS.min));
	} else
	    current_aspect_ratio = aspect_ratio;

	/*{{{  set aspect ratio if valid and sensible */
	if (current_aspect_ratio >= 0.01 && current_aspect_ratio <= 100.0) {
	    double current = (double)yscaler / xscaler ;
	    double required = current_aspect_ratio * t->v_tic / t->h_tic;

	    if (current > required)
		/* too tall */
		yscaler = xscaler * required;
	    else
		/* too wide */
		xscaler = yscaler / required;
	}
    }
}

static TBOOLEAN
get_arrow3d(
    struct arrow_def* arrow,
    int* sx, int* sy,
    int* ex, int* ey)
{
    map3d_position(&(arrow->start), sx, sy, "arrow");

    /* EAM  FIXME - Is this a sufficiently general test for out-of-bounds? */
    /*              Should we test the other end of the arrow also?        */
    if (*sx < 0 || *sx > term->xmax || *sy < 0 || *sy > term->ymax)
	return FALSE;

    if (arrow->relative) {
	map3d_position_r(&(arrow->end), ex, ey, "arrow");
	*ex += *sx;
	*ey += *sy;
    } else {
	map3d_position(&(arrow->end), ex, ey, "arrow");
    }

    return TRUE;
}

static void
place_labels3d(struct text_label *listhead, int layer)
{
    struct text_label *this_label;
    int x, y;

    if (term->pointsize)
	(*term->pointsize)(pointsize);

    for (this_label = listhead; this_label != NULL; this_label = this_label->next) {

	if (this_label->layer != layer)
	    continue;

	map3d_position(&this_label->place, &x, &y, "label");

	/* EAM FIXME - Is this a sufficient test for out-of-bounds? */
	if (x < 0 || x > term->xmax || y < 0 || y > term->ymax) {
	    FPRINTF((stderr,"place_labels3d: skipping out-of-bounds label\n"));
	    continue;
	}

	write_label(x, y, this_label);
    }
}

static void
place_arrows3d(int layer)
{
    struct termentry *t = term;
    struct arrow_def *this_arrow;
    for (this_arrow = first_arrow; this_arrow != NULL;
	 this_arrow = this_arrow->next) {
	int sx, sy, ex, ey;

	if (this_arrow->arrow_properties.layer != layer)
	    continue;
	if (get_arrow3d(this_arrow, &sx, &sy, &ex, &ey)) {
	    term_apply_lp_properties(&(this_arrow->arrow_properties.lp_properties));
	    apply_head_properties(&(this_arrow->arrow_properties));
	    (*t->arrow) ((unsigned int)sx, (unsigned int)sy, (unsigned int)ex, (unsigned int)ey, 
		this_arrow->arrow_properties.head);
	} else {
	    FPRINTF((stderr,"place_arrows3d: skipping out-of-bounds arrow\n"));
	}
    }
}

/* we precalculate features of the key, to save lots of nested
 * ifs in code - x,y = user supplied or computed position of key
 * taken to be inner edge of a line sample
 */
static int key_sample_left;	/* offset from x for left of line sample */
static int key_sample_right;	/* offset from x for right of line sample */
static int key_point_offset;	/* offset from x for point sample */
static int key_text_left;	/* offset from x for left-justified text */
static int key_text_right;	/* offset from x for right-justified text */
static int key_size_left;	/* distance from x to left edge of box */
static int key_size_right;	/* distance from x to right edge of box */


void
do_3dplot(
    struct surface_points *plots,
    int pcount,			/* count of plots in linked list */
    int quick)		 	/* !=0 means plot only axes etc., for quick rotation */
{
    struct termentry *t = term;
    int surface;
    struct surface_points *this_plot = NULL;
    int xl, yl;
    /* double ztemp, temp; unused */
    transform_matrix mat;
    int key_count;
    legend_key *key = &keyT;
    char *s, *e;
#ifdef PM3D
    TBOOLEAN can_pm3d = 0;
#endif

    /* Initiate transformation matrix using the global view variables. */
    mat_rot_z(surface_rot_z, trans_mat);
    mat_rot_x(surface_rot_x, mat);
    mat_mult(trans_mat, trans_mat, mat);
    mat_scale(surface_scale / 2.0, surface_scale / 2.0, surface_scale / 2.0, mat);
    mat_mult(trans_mat, trans_mat, mat);

    /* The extrema need to be set even when a surface is not being
     * drawn.   Without this, gnuplot used to assume that the X and
     * Y axis started at zero.   -RKC
     */

    if (polar)
	graph_error("Cannot splot in polar coordinate system.");

    /* done in plot3d.c
     *    if (z_min3d == VERYLARGE || z_max3d == -VERYLARGE ||
     *      x_min3d == VERYLARGE || x_max3d == -VERYLARGE ||
     *      y_min3d == VERYLARGE || y_max3d == -VERYLARGE)
     *      graph_error("all points undefined!");
     */

    /* If we are to draw the bottom grid make sure zmin is updated properly. */
    if (X_AXIS.ticmode || Y_AXIS.ticmode || some_grid_selected()) {
	base_z = Z_AXIS.min
	    - (Z_AXIS.max - Z_AXIS.min) * ticslevel;
	if (ticslevel >= 0)
	    floor_z = base_z;
	else
	    floor_z = Z_AXIS.min;

	if (ticslevel < -1)
	    ceiling_z = base_z;
	else
	    ceiling_z = Z_AXIS.max;
    } else {
	floor_z = base_z = Z_AXIS.min;
	ceiling_z = Z_AXIS.max;
    }

    /*  see comment accompanying similar tests of x_min/x_max and y_min/y_max
     *  in graphics.c:do_plot(), for history/rationale of these tests */
    if (X_AXIS.min == X_AXIS.max)
	graph_error("x_min3d should not equal x_max3d!");
    if (Y_AXIS.min == Y_AXIS.max)
	graph_error("y_min3d should not equal y_max3d!");
    if (Z_AXIS.min == Z_AXIS.max)
	graph_error("z_min3d should not equal z_max3d!");

#ifndef LITE
    /* HBB 20000715: changed meaning of this test. Only warn if _none_
     * of the plotted datasets has grid topology, as that would mean
     * that there is nothing in the plot anything could be hidden
     * behind... */
    if (hidden3d) {
	struct surface_points *plot;
	int p = 0;
	TBOOLEAN any_gridded_dataset = FALSE;

	/* Verify data is hidden line removable - grid based. */
	for (plot = plots; ++p <= pcount; plot = plot->next_sp) {
	    if (!(plot->plot_type == DATA3D && !plot->has_grid_topology)) {
		any_gridded_dataset = TRUE;
	    }
	}

	if (!any_gridded_dataset) {
	    fprintf(stderr, "Notice: No surface grid anything could be hidden behind\n");
	}
    }
#endif /* not LITE */

    term_start_plot();

    screen_ok = FALSE;

    /* now compute boundary for plot (xleft, xright, ytop, ybot) */
    boundary3d(plots, pcount);

    axis_set_graphical_range(FIRST_X_AXIS, xleft, xright);
    axis_set_graphical_range(FIRST_Y_AXIS, ybot, ytop);
    axis_set_graphical_range(FIRST_Z_AXIS, floor_z, ceiling_z);

    /* SCALE FACTORS */
    zscale3d = 2.0 / (ceiling_z - floor_z) * surface_zscale;
    yscale3d = 2.0 / (Y_AXIS.max - Y_AXIS.min);
    xscale3d = 2.0 / (X_AXIS.max - X_AXIS.min);

    term_apply_lp_properties(&border_lp);	/* border linetype */

    /* must come before using draw_3d_graphbox() the first time */
    setup_3d_box_corners();

    /* DRAW GRID AND BORDER */
    /* Original behaviour: draw entire grid in back, if 'set grid back': */
    /* HBB 20040331: but not if in hidden3d mode */
    if (!hidden3d && grid_layer == 0)
	draw_3d_graphbox(plots, pcount, ALLGRID);

#ifdef PM3D
    /* Draw PM3D color key box */
    if (!quick) {
	can_pm3d = is_plot_with_palette() && !make_palette()
	    && term->set_color;
	if (can_pm3d) {
	    draw_color_smooth_box(MODE_SPLOT);
	}
    }
#endif /* PM3D */

#ifdef USE_GRID_LAYERS
    if (!hidden3d && (grid_layer == -1))
	/* Default layering mode.  Draw the back part now, but not if
	 * hidden3d is in use, because that relies on all isolated
	 * lines being output after all surfaces have been defined. */
	draw_3d_graphbox(plots, pcount, BACKGRID);
#endif /* USE_GRID_LAYERS */

    /* PLACE TITLE */
    if (*title.text != 0) {
	unsigned int x, y;
	int tmpx, tmpy;
	if (splot_map) { /* case 'set view map' */
	    unsigned int map_x1, map_y1, map_x2, map_y2;
	    int tics_len = 0;
	    if (X_AXIS.ticmode & TICS_MIRROR) {
		tics_len = (int)(ticscale * (tic_in ? -1 : 1) * (term->v_tic));
		if (tics_len < 0) tics_len = 0; /* take care only about upward tics */
	    }
	    map3d_xy(X_AXIS.min, Y_AXIS.min, base_z, &map_x1, &map_y1);
	    map3d_xy(X_AXIS.max, Y_AXIS.max, base_z, &map_x2, &map_y2);
	    /* Distance between the title base line and graph top line or the upper part of
	       tics is as given by character height: */
	    map3d_position_r(&(title.offset), &tmpx, &tmpy, "3dplot");
#define DEFAULT_Y_DISTANCE 1.0
	    x = (unsigned int) ((map_x1 + map_x2) / 2 + tmpx);
	    y = (unsigned int) (map_y1 + tics_len + tmpy + (DEFAULT_Y_DISTANCE + titlelin - 0.5) * (t->v_char));
#undef DEFAULT_Y_DISTANCE
	} else { /* usual 3d set view ... */
	    map3d_position_r(&(title.offset), &tmpx, &tmpy, "3dplot");
	    x = (unsigned int) ((xleft + xright) / 2 + tmpx);
	    y = (unsigned int) (ytop + tmpy + titlelin * (t->h_char));
    	}
	apply_pm3dcolor(&(title.textcolor),t);
	/* PM: why there is JUST_TOP and not JUST_BOT? We should draw above baseline!
	 * But which terminal understands that? It seems vertical justification does
	 * not work... */
	write_multiline(x, y, title.text, CENTRE, JUST_TOP, 0, title.font);
	reset_textcolor(&(title.textcolor),t);
    }

    /* PLACE TIMEDATE */
    if (*timelabel.text) {
	char str[MAX_LINE_LEN+1];
	time_t now;
	int tmpx, tmpy;
	unsigned int x, y;

	map3d_position_r(&(timelabel.offset), &tmpx, &tmpy, "3dplot");
	x = t->v_char + tmpx;
	y = timelabel_bottom
	    ? yoffset * Y_AXIS.max + tmpy + t->v_char
	    : ytop + tmpy - t->v_char;

	time(&now);
	strftime(str, MAX_LINE_LEN, timelabel.text, localtime(&now));

	if (timelabel_rotate && (*t->text_angle) (TEXT_VERTICAL)) {
	    if (timelabel_bottom)
		write_multiline(x, y, str, LEFT, JUST_TOP, TEXT_VERTICAL, timelabel.font);
	    else
		write_multiline(x, y, str, RIGHT, JUST_TOP, TEXT_VERTICAL, timelabel.font);

	    (*t->text_angle) (0);
	} else {
	    if (timelabel_bottom)
		write_multiline(x, y, str, LEFT, JUST_BOT, 0, timelabel.font);
	    else
		write_multiline(x, y, str, LEFT, JUST_TOP, 0, timelabel.font);
	}
    }

    /* PLACE LABELS */
    place_labels3d(first_label, 0);

    /* PLACE ARROWS */
    place_arrows3d(0);

#ifndef LITE
    if (hidden3d && draw_surface && !quick) {
	init_hidden_line_removal();
	reset_hidden_line_removal();
    }
#endif /* not LITE */

    /* WORK OUT KEY SETTINGS AND DO KEY TITLE / BOX */

    if (key->reverse) {
	key_sample_left = -key_sample_width;
	key_sample_right = 0;
	key_text_left = t->h_char;
	key_text_right = t->h_char * (max_ptitl_len + 1);
	key_size_right = t->h_char * (max_ptitl_len + 2 + key->width_fix);
	key_size_left = t->h_char + key_sample_width;
    } else {
	key_sample_left = 0;
	key_sample_right = key_sample_width;
	key_text_left = -(int) (t->h_char * (max_ptitl_len + 1));
	key_text_right = -(int) t->h_char;
	key_size_left = t->h_char * (max_ptitl_len + 2 + key->width_fix);
	key_size_right = t->h_char + key_sample_width;
    }
    key_point_offset = (key_sample_left + key_sample_right) / 2;

    if (key->flag == KEY_AUTO_PLACEMENT) {
	if (key->vpos == TUNDER) {
#if 0
	    yl = yoffset * t->ymax + (key_rows) * key_entry_height + (ktitle_lines + 2) * t->v_char;
	    xl = max_ptitl_len * 1000 / (key_sample_width / t->h_char + max_ptitl_len + 2);
	    xl *= (xright - xleft) / key_cols;
	    xl /= 1000;
	    xl += xleft;
#else
	    /* HBB 19990608: why calculate these again? boundary3d has already
	     * done it... */
	    if (ptitl_cnt > 0) {
		/* maximise no cols, limited by label-length */
		key_cols = (int) (xright - xleft) / key_col_wth;
		key_rows = (int) (ptitl_cnt + key_cols - 1) / key_cols;
		/* now calculate actual no cols depending on no rows */
		key_cols = (int) (ptitl_cnt + key_rows - 1) / key_rows;
		key_col_wth = (int) (xright - xleft) / key_cols;
		/* we divide into columns, then centre in column by considering
		 * ratio of key_left_size to key_right_size
		 * key_size_left/(key_size_left+key_size_right) * (xright-xleft)/key_cols
		 * do one integer division to maximise accuracy (hope we dont
		 * overflow !)
		 */
		xl = xleft + ((xright - xleft) * key_size_left) / (key_cols * (key_size_left + key_size_right));
		yl = yoffset * t->ymax + (key_rows) * key_entry_height + (ktitle_lines + 2) * t->v_char;
	    }
#endif
	} else {
	    if (key->vpos == TTOP) {
		yl = ytop - t->v_tic - t->v_char;
	    } else {
		yl = ybot + t->v_tic + key_entry_height * key_rows + ktitle_lines * t->v_char;
	    }
	    if (key->hpos == TOUT) {
		/* keys outside plot border (right) */
		xl = xright + t->h_tic + key_size_left;
	    } else if (key->hpos == TLEFT) {
		xl = xleft + t->h_tic + key_size_left;
	    } else {
		xl = xright - key_size_right - key_col_wth * (key_cols - 1);
	    }
	}
	yl_ref = yl - ktitle_lines * t->v_char;
    }
    if (key->flag == KEY_USER_PLACEMENT) {
	map3d_position(&key->user_pos, &xl, &yl, "key");
    }
    if (key->visible && key->box.l_type > L_TYPE_NODRAW) {
	int yt = yl;
	int yb = yl - key_entry_height * (key_rows - ktitle_lines) - ktitle_lines * t->v_char;
	int key_xr = xl + key_col_wth * (key_cols - 1) + key_size_right;
	int tmp = (int)(0.5 * key->height_fix * t->v_char);
	yt += 2 * tmp;
	yl += tmp;

	/* key_rows seems to contain title at this point ??? */
	term_apply_lp_properties(&key->box);
	(*t->move) (xl - key_size_left, yb);
	(*t->vector) (xl - key_size_left, yt);
	(*t->vector) (key_xr, yt);
	(*t->vector) (key_xr, yb);
	(*t->vector) (xl - key_size_left, yb);

	/* draw a horizontal line between key title and first entry  JFi */
	(*t->move) (xl - key_size_left, yt - (ktitle_lines) * t->v_char);
	(*t->vector) (xl + key_size_right, yt - (ktitle_lines) * t->v_char);
    }
    /* DRAW SURFACES AND CONTOURS */

#ifndef LITE
    if (hidden3d && draw_surface && !quick)
	plot3d_hidden(plots, pcount);
#endif /* not LITE */

    /* KEY TITLE */
    if (key->visible && strlen(key->title)) {
	char *ss = gp_alloc(strlen(key->title) + 2, "tmp string ss");
	int tmp = (int)(0.5 * key->height_fix * t->v_char);
	strcpy(ss, key->title);
	strcat(ss, "\n");
	s = ss;
	yl -= t->v_char / 2;
	yl += tmp;

	while ((e = (char *) strchr(s, '\n')) != NULL) {
	    *e = '\0';
	    if (key->just == JLEFT) {
		(*t->justify_text) (LEFT);
		(*t->put_text) (xl + key_text_left, yl, s);
	    } else {
		if ((*t->justify_text) (RIGHT)) {
		    (*t->put_text) (xl + key_text_right,
				    yl, s);
		} else {
		    int x = xl + key_text_right - t->h_char * strlen(s);
		    if (inrange(x, xleft, xright))
			(*t->put_text) (x, yl, s);
		}
	    }
	    s = ++e;
	    yl -= t->v_char;
	}
	yl += t->v_char / 2;
	yl -= tmp;
	free(ss);
    }
    key_count = 0;
    yl_ref = yl -= key_entry_height / 2;	/* centralise the keys */

#define NEXT_KEY_LINE()					\
    if ( ++key_count >= key_rows ) {			\
	yl = yl_ref; xl += key_col_wth; key_count = 0;	\
    } else						\
	yl -= key_entry_height

    this_plot = plots;
    if (!quick)
	for (surface = 0;
	     surface < pcount;
	     this_plot = this_plot->next_sp, surface++) {
#ifdef PM3D
	    /* just an abbreviation */
	    TBOOLEAN use_palette = can_pm3d && this_plot->lp_properties.use_palette;
	    if (can_pm3d && PM3D_IMPLICIT == pm3d.implicit) {
		pm3d_draw_one(this_plot);
	    }
#endif

	    if (draw_surface) {
		TBOOLEAN lkey = (key->visible && this_plot->title && this_plot->title[0]);
		term_apply_lp_properties(&(this_plot->lp_properties));


	    if (lkey) {
	    	/* EAM - force key text to black, then restore */
		(*t->linetype)(LT_BLACK);
		ignore_enhanced_text = this_plot->title_no_enhanced == 1;
		key_text(xl, yl, this_plot->title);
		ignore_enhanced_text = 0;
		term_apply_lp_properties(&(this_plot->lp_properties));
	    }

	    switch (this_plot->plot_style) {
	    case BOXES:	/* can't do boxes in 3d yet so use impulses */
#ifdef PM3D
	    case FILLEDCURVES:
#endif
	    case IMPULSES:
		{
		    if (lkey) {
			key_sample_line(xl, yl);
		    }
		    if (!(hidden3d && draw_surface))
			plot3d_impulses(this_plot);
		    break;
		}
	    case STEPS:	/* HBB: I think these should be here */
	    case FSTEPS:
	    case HISTEPS:
	    case LINES:
		{
		    if (lkey) {
#ifdef PM3D
			if (this_plot->lp_properties.use_palette)
			    key_sample_line_pm3d(this_plot, xl, yl);
			else
#endif
			key_sample_line(xl, yl);
		    }
		    if (!(hidden3d && draw_surface)) {
#ifdef PM3D
			if (use_palette)
			    plot3d_lines_pm3d(this_plot);
			else
#endif
			    plot3d_lines(this_plot);
		    }
		    break;
		}
	    case YERRORLINES:	/* ignored; treat like points */
	    case XERRORLINES:	/* ignored; treat like points */
	    case XYERRORLINES:	/* ignored; treat like points */
	    case YERRORBARS:	/* ignored; treat like points */
	    case XERRORBARS:	/* ignored; treat like points */
	    case XYERRORBARS:	/* ignored; treat like points */
	    case BOXXYERROR:	/* HBB: ignore these as well */
	    case BOXERROR:
	    case CANDLESTICKS:	/* HBB: dito */
	    case FINANCEBARS:
	    case POINTSTYLE:
		if (lkey) {
#ifdef PM3D
		    if (this_plot->lp_properties.use_palette)
			key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
		    else
#endif
		    key_sample_point(xl, yl, this_plot->lp_properties.p_type);
		}
		if (!(hidden3d && draw_surface)) {
#ifdef PM3D
		    if (use_palette)
			plot3d_points_pm3d(this_plot, this_plot->lp_properties.p_type);
		    else
#endif
			plot3d_points(this_plot, this_plot->lp_properties.p_type);
		}
		break;

	    case LINESPOINTS:
		/* put lines */
		if (lkey) {
#ifdef PM3D
			if (this_plot->lp_properties.use_palette)
			    key_sample_line_pm3d(this_plot, xl, yl);
			else
#endif
		    key_sample_line(xl, yl);
		}

		if (!(hidden3d && draw_surface)) {
#ifdef PM3D
		    if (use_palette)
			plot3d_lines_pm3d(this_plot);
		    else
#endif
			plot3d_lines(this_plot);
		}

		/* put points */
		if (lkey) {
#ifdef PM3D
		    if (this_plot->lp_properties.use_palette)
			key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
		    else
#endif
		    key_sample_point(xl, yl, this_plot->lp_properties.p_type);
		}

		if (!(hidden3d && draw_surface)) {
#ifdef PM3D
		    if (use_palette)
			plot3d_points_pm3d(this_plot, this_plot->lp_properties.p_type);
		    else
#endif
			plot3d_points(this_plot, this_plot->lp_properties.p_type);
		}

		break;

	    case DOTS:
		if (lkey) {
#ifdef PM3D
		    if (this_plot->lp_properties.use_palette)
			key_sample_point_pm3d(this_plot, xl, yl, -1);
		    else
#endif
			key_sample_point(xl, yl, -1);
		}

		if (!(hidden3d && draw_surface)) {
#ifdef PM3D
		    if (use_palette)
			plot3d_points_pm3d(this_plot, -1);
		    else
#endif
			plot3d_points(this_plot, -1);
		}

		break;

	    case VECTOR:
		plot3d_vectors(this_plot);
		break;

#ifdef PM3D
	    case PM3DSURFACE:
		if (can_pm3d && PM3D_IMPLICIT != pm3d.implicit) {
		    pm3d_draw_one(this_plot);
		}
		break;
#endif

#ifdef EAM_DATASTRINGS
	    case LABELPOINTS:
		if (!(hidden3d && draw_surface))
		    place_labels3d(this_plot->labels->next, 1);
		break;
#endif
#ifdef EAM_HISTOGRAMS
	    case HISTOGRAMS: /* Cannot happen */
		break;
#endif
#ifdef WITH_IMAGE
	    case IMAGE:
		/* Plot image using projection of 3D plot coordinates to 2D viewing coordinates. */
		SPLOT_IMAGE(this_plot, IC_PALETTE);
		break;

	    case RGBIMAGE:
		/* Plot image using projection of 3D plot coordinates to 2D viewing coordinates. */
		SPLOT_IMAGE(this_plot, IC_RGB);
		break;
#endif
	    }			/* switch(plot-style) */

		/* move key on a line */
		if (lkey) {
		    NEXT_KEY_LINE();
		}
	    }			/* draw_surface */

	    if (draw_contour && this_plot->contours != NULL) {
		struct gnuplot_contours *cntrs = this_plot->contours;
		struct lp_style_type thiscontour_lp_properties =
		    this_plot->lp_properties;

		thiscontour_lp_properties.l_type += (hidden3d ? 1 : 0);

		term_apply_lp_properties(&(thiscontour_lp_properties));

		if (key->visible && this_plot->title && this_plot->title[0]
		    && !draw_surface && !label_contours) {
		    /* unlabelled contours but no surface : put key entry in now */
		    /* EAM - force key text to black, then restore */
		    (*t->linetype)(LT_BLACK);
		    key_text(xl, yl, this_plot->title);
		    term_apply_lp_properties(&(thiscontour_lp_properties));

		    switch (this_plot->plot_style) {
		    case IMPULSES:
		    case LINES:
		    case BOXES:	/* HBB: I think these should be here... */
#ifdef PM3D
		    case FILLEDCURVES:
#endif
		    case VECTOR:
		    case STEPS:
		    case FSTEPS:
		    case HISTEPS:
			key_sample_line(xl, yl);
			break;
		    case YERRORLINES:	/* ignored; treat like points */
		    case XERRORLINES:	/* ignored; treat like points */
		    case XYERRORLINES:	/* ignored; treat like points */
		    case YERRORBARS:	/* ignored; treat like points */
		    case XERRORBARS:	/* ignored; treat like points */
		    case XYERRORBARS:	/* ignored; treat like points */
		    case BOXERROR:	/* HBB: ignore these as well */
		    case BOXXYERROR:
		    case CANDLESTICKS:	/* HBB: dito */
		    case FINANCEBARS:
		    case POINTSTYLE:
#ifdef PM3D
			if (this_plot->lp_properties.use_palette)
			    key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
			else
#endif
			key_sample_point(xl, yl, this_plot->lp_properties.p_type);
			break;
		    case LINESPOINTS:
#ifdef PM3D
			if (this_plot->lp_properties.use_palette)
			    key_sample_line_pm3d(this_plot, xl, yl);
			else
#endif
			key_sample_line(xl, yl);
			break;
		    case DOTS:
#ifdef PM3D
			if (this_plot->lp_properties.use_palette)
			    key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
			else
#endif
			key_sample_point(xl, yl, -1);
			break;
#ifdef PM3D
		    case PM3DSURFACE: /* ignored */
			break;
#endif
#ifdef EAM_HISTOGRAMS
		    case HISTOGRAMS: /* ignored */
			break;
#endif
#ifdef EAM_DATASTRINGS
		    case LABELPOINTS: /* Already handled above */
			break;
#endif
#ifdef WITH_IMAGE
		    case IMAGE:
		    case RGBIMAGE:
			break;
#endif
		    }
		    NEXT_KEY_LINE();
		}
		while (cntrs) {
		    if (label_contours && cntrs->isNewLevel) {
		    	if (key->visible) {
			    (*t->linetype)(LT_BLACK);
			    key_text(xl, yl, cntrs->label);
			}
#ifdef PM3D
			if (use_palette)
			    set_color( cb2gray( z2cb(cntrs->z) ) );
			else
#endif
			    (*t->linetype) (++thiscontour_lp_properties.l_type);

			if (key->visible) {

			    switch (this_plot->plot_style) {
			    case IMPULSES:
			    case LINES:
			    case LINESPOINTS:
			    case BOXES:	/* HBB: these should be treated as well... */
#ifdef PM3D
			    case FILLEDCURVES:
#endif
			    case VECTOR:
			    case STEPS:
			    case FSTEPS:
			    case HISTEPS:
				key_sample_line(xl, yl);
				break;
			    case YERRORLINES:	/* ignored; treat like points */
			    case XERRORLINES:	/* ignored; treat like points */
			    case XYERRORLINES:	/* ignored; treat like points */
			    case YERRORBARS:	/* ignored; treat like points */
			    case XERRORBARS:	/* ignored; treat like points */
			    case XYERRORBARS:	/* ignored; treat like points */
			    case BOXERROR:		/* HBB: treat these likewise */
			    case BOXXYERROR:
			    case CANDLESTICKS:	/* HBB: ditto */
			    case FINANCEBARS:
			    case POINTSTYLE:
#ifdef PM3D
				if (this_plot->lp_properties.use_palette)
				    key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
				else
#endif
				key_sample_point(xl, yl, this_plot->lp_properties.p_type);
				break;
			    case DOTS:
#ifdef PM3D
				if (this_plot->lp_properties.use_palette)
				    key_sample_point_pm3d(this_plot, xl, yl, this_plot->lp_properties.p_type);
				else
#endif
				key_sample_point(xl, yl, -1);
				break;
#ifdef PM3D
			    case PM3DSURFACE: /* ignored */
				break;
#endif
#ifdef EAM_HISTOGRAMS
			    case HISTOGRAMS: /* ignored */
				break;
#endif
#ifdef EAM_DATASTRINGS
			    case LABELPOINTS: /* Already handled above */
				break;
#endif
#ifdef WITH_IMAGE
			    case IMAGE:
			    case RGBIMAGE:
				break;
#endif
			    }	/* switch */

			    NEXT_KEY_LINE();

			} /* key */
		    } /* label_contours */

		    /* now draw the contour */
		    switch (this_plot->plot_style) {
			/* treat boxes like impulses: */
		    case BOXES:
#ifdef PM3D
		    case FILLEDCURVES:
#endif
		    case VECTOR:
		    case IMPULSES:
			cntr3d_impulses(cntrs, &thiscontour_lp_properties);
			break;

		    case STEPS:
		    case FSTEPS:
		    case HISTEPS:
			/* treat all the above like 'lines' */
		    case LINES:
			cntr3d_lines(cntrs, &thiscontour_lp_properties);
			break;

		    case LINESPOINTS:
			cntr3d_lines(cntrs, &thiscontour_lp_properties);
			/* FALLTHROUGH to draw the points */
		    case YERRORLINES:
		    case XERRORLINES:
		    case XYERRORLINES:
		    case YERRORBARS:
		    case XERRORBARS:
		    case XYERRORBARS:
		    case BOXERROR:
		    case BOXXYERROR:
		    case CANDLESTICKS:
		    case FINANCEBARS:
			/* treat all the above like points */
		    case POINTSTYLE:
			cntr3d_points(cntrs, &thiscontour_lp_properties);
			break;
		    case DOTS:
			cntr3d_dots(cntrs);
			break;
#ifdef PM3D
		    case PM3DSURFACE: /* ignored */
			break;
#endif
#ifdef WITH_IMAGE
		    case IMAGE:
		    case RGBIMAGE:
			break;
#endif
#ifdef EAM_HISTOGRAMS 
		    case HISTOGRAMS: /* ignored */
			break;
#endif
#ifdef EAM_DATASTRINGS
		    case LABELPOINTS: /* Already handled above */
			break;
#endif
		    } /*switch */

		    cntrs = cntrs->next;
		} /* loop over contours */
	    } /* draw contours */
	} /* loop over surfaces */

    /* DRAW GRID AND BORDER */
#ifndef USE_GRID_LAYERS
    /* Old behaviour: draw entire grid now, unless it was requested to
     * be in the back. */
    if (grid_layer != 0)
	draw_3d_graphbox(plots, pcount, ALLGRID);
#else
    /* HBB NEW 20040311: do front part now, after surfaces have been
     * output. If "set grid front", or hidden3d is active, must output
     * the whole shebang now, otherwise only the front part. */
    if (hidden3d || grid_layer == 1)
	draw_3d_graphbox(plots, pcount, ALLGRID);
    else if (grid_layer == -1)
	draw_3d_graphbox(plots, pcount, FRONTGRID);

#endif /* USE_GRID_LAYERS */

    /* PLACE LABELS */
    place_labels3d(first_label, 1);

    /* PLACE ARROWS */
    place_arrows3d(1);

#ifdef USE_MOUSE
    /* finally, store the 2d projection of the x and y axis, to enable zooming by mouse */
    {
	unsigned int o_x, o_y, x, y;
	map3d_xy(X_AXIS.min, Y_AXIS.min, base_z, &o_x, &o_y);
	axis3d_o_x = (int)o_x;
	axis3d_o_y = (int)o_y;
	map3d_xy(X_AXIS.max, Y_AXIS.min, base_z, &x, &y);
	axis3d_x_dx = (int)x - axis3d_o_x;
	axis3d_x_dy = (int)y - axis3d_o_y;
	map3d_xy(X_AXIS.min, Y_AXIS.max, base_z, &x, &y);
	axis3d_y_dx = (int)x - axis3d_o_x;
	axis3d_y_dy = (int)y - axis3d_o_y;
    }
#endif

#ifdef PM3D
    /* Release the palette we have made use of. Actually, now it is used only
     * in postscript terminals which write all pm3d plots in between
     * gsave/grestore. Thus, now I'm wondering whether term->previous_palette()
     * is really needed anymore, for any terminal, or could this termentry
     * be removed completely? Any future driver won't need it?
     */
    if (is_plot_with_palette() && term->previous_palette)
	term->previous_palette();
#endif

    term_end_plot();

#ifndef LITE
    if (hidden3d && draw_surface) {
	term_hidden_line_removal();
    }
#endif /* not LITE */
}


/* plot3d_impulses:
 * Plot the surfaces in IMPULSES style
 */
static void
plot3d_impulses(struct surface_points *plot)
{
    int i;				/* point index */
    unsigned int x, y, xx0, yy0;	/* point in terminal coordinates */
    struct iso_curve *icrvs = plot->iso_crvs;

    while (icrvs) {
	struct coordinate GPHUGE *points = icrvs->points;

	for (i = 0; i < icrvs->p_count; i++) {
	    switch (points[i].type) {
	    case INRANGE:
		{
		    map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		    if (inrange(0.0, Z_AXIS.min, Z_AXIS.max)) {
			map3d_xy(points[i].x, points[i].y, 0.0, &xx0, &yy0);
		    } else if (inrange(Z_AXIS.min, 0.0, points[i].z)) {
			map3d_xy(points[i].x, points[i].y, Z_AXIS.min, &xx0, &yy0);
		    } else {
			map3d_xy(points[i].x, points[i].y, Z_AXIS.max, &xx0, &yy0);
		    }

		    clip_move(xx0, yy0);
		    clip_vector(x, y);

		    break;
		}
	    case OUTRANGE:
		{
		    if (!inrange(points[i].x, X_AXIS.min, X_AXIS.max) ||
			!inrange(points[i].y, Y_AXIS.min, Y_AXIS.max))
			break;

		    if (inrange(0.0, Z_AXIS.min, Z_AXIS.max)) {
			/* zero point is INRANGE */
			map3d_xy(points[i].x, points[i].y, 0.0, &xx0, &yy0);

			/* must cross z = Z_AXIS.min or Z_AXIS.max limits */
			if (inrange(Z_AXIS.min, 0.0, points[i].z) &&
			    Z_AXIS.min != 0.0 && Z_AXIS.min != points[i].z) {
			    map3d_xy(points[i].x, points[i].y, Z_AXIS.min, &x, &y);
			} else {
			    map3d_xy(points[i].x, points[i].y, Z_AXIS.max, &x, &y);
			}
		    } else {
			/* zero point is also OUTRANGE */
			if (inrange(Z_AXIS.min, 0.0, points[i].z) &&
			    inrange(Z_AXIS.max, 0.0, points[i].z)) {
			    /* crosses z = Z_AXIS.min or Z_AXIS.max limits */
			    map3d_xy(points[i].x, points[i].y, Z_AXIS.max, &x, &y);
			    map3d_xy(points[i].x, points[i].y, Z_AXIS.min, &xx0, &yy0);
			} else {
			    /* doesn't cross z = Z_AXIS.min or Z_AXIS.max limits */
			    break;
			}
		    }

		    clip_move(xx0, yy0);
		    clip_vector(x, y);

		    break;
		}
	    default:		/* just a safety */
	    case UNDEFINED:{
		    break;
		}
	    }
	}

	icrvs = icrvs->next;
    }
}


/* plot3d_lines:
 * Plot the surfaces in LINES style
 */
/* We want to always draw the lines in the same direction, otherwise when
   we draw an adjacent box we might get the line drawn a little differently
   and we get splotches.  */

static void
plot3d_lines(struct surface_points *plot)
{
    int i;
    unsigned int x, y, xx0, yy0;	/* point in terminal coordinates */
    double clip_x, clip_y, clip_z;
    struct iso_curve *icrvs = plot->iso_crvs;
    struct coordinate GPHUGE *points;
    enum coord_type prev = UNDEFINED;
    double lx[2], ly[2], lz[2];	/* two edge points */

#ifndef LITE
/* These are handled elsewhere.  */
    if (plot->has_grid_topology && hidden3d)
	return;
#endif /* not LITE */

    while (icrvs) {
	prev = UNDEFINED;	/* type of previous plot */

	for (i = 0, points = icrvs->points; i < icrvs->p_count; i++) {
	    switch (points[i].type) {
	    case INRANGE:{
		    map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		    if (prev == INRANGE) {
			clip_vector(x, y);
		    } else {
			if (prev == OUTRANGE) {
			    /* from outrange to inrange */
			    if (!clip_lines1) {
				clip_move(x, y);
			    } else {
				/*
				 * Calculate intersection point and draw
				 * vector from there
				 */
				edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

				map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

				clip_move(xx0, yy0);
				clip_vector(x, y);
			    }
			} else {
			    clip_move(x, y);
			}
		    }

		    break;
		}
	    case OUTRANGE:{
		    if (prev == INRANGE) {
			/* from inrange to outrange */
			if (clip_lines1) {
			    /*
			     * Calculate intersection point and draw
			     * vector to it
			     */

			    edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

			    map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

			    clip_vector(xx0, yy0);
			}
		    } else if (prev == OUTRANGE) {
			/* from outrange to outrange */
			if (clip_lines2) {
			    /*
			     * Calculate the two 3D intersection points
			     * if present
			     */
			    if (two_edge3d_intersect(points, i, lx, ly, lz)) {

				map3d_xy(lx[0], ly[0], lz[0], &x, &y);

				map3d_xy(lx[1], ly[1], lz[1], &xx0, &yy0);

				clip_move(x, y);
				clip_vector(xx0, yy0);
			    }
			}
		    }
		    break;
		}
	    case UNDEFINED:{
		    break;
		}
	    default:
		    graph_error("Unknown point type in plot3d_lines");
	    }

	    prev = points[i].type;
	}

	icrvs = icrvs->next;
    }
}

#ifdef PM3D
/* this is basically the same function as above, but:
 *  - it splits the bunch of scans in two sets corresponding to
 *    the two scan directions.
 *  - reorders the two sets -- from behind to front
 *  - checks if inside on scan of a set the order should be inverted
 */
static void
plot3d_lines_pm3d(struct surface_points *plot)
{
    struct iso_curve** icrvs_pair[2];
    int invert[2];
    int n[2];

    int i, set, scan;
    unsigned int x, y, xx0, yy0;	/* point in terminal coordinates */
    double clip_x, clip_y, clip_z;
    struct coordinate GPHUGE *points;
    enum coord_type prev = UNDEFINED;
    double lx[2], ly[2], lz[2];	/* two edge points */
    double z;

    /* just a shortcut */
    TBOOLEAN color_from_column = plot->pm3d_color_from_column;

#ifndef LITE
/* These are handled elsewhere.  */
    if (plot->has_grid_topology && hidden3d)
	return;
#endif /* not LITE */

    /* split the bunch of scans in two sets in
     * which the scans are already depth ordered */
    pm3d_rearrange_scan_array(plot,
	icrvs_pair, n, invert,
	icrvs_pair + 1, n + 1, invert + 1);

    for (set = 0; set < 2; set++) {

	int begin = 0;
	int step;

	if (invert[set]) {
	    /* begin is set below to the length of the scan - 1 */
	    step = -1;
	} else {
	    step = 1;
	}

	for (scan = 0; scan < n[set] && icrvs_pair[set]; scan++) {

	    int cnt;
	    struct iso_curve *icrvs = icrvs_pair[set][scan];

	    if (invert[set]) {
		begin = icrvs->p_count - 1;
	    }

	    prev = UNDEFINED;	/* type of previous plot */

	    for (cnt = 0, i = begin, points = icrvs->points; cnt < icrvs->p_count; cnt++, i += step) {
		switch (points[i].type) {
		    case INRANGE:
			map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

			if (prev == INRANGE) {
			    if (color_from_column)
				z =  (points[i - step].CRD_COLOR + points[i].CRD_COLOR) * 0.5;
			    else
				z =  (z2cb(points[i - step].z) + z2cb(points[i].z)) * 0.5;
			    set_color( cb2gray(z) );
			    clip_vector(x, y);
			} else {
			    if (prev == OUTRANGE) {
				/* from outrange to inrange */
				if (!clip_lines1) {
				    clip_move(x, y);
				} else {
				    /*
				     * Calculate intersection point and draw
				     * vector from there
				     */
				    edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

				    map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

				    clip_move(xx0, yy0);
				    if (color_from_column)
					z =  (points[i - step].CRD_COLOR + points[i].CRD_COLOR) * 0.5;
				    else
					z =  (z2cb(points[i - step].z) + z2cb(points[i].z)) * 0.5;
				    set_color( cb2gray(z) );
				    clip_vector(x, y);
				}
			    } else {
				clip_move(x, y);
			    }
			}

			break;
		    case OUTRANGE:
			if (prev == INRANGE) {
			    /* from inrange to outrange */
			    if (clip_lines1) {
				/*
				 * Calculate intersection point and draw
				 * vector to it
				 */

				edge3d_intersect(points, i, &clip_x, &clip_y, &clip_z);

				map3d_xy(clip_x, clip_y, clip_z, &xx0, &yy0);

				if (color_from_column)
				    z =  (points[i - step].CRD_COLOR + points[i].CRD_COLOR) * 0.5;
				else
				    z =  (z2cb(points[i - step].z) + z2cb(points[i].z)) * 0.5;
				set_color( cb2gray(z));
				clip_vector(xx0, yy0);
			    }
			} else if (prev == OUTRANGE) {
			    /* from outrange to outrange */
			    if (clip_lines2) {
				/*
				 * Calculate the two 3D intersection points
				 * if present
				 */
				if (two_edge3d_intersect(points, i, lx, ly, lz)) {

				    map3d_xy(lx[0], ly[0], lz[0], &x, &y);

				    map3d_xy(lx[1], ly[1], lz[1], &xx0, &yy0);

				    clip_move(x, y);
				    if (color_from_column)
					z =  (points[i - step].CRD_COLOR + points[i].CRD_COLOR) * 0.5;
				    else
					z =  (z2cb(points[i - step].z) + z2cb(points[i].z)) * 0.5;
				    set_color( cb2gray(z) );
				    clip_vector(xx0, yy0);
				}
			    }
			}
			break;
		    case UNDEFINED:
			break;
		    default:
			graph_error("Unknown point type in plot3d_lines");
		}

		prev = points[i].type;

	    } /* one scan */

	} /* while (icrvs)  */

    } /* for (scan = 0; scan < 2; scan++) */

    if (icrvs_pair[0])
	free(icrvs_pair[0]);
    if (icrvs_pair[1])
	free(icrvs_pair[1]);
}
#endif

/* plot3d_points:
 * Plot the surfaces in POINTSTYLE style
 */
static void
plot3d_points(struct surface_points *plot, int p_type)
{
    int i;
    unsigned int x, y;
    struct termentry *t = term;
    struct iso_curve *icrvs = plot->iso_crvs;

    while (icrvs) {
	struct coordinate GPHUGE *points = icrvs->points;

	for (i = 0; i < icrvs->p_count; i++) {
	    if (points[i].type == INRANGE) {
		map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		if (!clip_point(x, y)) {
		    if (plot->plot_style == POINTSTYLE
		    &&  plot->lp_properties.p_size < 0)
			(*t->pointsize)(pointsize * points[i].CRD_PTSIZE);
		    (*t->point) (x, y, p_type);
		}
	    }
	}

	icrvs = icrvs->next;
    }
}

#ifdef PM3D
static void
plot3d_points_pm3d(struct surface_points *plot, int p_type)
{
    struct iso_curve** icrvs_pair[2];
    int invert[2];
    int n[2];

    int i, set, scan;
    unsigned int x, y;
    struct termentry *t = term;

    /* just a shortcut */
    TBOOLEAN color_from_column = plot->pm3d_color_from_column;

    /* split the bunch of scans in two sets in
     * which the scans are already depth ordered */
    pm3d_rearrange_scan_array(plot,
	icrvs_pair, n, invert,
	icrvs_pair + 1, n + 1, invert + 1);

    for (set = 0; set < 2; set++) {

	int begin = 0;
	int step;

	if (invert[set]) {
	    /* begin is set below to the length of the scan - 1 */
	    step = -1;
	} else {
	    step = 1;
	}

	for (scan = 0; scan < n[set] && icrvs_pair[set]; scan++) {

	    int cnt;
	    struct iso_curve *icrvs = icrvs_pair[set][scan];
	    struct coordinate GPHUGE *points;

	    if (invert[set]) {
		begin = icrvs->p_count - 1;
	    }

	    for (cnt = 0, i = begin, points = icrvs->points; cnt < icrvs->p_count; cnt++, i += step) {

		if (points[i].type == INRANGE) {
		    map3d_xy(points[i].x, points[i].y, points[i].z, &x, &y);

		    if (!clip_point(x, y)) {
			if (color_from_column)
			    set_color( cb2gray(points[i].CRD_COLOR) );
			else
			    set_color( cb2gray( z2cb(points[i].z) ) );
			if (plot->plot_style == POINTSTYLE
			&&  plot->lp_properties.p_size < 0)
			    (*t->pointsize)(pointsize * points[i].CRD_PTSIZE);
			(*t->point) (x, y, p_type);
		    }
		}
	    }
	} /* scan */
    } /* set */
}
#endif


/* cntr3d_impulses:
 * Plot a surface contour in IMPULSES style
 */
static void
cntr3d_impulses(struct gnuplot_contours *cntr, struct lp_style_type *lp)
{
    int i;				/* point index */
    vertex vertex_on_surface, vertex_on_base;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &vertex_on_surface);
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &vertex_on_base);
#ifdef PM3D
	    /* HBB 20010822: Provide correct color-coding for
	     * "linetype palette" PM3D mode */
	    vertex_on_base.real_z = cntr->coords[i].z;
#endif
	    draw3d_line(&vertex_on_surface, &vertex_on_base, lp);
	}
    } else {
	/* Must be on base grid, so do points. */
	cntr3d_points(cntr, lp);
    }
}

/* cntr3d_lines: Plot a surface contour in LINES style */
/* HBB NEW 20031218: changed to use move/vector() style polyline
 * drawing. Gets rid of variable "previous_vertex" */
static void
cntr3d_lines(struct gnuplot_contours *cntr, struct lp_style_type *lp)
{
    int i;			/* point index */
    vertex this_vertex;

    if (draw_contour & CONTOUR_SRF) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, cntr->coords[0].z,
		  &this_vertex);
	/* move slightly frontward, to make sure the contours are
	 * visible in front of the the triangles they're in, if this
	 * is a hidden3d plot */
	if (hidden3d && !VERTEX_IS_UNDEFINED(this_vertex))
	    this_vertex.z += 1e-2;

	polyline3d_start(&this_vertex);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &this_vertex);
	    /* move slightly frontward, to make sure the contours are
	     * visible in front of the the triangles they're in, if this
	     * is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(this_vertex))
		this_vertex.z += 1e-2;
	    polyline3d_next(&this_vertex, lp);
	}
    }

    if (draw_contour & CONTOUR_BASE) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, base_z,
		  &this_vertex);
#ifdef PM3D
	this_vertex.real_z = cntr->coords[0].z;
#endif
	polyline3d_start(&this_vertex);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &this_vertex);
#ifdef PM3D
	    this_vertex.real_z = cntr->coords[i].z;
#endif
	    polyline3d_next(&this_vertex, lp);
	}
    }
}

#if 0 /* HBB UNUSED 20031219 */
/* cntr3d_linespoints:
 * Plot a surface contour in LINESPOINTS style */
static void
cntr3d_linespoints(struct gnuplot_contours *cntr, struct lp_style_type *lp)
{
    int i;			/* point index */
    vertex previous_vertex, this_vertex;

    if (draw_contour & CONTOUR_SRF) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, cntr->coords[0].z,
		  &previous_vertex);
	/* move slightly frontward, to make sure the contours are
	 * visible in front of the the triangles they're in, if this
	 * is a hidden3d plot */
	if (hidden3d && !VERTEX_IS_UNDEFINED(previous_vertex))
	    previous_vertex.z += 1e-2;
	draw3d_point(&previous_vertex, lp);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &this_vertex);
	    /* move slightly frontward, to make sure the contours are
	     * visible in front of the the triangles they're in, if this
	     * is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(this_vertex))
		this_vertex.z += 1e-2;
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    draw3d_point(&this_vertex, lp);
	    previous_vertex = this_vertex;
	}
    }

    if (draw_contour & CONTOUR_BASE) {
	map3d_xyz(cntr->coords[0].x, cntr->coords[0].y, base_z,
		  &previous_vertex);
#ifdef PM3D
	/* HBB 20010822: in "linetype palette" mode,
	 * draw3d_line_unconditional() will look at real_z to
	 * determine the contour line's color --> override base_z by
	 * the actual z position of the contour line */
	previous_vertex.real_z = cntr->coords[0].z;
#endif
	draw3d_point(&previous_vertex, lp);

	for (i = 1; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		      &this_vertex);
#ifdef PM3D
	    /* HBB 20010822: see above */
	    this_vertex.real_z = cntr->coords[i].z;
#endif
	    draw3d_line(&previous_vertex, &this_vertex, lp);
	    draw3d_point(&this_vertex, lp);
	    previous_vertex=this_vertex;
	}
    }
}
#endif

/* cntr3d_points:
 * Plot a surface contour in POINTSTYLE style
 */
static void
cntr3d_points(struct gnuplot_contours *cntr, struct lp_style_type *lp)
{
    int i;
    vertex v;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z,
		      &v);
	    /* move slightly frontward, to make sure the contours and
	     * points are visible in front of the triangles they're
	     * in, if this is a hidden3d plot */
	    if (hidden3d && !VERTEX_IS_UNDEFINED(v))
		v.z += 1e-2;
	    draw3d_point(&v, lp);
	}
    }
    if (draw_contour & CONTOUR_BASE) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xyz(cntr->coords[i].x, cntr->coords[i].y, base_z,
		     &v);
#ifdef PM3D
	    /* HBB 20010822: see above */
	    v.real_z = cntr->coords[i].z;
#endif
	    draw3d_point(&v, lp);
	}
    }
}

/* cntr3d_dots:
 * Plot a surface contour in DOTS style
 */
/* FIXME HBB 20000621: this is the only contour output routine left
 * without hiddenlining. It should probably deleted altogether, and
 * its call replaced by one of cntr3d_points */
static void
cntr3d_dots(struct gnuplot_contours *cntr)
{
    int i;
    unsigned int x, y;
    struct termentry *t = term;

    if (draw_contour & CONTOUR_SRF) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xy(cntr->coords[i].x, cntr->coords[i].y, cntr->coords[i].z, &x, &y);

	    if (!clip_point(x, y))
		(*t->point) (x, y, -1);
	}
    }
    if (draw_contour & CONTOUR_BASE) {
	for (i = 0; i < cntr->num_pts; i++) {
	    map3d_xy(cntr->coords[i].x, cntr->coords[i].y, base_z,
		     &x, &y);

	    if (!clip_point(x, y))
		(*t->point) (x, y, -1);
	}
    }
}


/* map xmin | xmax to 0 | 1 and same for y
 * 0.1 avoids any rounding errors
 */
#define MAP_HEIGHT_X(x) ( (int) (((x)-X_AXIS.min)/(X_AXIS.max-X_AXIS.min)+0.1) )
#define MAP_HEIGHT_Y(y) ( (int) (((y)-Y_AXIS.min)/(Y_AXIS.max-Y_AXIS.min)+0.1) )

/* if point is at corner, update height[][] and depth[][]
 * we are still assuming that extremes of surfaces are at corners,
 * but we are not assuming order of corners
 */
static void
check_corner_height(
    struct coordinate GPHUGE *p,
    double height[2][2], double depth[2][2])
{
    if (p->type != INRANGE)
	return;
    /* FIXME HBB 20010121: don't compare 'zero' to data values in
     * absolute terms. */
    if ((fabs(p->x - X_AXIS.min) < zero || fabs(p->x - X_AXIS.max) < zero) &&
	(fabs(p->y - Y_AXIS.min) < zero || fabs(p->y - Y_AXIS.max) < zero)) {
	unsigned int x = MAP_HEIGHT_X(p->x);
	unsigned int y = MAP_HEIGHT_Y(p->y);
	if (height[x][y] < p->z)
	    height[x][y] = p->z;
	if (depth[x][y] > p->z)
	    depth[x][y] = p->z;
    }
}

/* work out where the axes and tics are drawn */
static void
setup_3d_box_corners()
{
    int quadrant = surface_rot_z / 90;
    if ((quadrant + 1) & 2) {
	zaxis_x = X_AXIS.max;
	right_x = X_AXIS.min;
	back_y  = Y_AXIS.min;
	front_y  = Y_AXIS.max;
    } else {
	zaxis_x = X_AXIS.min;
	right_x = X_AXIS.max;
	back_y  = Y_AXIS.max;
	front_y  = Y_AXIS.min;
    }

    if (quadrant & 2) {
	zaxis_y = Y_AXIS.max;
	right_y = Y_AXIS.min;
	back_x  = X_AXIS.max;
	front_x  = X_AXIS.min;
    } else {
	zaxis_y = Y_AXIS.min;
	right_y = Y_AXIS.max;
	back_x  = X_AXIS.min;
	front_x  = X_AXIS.max;
    }

    if (surface_rot_x > 90) {
	/* labels on the back axes */
	yaxis_x = back_x;
	xaxis_y = back_y;
    } else {
	yaxis_x = front_x;
	xaxis_y = front_y;
    }
}

/* Draw all elements of the 3d graph box, including borders, zeroaxes,
 * tics, gridlines, ticmarks, axis labels and the base plane. */
static void
draw_3d_graphbox(struct surface_points *plot, int plot_num, WHICHGRID whichgrid)
{
    unsigned int x, y;		/* point in terminal coordinates */
    struct termentry *t = term;

    if (draw_border) {
	/* the four corners of the base plane, in normalized view
	 * coordinates (-1..1) on all three axes. */
	vertex bl, bb, br, bf;

	/* map to normalized view coordinates the corners of the
	 * baseplane: left, back, right and front, in that order: */
	map3d_xyz(zaxis_x, zaxis_y, base_z, &bl);
	map3d_xyz(back_x , back_y , base_z, &bb);
	map3d_xyz(right_x, right_y, base_z, &br);
	map3d_xyz(front_x, front_y, base_z, &bf);

#ifdef USE_GRID_LAYERS
	if (BACKGRID != whichgrid)
#endif
	{
	    /* Draw front part of base grid, right to front corner: */
	    if (draw_border & 4)
		draw3d_line(&br, &bf, &border_lp);
	    /* ... and left to front: */
	    if (draw_border & 1)
		draw3d_line(&bl, &bf, &border_lp);
	}
#ifdef USE_GRID_LAYERS
	if (FRONTGRID != whichgrid)
#endif
	{
	    /* Draw back part of base grid: left to back corner: */
	    if (draw_border & 2)
		draw3d_line(&bl, &bb, &border_lp);
	    /* ... and right to back: */
	    if (draw_border & 8)
		draw3d_line(&br, &bb, &border_lp);
	}

	/* if surface is drawn, draw the rest of the graph box, too: */
	if (draw_surface || (draw_contour & CONTOUR_SRF)
#ifdef PM3D
	    || strpbrk(pm3d.where,"st") != NULL
#endif
	   ) {
	    vertex fl, fb, fr, ff; /* floor left/back/right/front corners */
	    vertex tl, tb, tr, tf; /* top left/back/right/front corners */

	    map3d_xyz(zaxis_x, zaxis_y, floor_z, &fl);
	    map3d_xyz(back_x , back_y , floor_z, &fb);
	    map3d_xyz(right_x, right_y, floor_z, &fr);
	    map3d_xyz(front_x, front_y, floor_z, &ff);

	    map3d_xyz(zaxis_x, zaxis_y, ceiling_z, &tl);
	    map3d_xyz(back_x , back_y , ceiling_z, &tb);
	    map3d_xyz(right_x, right_y, ceiling_z, &tr);
	    map3d_xyz(front_x, front_y, ceiling_z, &tf);

	    if ((draw_border & 0xf0) == 0xf0) {
		/* all four verticals are drawn - save some time by
		 * drawing them to the full height, regardless of
		 * where the surface lies */
#ifdef USE_GRID_LAYERS
		if (FRONTGRID != whichgrid) {
#endif
		    /* Draw the back verticals floor-to-ceiling, left: */
		    draw3d_line(&fl, &tl, &border_lp);
		    /* ... back: */
		    draw3d_line(&fb, &tb, &border_lp);
		    /* ... and right */
		    draw3d_line(&fr, &tr, &border_lp);
#ifdef USE_GRID_LAYERS
		}
		if (BACKGRID != whichgrid)
#endif
		    /* Draw the front vertical: floor-to-ceiling, front: */
		    draw3d_line(&ff, &tf, &border_lp);
	    } else {
		/* find heights of surfaces at the corners of the xy
		 * rectangle */
		double height[2][2];
		double depth[2][2];
		unsigned int zaxis_i = MAP_HEIGHT_X(zaxis_x);
		unsigned int zaxis_j = MAP_HEIGHT_Y(zaxis_y);
		unsigned int back_i = MAP_HEIGHT_X(back_x);
		unsigned int back_j = MAP_HEIGHT_Y(back_y);

		height[0][0] = height[0][1]
		    = height[1][0] = height[1][1] = base_z;
		depth[0][0] = depth[0][1]
		    = depth[1][0] = depth[1][1] = base_z;

		/* FIXME HBB 20000617: this method contains the
		 * assumption that the topological corners of the
		 * surface mesh(es) are also the geometrical ones of
		 * their xy projections. This is only true for
		 * 'explicit' surface datasets, i.e. z(x,y) */
		for (; --plot_num >= 0; plot = plot->next_sp) {
		    struct iso_curve *curve = plot->iso_crvs;
		    int count = curve->p_count;
		    int iso;
		    if (plot->plot_type == DATA3D) {
			if (!plot->has_grid_topology)
			    continue;
			iso = plot->num_iso_read;
		    } else
			iso = iso_samples_2;

		    check_corner_height(curve->points, height, depth);
		    check_corner_height(curve->points + count - 1, height, depth);
		    while (--iso)
			curve = curve->next;
		    check_corner_height(curve->points, height, depth);
		    check_corner_height(curve->points + count - 1, height, depth);
		}

#define VERTICAL(mask,x,y,i,j,bottom,top)			\
		if (draw_border&mask) {				\
		    draw3d_line(bottom,top, &border_lp);	\
		} else if (height[i][j] != depth[i][j]) {	\
		    vertex a, b;				\
		    map3d_xyz(x,y,depth[i][j],&a);		\
		    map3d_xyz(x,y,height[i][j],&b);		\
		    draw3d_line(&a, &b, &border_lp);		\
		}

#ifdef USE_GRID_LAYERS
		if (FRONTGRID != whichgrid) {
#endif
		    /* Draw back verticals: floor-to-ceiling left: */
		    VERTICAL(0x10, zaxis_x, zaxis_y, zaxis_i, zaxis_j, &fl, &tl);
		    /* ... back: */
		    VERTICAL(0x20, back_x, back_y, back_i, back_j, &fb, &tb);
		    /* ... and right: */
		    VERTICAL(0x40, right_x, right_y, 1 - zaxis_i, 1 - zaxis_j,
			     &fr, &tr);
#ifdef USE_GRID_LAYERS
		}
		if (BACKGRID != whichgrid) {
#endif
		    /* Draw front verticals: floor-to-ceiling front */
		    VERTICAL(0x80, front_x, front_y, 1 - back_i, 1 - back_j,
			     &ff, &tf);
#ifdef USE_GRID_LAYERS
		}
#endif
#undef VERTICAL
	    } /* else (all 4 verticals drawn?) */

	    /* now border lines on top */
#ifdef USE_GRID_LAYERS
	    if (FRONTGRID != whichgrid)
#endif
	    {
		/* Draw back part of top of box: top left to back corner: */
		if (draw_border & 0x100)
		    draw3d_line(&tl, &tb, &border_lp);
		/* ... and top right to back: */
		if (draw_border & 0x200)
		    draw3d_line(&tr, &tb, &border_lp);
	    }
#ifdef USE_GRID_LAYERS
	    if (BACKGRID != whichgrid)
#endif
	    {
		/* Draw front part of top of box: top left to front corner: */
		if (draw_border & 0x400)
		    draw3d_line(&tl, &tf, &border_lp);
		/* ... and top right to front: */
		if (draw_border & 0x800)
		    draw3d_line(&tr, &tf, &border_lp);
	    }
	} /* else (surface is drawn) */
    } /* if (draw_border) */

    /* Draw ticlabels and axis labels. x axis, first:*/
    if (X_AXIS.ticmode || *X_AXIS.label.text) {
	vertex v0, v1;
	double other_end =
	    Y_AXIS.min + Y_AXIS.max - xaxis_y;
	double mid_x =
	    (X_AXIS.max + X_AXIS.min) / 2;

	map3d_xyz(mid_x, xaxis_y, base_z, &v0);
	map3d_xyz(mid_x, other_end, base_z, &v1);

	tic_unitx = (v1.x - v0.x) / (double)yscaler;
	tic_unity = (v1.y - v0.y) / (double)yscaler;
	tic_unitz = (v1.z - v0.z) / (double)yscaler;

#ifdef USE_GRID_LAYERS
	/* Don't output tics and grids if this is the front part of a
	 * two-part grid drawing process: */
	if ((surface_rot_x <= 90 && FRONTGRID != whichgrid) ||
	    (surface_rot_x > 90 && BACKGRID != whichgrid))
#endif
	if (X_AXIS.ticmode) {
	    gen_tics(FIRST_X_AXIS, xtick_callback);
	}

	if (*X_AXIS.label.text) {
	    /* label at xaxis_y + 1/4 of (xaxis_y-other_y) */
#ifdef USE_GRID_LAYERS /* FIXME: still needed??? what for? */
	    if ((surface_rot_x <= 90 && BACKGRID != whichgrid) ||
		(surface_rot_x > 90 && FRONTGRID != whichgrid)) {
#endif
	    unsigned int x1, y1;
	    int tmpx, tmpy;

	    if (splot_map) { /* case 'set view map' */
		/* copied from xtick_callback(): baseline of tics labels */
		vertex v1, v2;
		map3d_xyz(mid_x, xaxis_y, base_z, &v1);
		v2.x = v1.x;
		v2.y = v1.y - tic_unity * t->v_char * 1;
		if (!tic_in) {
		    /* FIXME
		     * This code and its source in xtick_callback() is wrong --- tics
		     * can be "in" but ticscale <0 ! To be corrected in both places!
		     */
		    v2.y -= tic_unity * t->v_tic * ticscale;
		}
#if 0
		/* PM: correct implementation for map should probably be like this: */
		if (X_AXIS.ticmode) {
		    int tics_len = (int)(ticscale * (tic_in ? -1 : 1) * (term->v_tic));
		    if (tics_len < 0) tics_len = 0; /* take care only about upward tics */
		    v2.y += tics_len;
		}
#endif
		TERMCOORD(&v2, x1, y1);
		/* DEFAULT_Y_DISTANCE is with respect to baseline of tics labels */
#define DEFAULT_Y_DISTANCE 0.5
		y1 -= (unsigned int) ((1 + DEFAULT_Y_DISTANCE) * t->v_char);
#undef DEFAULT_Y_DISTANCE
	    } else { /* usual 3d set view ... */
    		double step = (xaxis_y - other_end) / 4;

		map3d_xyz(mid_x, xaxis_y + step, base_z, &v1);
		if (!tic_in) {
		    v1.x -= tic_unitx * ticscale * t->v_tic;
		    v1.y -= tic_unity * ticscale * t->v_tic;
		}
		TERMCOORD(&v1, x1, y1);
	    }

	    map3d_position_r(&(X_AXIS.label.offset), &tmpx, &tmpy, "graphbox");
	    x1 += tmpx; /* user-defined label offset */
	    y1 += tmpy;
	    apply_pm3dcolor(&(X_AXIS.label.textcolor),t);
	    write_multiline(x1, y1, X_AXIS.label.text,
			    CENTRE, JUST_TOP, 0,
			    X_AXIS.label.font);
	    reset_textcolor(&(X_AXIS.label.textcolor),t);
#ifdef USE_GRID_LAYERS
	    }
#endif
	}
    }

    /* y axis: */
    if (Y_AXIS.ticmode || *Y_AXIS.label.text) {
	vertex v0, v1;
	double other_end =
	    X_AXIS.min + X_AXIS.max - yaxis_x;
	double mid_y =
	    (Y_AXIS.max + Y_AXIS.min) / 2;

	map3d_xyz(yaxis_x, mid_y, base_z, &v0);
	map3d_xyz(other_end, mid_y, base_z, &v1);

	tic_unitx = (v1.x - v0.x) / (double)xscaler;
	tic_unity = (v1.y - v0.y) / (double)xscaler;
	tic_unitz = (v1.z - v0.z) / (double)xscaler;

#ifdef USE_GRID_LAYERS
	/* Don't output tics and grids if this is the front part of a
	 * two-part grid drawing process: */
	if ((surface_rot_x <= 90 && FRONTGRID != whichgrid) ||
	    (surface_rot_x > 90 && BACKGRID != whichgrid))
#endif
	if (Y_AXIS.ticmode) {
	    gen_tics(FIRST_Y_AXIS, ytick_callback);
	}
	if (*Y_AXIS.label.text) {
#ifdef USE_GRID_LAYERS
	    if ((surface_rot_x <= 90 && BACKGRID != whichgrid) ||
		(surface_rot_x > 90 && FRONTGRID != whichgrid)) {
#endif
		unsigned int x1, y1;
		int tmpx, tmpy;
		int h_just, v_just, angle;

		if (splot_map) { /* case 'set view map' */
		    /* copied from ytick_callback(): baseline of tics labels */
		    vertex v1, v2;
		    map3d_xyz(yaxis_x, mid_y, base_z, &v1);
		    if (Y_AXIS.ticmode & TICS_ON_AXIS
			    && !X_AXIS.log
			    && inrange (0.0, X_AXIS.min, X_AXIS.max)
		       ) {
			map3d_xyz(0.0, yaxis_x, base_z, &v1);
		    }
		    v2.x = v1.x - tic_unitx * t->h_char * 1;
		    v2.y = v1.y;
	    	    if (!tic_in)
			v2.x -= tic_unitx * t->h_tic * ticscale;
		    TERMCOORD(&v2, x1, y1);
		    /* calculate max length of y-tics labels */
		    widest_tic_strlen = 0;
		    if (Y_AXIS.ticmode & TICS_ON_BORDER) {
			widest_tic_strlen = 0; /* reset the global variable */
		      	gen_tics(FIRST_Y_AXIS, widest_tic_callback);
		    }
		    /* DEFAULT_Y_DISTANCE is with respect to baseline of tics labels */
#define DEFAULT_X_DISTANCE 0.
		    x1 -= (unsigned int) ((DEFAULT_X_DISTANCE + 0.5 + widest_tic_strlen) * t->h_char);
#undef DEFAULT_X_DISTANCE
#if 0
		    /* another method ... but not compatible */
		    unsigned int map_y1, map_x2, map_y2;
		    int tics_len = 0;
		    if (Y_AXIS.ticmode) {
			tics_len = (int)(ticscale * (tic_in ? 1 : -1) * (term->v_tic));
			if (tics_len > 0) tics_len = 0; /* take care only about left tics */
		    }
		    map3d_xy(X_AXIS.min, Y_AXIS.min, base_z, &x1, &map_y1);
		    map3d_xy(X_AXIS.max, Y_AXIS.max, base_z, &map_x2, &map_y2);
		    y1 = (unsigned int)((map_y1 + map_y2) * 0.5);
		    /* Distance between the title base line and graph top line or the upper part of
	 	       tics is as given by character height: */
#define DEFAULT_X_DISTANCE 0
		    x1 += (unsigned int) (tics_len + (-0.5 + Y_AXIS.label.xoffset) * t->h_char);
		    y1 += (unsigned int) ((DEFAULT_X_DISTANCE + Y_AXIS.label.yoffset) * t->v_char);
#undef DEFAULT_X_DISTANCE
#endif
		    h_just = CENTRE; /* vertical justification for rotated text */
		    v_just = JUST_BOT; /* horizontal -- does not work for rotated text? */
		    angle = TEXT_VERTICAL; /* seems it has not effect ... why? */
		} else { /* usual 3d set view ... */
		    double step = (other_end - yaxis_x) / 4;
		map3d_xyz(yaxis_x - step, mid_y, base_z, &v1);
		if (!tic_in) {
		    v1.x -= tic_unitx * ticscale * t->h_tic;
		    v1.y -= tic_unity * ticscale * t->h_tic;
		}
		TERMCOORD(&v1, x1, y1);
		    h_just = CENTRE;
		    v_just = JUST_TOP;
		    angle = 0;
		}

		map3d_position_r(&(Y_AXIS.label.offset), &tmpx, &tmpy, "graphbox");
		x1 += tmpx; /* user-defined label offset */
		y1 += tmpy;
		/* vertical y-label for maps */
		if (splot_map == TRUE)
		    (*t->text_angle)(TEXT_VERTICAL);
		/* write_multiline mods it */
		apply_pm3dcolor(&(Y_AXIS.label.textcolor),t);
		write_multiline(x1, y1, Y_AXIS.label.text,
				h_just, v_just, angle,
				Y_AXIS.label.font);
		reset_textcolor(&(Y_AXIS.label.textcolor),t);
		/* revert from vertical y-label for maps */
		if (splot_map == TRUE)
		    (*t->text_angle)(0);
#ifdef USE_GRID_LAYERS
	    }
#endif
	}
    }

    /* do z tics */
    if (Z_AXIS.ticmode
#ifdef USE_GRID_LAYERS
	/* Don't output tics and grids if this is the front part of a
	 * two-part grid drawing process: */
	&& (FRONTGRID != whichgrid)
#endif
	&& (splot_map == FALSE)
	&& (draw_surface
	    || (draw_contour & CONTOUR_SRF)
#ifdef PM3D
	    || strchr(pm3d.where,'s') != NULL
#endif
	    )
	) {
	gen_tics(FIRST_Z_AXIS, ztick_callback);
    }
    if ((Y_AXIS.zeroaxis.l_type > L_TYPE_NODRAW)
	&& !X_AXIS.log
	&& inrange(0, X_AXIS.min, X_AXIS.max)
	) {
	vertex v1, v2;

	/* line through x=0 */
	map3d_xyz(0.0, Y_AXIS.min, base_z, &v1);
	map3d_xyz(0.0, Y_AXIS.max, base_z, &v2);
	draw3d_line(&v1, &v2, &Y_AXIS.zeroaxis);
    }
    if ((X_AXIS.zeroaxis.l_type > L_TYPE_NODRAW)
	&& !Y_AXIS.log
	&& inrange(0, Y_AXIS.min, Y_AXIS.max)
	) {
	vertex v1, v2;

	term_apply_lp_properties(&X_AXIS.zeroaxis);
	/* line through y=0 */
	map3d_xyz(X_AXIS.min, 0.0, base_z, &v1);
	map3d_xyz(X_AXIS.max, 0.0, base_z, &v2);
	draw3d_line(&v1, &v2, &X_AXIS.zeroaxis);
    }
    /* PLACE ZLABEL - along the middle grid Z axis - eh ? */
    if (*Z_AXIS.label.text
	&& (draw_surface
	    || (draw_contour & CONTOUR_SRF)
#ifdef PM3D
	    || strpbrk(pm3d.where,"st") != NULL
#endif
	    )
	) {
	int tmpx, tmpy;
	map3d_xy(zaxis_x, zaxis_y,
		 Z_AXIS.max
		 + (Z_AXIS.max - base_z) / 4,
		 &x, &y);

	map3d_position_r(&(Z_AXIS.label.offset), &tmpx, &tmpy, "graphbox");
	x += tmpx;
	y += tmpy;

	apply_pm3dcolor(&(Z_AXIS.label.textcolor),t);
	write_multiline(x, y, Z_AXIS.label.text,
			CENTRE, CENTRE, 0,
			Z_AXIS.label.font);
	reset_textcolor(&(Z_AXIS.label.textcolor),t);
    }
}

/* HBB 20010118: all the *_callback() functions made non-static. This
 * is necessary to work around a bug in HP's assembler shipped with
 * HP-UX 10 and higher, if GCC tries to use it */

void
xtick_callback(
    AXIS_INDEX axis,
    double place,
    char *text,
    struct lp_style_type grid)	/* linetype or -2 for none */
{
    vertex v1, v2;
    double scale = (text ? ticscale : miniticscale) * (tic_in ? 1 : -1);
    double other_end =
	Y_AXIS.min + Y_AXIS.max - xaxis_y;
    struct termentry *t = term;

    (void) axis;		/* avoid -Wunused warning */

    map3d_xyz(place, xaxis_y, base_z, &v1);
    if (grid.l_type > L_TYPE_NODRAW) {
	/* to save mapping twice, map non-axis y */
	map3d_xyz(place, other_end, base_z, &v2);
	draw3d_line(&v1, &v2, &grid);
    }
    if ((X_AXIS.ticmode & TICS_ON_AXIS)
	&& !Y_AXIS.log
	&& inrange (0.0, Y_AXIS.min, Y_AXIS.max)
	) {
	map3d_xyz(place, 0.0, base_z, &v1);
    }
    v2.x = v1.x + tic_unitx * scale * t->v_tic ;
    v2.y = v1.y + tic_unity * scale * t->v_tic ;
    v2.z = v1.z + tic_unitz * scale * t->v_tic ;
#ifdef PM3D
    v2.real_z = v1.real_z;
#endif
    draw3d_line(&v1, &v2, &border_lp);
    term_apply_lp_properties(&border_lp);

    if (text) {
	int just;
	unsigned int x2, y2;
	/* get offset */
	int offsetx, offsety;
	map3d_position_r(&(axis_array[axis].ticdef.offset),
		       &offsetx, &offsety, "xtics");
	if (tic_unitx * xscaler < -0.9)
	    just = LEFT;
	else if (tic_unitx * xscaler < 0.9)
	    just = CENTRE;
	else
	    just = RIGHT;
	v2.x = v1.x - tic_unitx * t->h_char * 1;
	v2.y = v1.y - tic_unity * t->v_char * 1;
	if (!tic_in) {
	    v2.x -= tic_unitx * t->v_tic * ticscale;
	    v2.y -= tic_unity * t->v_tic * ticscale;
	}
	TERMCOORD(&v2, x2, y2);
        /* User-specified different color for the tics text */
	if (axis_array[axis].ticdef.textcolor.lt != TC_DEFAULT)
	    apply_pm3dcolor(&(axis_array[axis].ticdef.textcolor), t);
	clip_put_text_just(x2+offsetx, y2+offsety, text,
			   just, JUST_TOP,
			   axis_array[axis].ticdef.font);
	term_apply_lp_properties(&border_lp);
    }

    if (X_AXIS.ticmode & TICS_MIRROR) {
	map3d_xyz(place, other_end, base_z, &v1);
	v2.x = v1.x - tic_unitx * scale * t->v_tic;
	v2.y = v1.y - tic_unity * scale * t->v_tic;
	v2.z = v1.z - tic_unitz * scale * t->v_tic;
#ifdef PM3D
	v2.real_z = v1.real_z;
#endif
	draw3d_line(&v1, &v2, &border_lp);
    }
}

void
ytick_callback(
    AXIS_INDEX axis,
    double place,
    char *text,
    struct lp_style_type grid)
{
    vertex v1, v2;
    double scale = (text ? ticscale : miniticscale) * (tic_in ? 1 : -1);
    double other_end =
	X_AXIS.min + X_AXIS.max - yaxis_x;
    struct termentry *t = term;

    (void) axis;		/* avoid -Wunused warning */

    map3d_xyz(yaxis_x, place, base_z, &v1);
    if (grid.l_type > L_TYPE_NODRAW) {
	map3d_xyz(other_end, place, base_z, &v2);
	draw3d_line(&v1, &v2, &grid);
    }
    if (Y_AXIS.ticmode & TICS_ON_AXIS
	&& !X_AXIS.log
	&& inrange (0.0, X_AXIS.min, X_AXIS.max)
	) {
	map3d_xyz(0.0, place, base_z, &v1);
    }

    v2.x = v1.x + tic_unitx * scale * t->h_tic;
    v2.y = v1.y + tic_unity * scale * t->h_tic;
    v2.z = v1.z + tic_unitz * scale * t->h_tic;
#ifdef PM3D
    v2.real_z = v1.real_z;
#endif
    draw3d_line(&v1, &v2, &border_lp);

    if (text) {
	int just;
	unsigned int x2, y2;
	/* get offset */
	int offsetx, offsety;
	map3d_position_r(&(axis_array[axis].ticdef.offset),
		       &offsetx, &offsety, "ytics");

	if (tic_unitx * xscaler < -0.9)
	    just = LEFT;
	else if (tic_unitx * xscaler < 0.9)
	    just = CENTRE;
	else
	    just = RIGHT;
	v2.x = v1.x - tic_unitx * t->h_char * 1;
	v2.y = v1.y - tic_unity * t->v_char * 1;
	if (!tic_in) {
	    v2.x -= tic_unitx * t->h_tic * ticscale;
	    v2.y -= tic_unity * t->v_tic * ticscale;
	}
        /* User-specified different color for the tics text */
	if (axis_array[axis].ticdef.textcolor.lt != TC_DEFAULT)
	    apply_pm3dcolor(&(axis_array[axis].ticdef.textcolor), t);
	TERMCOORD(&v2, x2, y2);
	clip_put_text_just(x2+offsetx, y2+offsety, text, 
			   just, JUST_TOP,
			   axis_array[axis].ticdef.font);
	term_apply_lp_properties(&border_lp);
    }

    if (Y_AXIS.ticmode & TICS_MIRROR) {
	map3d_xyz(other_end, place, base_z, &v1);
	v2.x = v1.x - tic_unitx * scale * t->h_tic;
	v2.y = v1.y - tic_unity * scale * t->h_tic;
	v2.z = v1.z - tic_unitz * scale * t->h_tic;
#ifdef PM3D
	v2.real_z = v1.real_z;
#endif
	draw3d_line(&v1, &v2, &border_lp);
    }
}

void
ztick_callback(
    AXIS_INDEX axis,
    double place,
    char *text,
    struct lp_style_type grid)
{
    /* HBB: inserted some ()'s to shut up gcc -Wall, here and below */
    int len = (text ? ticscale : miniticscale)
	* (tic_in ? 1 : -1) * (term->h_tic);
    vertex v1, v2, v3;

    (void) axis;		/* avoid -Wunused warning */

    map3d_xyz(zaxis_x, zaxis_y, place, &v1);
    if (grid.l_type > L_TYPE_NODRAW) {
	map3d_xyz(back_x, back_y, place, &v2);
	map3d_xyz(right_x, right_y, place, &v3);
	draw3d_line(&v1, &v2, &grid);
	draw3d_line(&v2, &v3, &grid);
    }
    v2.x = v1.x + len / (double)xscaler;
    v2.y = v1.y;
    v2.z = v1.z;
#ifdef PM3D
    v2.real_z = v1.real_z;
#endif
    draw3d_line(&v1, &v2, &border_lp);

    if (text) {
	unsigned int x1, y1;
	/* get offset */
	int offsetx, offsety;
	map3d_position_r(&(axis_array[axis].ticdef.offset),
		       &offsetx, &offsety, "ztics");

	TERMCOORD(&v1, x1, y1);
	x1 -= (term->h_tic) * 2;
	if (!tic_in)
	    x1 -= (term->h_tic) * ticscale;
        /* User-specified different color for the tics text */
	if (axis_array[axis].ticdef.textcolor.lt != TC_DEFAULT)
	    apply_pm3dcolor(&(axis_array[axis].ticdef.textcolor), term);
	clip_put_text_just(x1+offsetx, y1+offsety, text,
			   RIGHT, JUST_CENTRE,
			   axis_array[axis].ticdef.font);
	term_apply_lp_properties(&border_lp);
    }

    if (Z_AXIS.ticmode & TICS_MIRROR) {
	map3d_xyz(right_x, right_y, place, &v1);
	v2.x = v1.x - len / (double)xscaler;
	v2.y = v1.y;
	v2.z = v1.z;
#ifdef PM3D
	v2.real_z = v1.real_z;
#endif
	draw3d_line(&v1, &v2, &border_lp);
    }
}

static int
map3d_getposition(
    struct position *pos,
    const char *what,
    double *xpos, double *ypos, double *zpos)
{
    TBOOLEAN screen_coords = FALSE;
    TBOOLEAN char_coords = FALSE;
    TBOOLEAN plot_coords = FALSE;

    switch (pos->scalex) {
    case first_axes:
    case second_axes:
	*xpos = axis_log_value_checked(FIRST_X_AXIS, *xpos, what);
	plot_coords = TRUE;
	break;
    case graph:
	*xpos = X_AXIS.min + *xpos * (X_AXIS.max - X_AXIS.min);
	plot_coords = TRUE;
	break;
    case screen:
	*xpos = *xpos * (term->xmax -1) + 0.5;
	screen_coords = TRUE;
	break;
    case character:
	*xpos = *xpos * term->h_char + 0.5;
	char_coords = TRUE;
	break;
    }

    switch (pos->scaley) {
    case first_axes:
    case second_axes:
	*ypos = axis_log_value_checked(FIRST_Y_AXIS, *ypos, what);
	plot_coords = TRUE;
	break;
    case graph:
	*ypos = Y_AXIS.min + *ypos * (Y_AXIS.max - Y_AXIS.min);
	plot_coords = TRUE;
	break;
    case screen:
	*ypos = *ypos * (term->ymax -1) + 0.5;
	screen_coords = TRUE;
	break;
    case character:
	*ypos = *ypos * term->v_char + 0.5;
	char_coords = TRUE;
	break;
    }

    switch (pos->scalez) {
    case first_axes:
    case second_axes:
	*zpos = axis_log_value_checked(FIRST_Z_AXIS, *zpos, what);
	plot_coords = TRUE;
	break;
    case graph:
	*zpos = Z_AXIS.min + *zpos * (Z_AXIS.max - Z_AXIS.min);
	plot_coords = TRUE;
	break;
    case screen:
	screen_coords = TRUE;
	break;
    case character:
	char_coords = TRUE;
	break;
    }

    if (plot_coords && (screen_coords || char_coords))
	graph_error("Cannot mix screen or character coords with plot coords");

    return (screen_coords || char_coords);
}

void
map3d_position(
    struct position *pos,
    int *x, int *y,
    const char *what)
{
    double xpos = pos->x;
    double ypos = pos->y;
    double zpos = pos->z;
    
    
    if (map3d_getposition(pos, what, &xpos, &ypos, &zpos) == 0) {
	double xx, yy;
	map3d_xy_double(xpos, ypos, zpos, &xx, &yy);
	*x = xx;
	*y = yy;
    } else {
	/* Screen or character coordinates */
	*x = xpos;
	*y = ypos;
    }

    return;
}

void
map3d_position_r(
    struct position *pos,
    int *x, int *y,
    const char *what)
{
    double xpos = pos->x;
    double ypos = pos->y;
    double zpos = pos->z;

    /* startpoint in graph coordinates */
    if (map3d_getposition(pos, what, &xpos, &ypos, &zpos) == 0) {
	unsigned int xoriginlocal, yoriginlocal;
	double xx, yy;
	map3d_xy_double(xpos, ypos, zpos, &xx, &yy);
	*x = xx;
	*y = yy;
	if (pos->scalex == graph)
	    xpos = X_AXIS.min;
	else
	    xpos = 0;
	if (pos->scaley == graph)
	    ypos = Y_AXIS.min;
	else
	    ypos = 0;
	if (pos->scalez == graph)
	    zpos = Z_AXIS.min;
	else
	    zpos = 0;
	map3d_xy(xpos, ypos, zpos, &xoriginlocal, &yoriginlocal);
	*x -= xoriginlocal;
	*y -= yoriginlocal;
    } else {
    /* endpoint `screen' or 'character' coordinates */
	    *x = xpos;
	    *y = ypos;
    }
    return;
}

/*
 * these code blocks were moved to functions, to make the code simpler
 */

static void
key_text(int xl, int yl, char *text)
{
    legend_key *key = &keyT;

    if (key->just == JLEFT && key->flag == KEY_AUTO_PLACEMENT) {
	(*term->justify_text) (LEFT);
	(*term->put_text) (xl + key_text_left, yl, text);
    } else {
	if ((*term->justify_text) (RIGHT)) {
	    if (key->flag == KEY_USER_PLACEMENT)
		clip_put_text(xl + key_text_right, yl, text);
	    else
		(*term->put_text) (xl + key_text_right, yl, text);
	} else {
	    int x = xl + key_text_right - (term->h_char) * strlen(text);
	    if (key->flag == KEY_USER_PLACEMENT) {
		if (i_inrange(x, xleft, xright))
		    clip_put_text(x, yl, text);
	    } else {
		(*term->put_text) (x, yl, text);
	    }
	}
    }
}

static void
key_sample_line(int xl, int yl)
{
    legend_key *key = &keyT;

    if (key->flag == KEY_AUTO_PLACEMENT) {
	(*term->move) (xl + key_sample_left, yl);
	(*term->vector) (xl + key_sample_right, yl);
    } else {
	clip_move(xl + key_sample_left, yl);
	clip_vector(xl + key_sample_right, yl);
    }
}

static void
key_sample_point(int xl, int yl, int pointtype)
{
    legend_key *key = &keyT;

    /* HBB 20000412: fixed incorrect clipping: the point sample was
     * clipped against the graph box, even if in 'below' or 'outside'
     * position. But the result of that clipping was utterly ignored,
     * because the 'else' part did exactly the same thing as the
     * 'then' one. Some callers of this routine thus did their own
     * clipping, which I removed, along with this change.
     *
     * Now, all 'automatically' placed cases will never be clipped,
     * only user-specified ones. */
    if ((key->flag == KEY_AUTO_PLACEMENT)            /* ==-1 means auto-placed key */
	|| !clip_point(xl + key_point_offset, yl)) {
	(*term->point) (xl + key_point_offset, yl, pointtype);
    }
}


#ifdef PM3D

/*
 * returns minimal and maximal values of the cb-range (or z-range if taking the
 * color from the z value) of the given surface
 */
static void
get_surface_cbminmax(struct surface_points *plot, double *cbmin, double *cbmax)
{
    int i, curve = 0;
    TBOOLEAN color_from_column = plot->pm3d_color_from_column; /* just a shortcut */
    coordval cb;
    struct iso_curve *icrvs = plot->iso_crvs;
    struct coordinate GPHUGE *points;

    *cbmin = VERYLARGE;
    *cbmax = -VERYLARGE;

    while (icrvs && curve < plot->num_iso_read) {
	/* fprintf(stderr,"**** NEW ISOCURVE - nb of pts: %i ****\n", icrvs->p_count); */
        for (i = 0, points = icrvs->points; i < icrvs->p_count; i++) {
		/* fprintf(stderr,"  point i=%i => x=%4g y=%4g z=%4lg cb=%4lg\n",i, points[i].x,points[i].y,points[i].z,points[i].CRD_COLOR); */
		if (points[i].type == INRANGE) {
		    /* ?? if (!clip_point(x, y)) ... */
		    cb = color_from_column ? points[i].CRD_COLOR : points[i].z;
		    if (cb < *cbmin) *cbmin = cb;
		    if (cb > *cbmax) *cbmax = cb;
		}
	} /* points on one scan */
	icrvs = icrvs->next;
	curve++;
    } /* surface */
}


/*
 * Draw a gradient color line for a key (legend).
 */
static void
key_sample_line_pm3d(struct surface_points *plot, int xl, int yl)
{
    legend_key *key = &keyT;
    int steps = GPMIN(24, abs(key_sample_right - key_sample_left));
        /* don't multiply by key->swidth --- could be >> palette.maxcolors */
    /* int x_from = xl + key_sample_left; */
    int x_to = xl + key_sample_right;
    double step = ((double)(key_sample_right - key_sample_left)) / steps;
    int i = 1, x1 = xl + key_sample_left, x2;
    double cbmin, cbmax;
    double gray, gray_from, gray_to, gray_step;

    /* color gradient only over the cb-values of the surface, if smaller than the
     * cb-axis range (the latter are gray values [0:1]) */
    get_surface_cbminmax(plot, &cbmin, &cbmax);
    if (cbmin > cbmax) return; /* splot 1/0, for example */
    cbmin = GPMAX(cbmin, CB_AXIS.min);
    cbmax = GPMIN(cbmax, CB_AXIS.max);
    gray_from = cb2gray(cbmin);
    gray_to = cb2gray(cbmax);
    gray_step = (gray_to - gray_from)/steps;

    if (key->flag == KEY_AUTO_PLACEMENT)
	(*term->move) (x1, yl);
    else
	clip_move(x1, yl);
    x2 = x1;
    while (i <= steps) {
	/* if (i>1) set_color( i==steps ? 1 : (i-0.5)/steps ); ... range [0:1] */
	gray = (i==steps) ? gray_to : gray_from+i*gray_step;
	set_color(gray);
	(*term->move) (x2, yl);
	x2 = (i==steps) ? x_to : x1 + (int)(i*step+0.5);
	if (key->flag == KEY_AUTO_PLACEMENT)
	    (*term->vector) (x2, yl);
	else
	    clip_vector(x2, yl);
	i++;
    }
}


/*
 * Draw a sequence of points with gradient color a key (legend).
 */
static void
key_sample_point_pm3d(
    struct surface_points *plot,
    int xl, int yl,
    int pointtype)
{
    legend_key *key = &keyT;
    /* int x_from = xl + key_sample_left; */
    int x_to = xl + key_sample_right;
    int i = 0, x1 = xl + key_sample_left, x2;
    double cbmin, cbmax;
    double gray, gray_from, gray_to, gray_step;
    /* rule for number of steps: 3*char_width*pointsize or char_width for dots,
     * but at least 3 points */
    double step = term->h_char * (pointtype == -1 ? 1 : 3*(1+(pointsize-1)/2));
    int steps = (int)(((double)(key_sample_right - key_sample_left)) / step + 0.5);
    if (steps < 2) steps = 2;
    step = ((double)(key_sample_right - key_sample_left)) / steps;

    /* color gradient only over the cb-values of the surface, if smaller than the
     * cb-axis range (the latter are gray values [0:1]) */
    get_surface_cbminmax(plot, &cbmin, &cbmax);
    if (cbmin > cbmax) return; /* splot 1/0, for example */
    cbmin = GPMAX(cbmin, CB_AXIS.min);
    cbmax = GPMIN(cbmax, CB_AXIS.max);
    gray_from = cb2gray(cbmin);
    gray_to = cb2gray(cbmax);
    gray_step = (gray_to - gray_from)/steps;
#if 0
    fprintf(stderr,"POINT_pm3D: cbmin=%g  cbmax=%g\n",cbmin, cbmax);
    fprintf(stderr,"steps=%i gray_from=%g gray_to=%g gray_step=%g\n",steps,gray_from,gray_to,gray_step);
#endif
    while (i <= steps) {
	/* if (i>0) set_color( i==steps ? gray_to : (i-0.5)/steps ); ... range [0:1] */
	gray = (i==steps) ? gray_to : gray_from+i*gray_step;
	set_color(gray);
	x2 = i==0 ? x1 : (i==steps ? x_to : x1 + (int)(i*step+0.5));
	/* x2 += key_point_offset; ... that's if there is only 1 point */
	if (key->flag == KEY_AUTO_PLACEMENT || !clip_point(x2, yl))
	    (*term->point) (x2, yl, pointtype);
	i++;
    }
}

#endif


/* plot_vectors:
 * Plot the curves in VECTORS style
 */
static void
plot3d_vectors(struct surface_points *plot)
{
    int i;
    unsigned int x1, y1, x2, y2;
    struct coordinate GPHUGE *heads = plot->iso_crvs->points;
    struct coordinate GPHUGE *tails = plot->iso_crvs->next->points;

    /* Only necessary once because all arrows equal */
    term_apply_lp_properties(&(plot->arrow_properties.lp_properties));
    apply_head_properties(&(plot->arrow_properties));

    for (i = 0; i < plot->iso_crvs->p_count; i++) {
	
	if (heads[i].type == UNDEFINED || tails[i].type == UNDEFINED)
	    continue;

	if (heads[i].type == INRANGE && tails[i].type == INRANGE) {
	    map3d_xy(heads[i].x, heads[i].y, heads[i].z, &x1, &y1);
	    map3d_xy(tails[i].x, tails[i].y, tails[i].z, &x2, &y2);

	    if (!clip_point(x1, y1) && !(clip_point(x2,y2)))
		(*term->arrow) (x1, y1, x2, y2, plot->arrow_properties.head);
	}
    }
}

