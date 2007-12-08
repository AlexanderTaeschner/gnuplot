/*
 * gadgets.h,v 1.1.3.1 2000/05/03 21:47:15 hbb Exp
 */

/* GNUPLOT - gadgets.h */

/*[
 * Copyright 2000, 2004   Thomas Williams, Colin Kelley
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

#ifndef GNUPLOT_GADGETS_H
# define GNUPLOT_GADGETS_H

#include "syscfg.h"

#include "term_api.h"

/* Types and variables concerning graphical plot elements that are not
 * *terminal-specific, are used by both* 2D and 3D plots, and are not
 * *assignable to any particular * axis. I.e. they belong to neither
 * *term_api, graphics, graph3d, nor * axis .h files.
 */

/* #if... / #include / #define collection: */

/* Default point size is taken from the global "pointsize" variable */
#define PTSZ_DEFAULT    (-2)
#define PTSZ_VARIABLE   (-3)

/* Type definitions */

/* Coordinate system specifications: x1/y1, x2/y2, graph-box relative
 * or screen relative coordinate systems */
typedef enum position_type {
    first_axes,
    second_axes,
    graph,
    screen,
    character
} position_type;

/* A full 3D position, with all 3 coordinates of different axes,
 * possibly. Used for 'set label' and 'set arrow' positions: */
typedef struct position {
    enum position_type scalex,scaley,scalez;
    double x,y,z;
} t_position;

/* Linked list of structures storing 'set label' information */
typedef struct text_label {
    struct text_label *next;	/* pointer to next label in linked list */
    int tag;			/* identifies the label */
    t_position place;
    enum JUSTIFY pos;
    int rotate;
    int layer;
    char *text;
    char *font;			/* Entry font added by DJL */
    struct t_colorspec textcolor;
    struct lp_style_type lp_properties;
    struct position offset;
    TBOOLEAN noenhanced;
} text_label;

/* This is the default state for the axis, timestamp, and plot title labels
 * indicated by tag = -2 */
#define EMPTY_LABELSTRUCT \
    {NULL, -2, {character, character, character, 0.0, 0.0, 0.0}, CENTRE, 0, 0, \
     NULL, NULL, {TC_LT, -2, 0.0}, DEFAULT_LP_STYLE_TYPE, \
     {character, character, character, 0.0, 0.0, 0.0}, FALSE }

/* Datastructure for implementing 'set arrow' */
typedef struct arrow_def {
    struct arrow_def *next;	/* pointer to next arrow in linked list */
    int tag;			/* identifies the arrow */
    t_position start;
    t_position end;
    TBOOLEAN relative;		/* second coordinate is relative to first */
    struct arrow_style_type arrow_properties;
} arrow_def;

#ifdef EAM_OBJECTS
/* The only object type supported so far is OBJ_RECTANGLE */
typedef struct rectangle {
    int type;			/* 0 = corners;  1 = center + size */
    t_position bl;		/* bottom left */
    t_position tr;		/* top right */
    t_position center;		/* center */
    t_position extent;		/* width and height */
} t_rectangle;

/* Datastructure for 'set object' */
typedef struct object {
    struct object *next;
    int tag;
    int layer;			/* behind or back or front */
    int object_type;	/* OBJ_RECTANGLE */
    fill_style_type fillstyle;
    lp_style_type lp_properties;
    union o {t_rectangle rectangle;} o;
} t_object;
#define OBJ_RECTANGLE (1)
#endif

/* Datastructure implementing 'set style line' */
struct linestyle_def {
    struct linestyle_def *next;	/* pointer to next linestyle in linked list */
    int tag;			/* identifies the linestyle */
    struct lp_style_type lp_properties;
};

/* Datastructure implementing 'set style arrow' */
struct arrowstyle_def {
    struct arrowstyle_def *next;/* pointer to next arrowstyle in linked list */
    int tag;			/* identifies the arrowstyle */
    struct arrow_style_type arrow_properties;
};

/* The stacking direction of the key box: (vertical, horizontal) */
typedef enum en_key_stack_direction {
    GPKEY_VERTICAL,
    GPKEY_HORIZONTAL
} t_key_stack_direction;

/* The region, with respect to the border, key is located: (inside, outside) */
typedef enum en_key_region {
    GPKEY_AUTO_INTERIOR_LRTBC,   /* Auto placement, left/right/top/bottom/center */
    GPKEY_AUTO_EXTERIOR_LRTBC,   /* Auto placement, left/right/top/bottom/center */
    GPKEY_AUTO_EXTERIOR_MARGIN,  /* Auto placement, margin plus lrc or tbc */
    GPKEY_USER_PLACEMENT         /* User specified placement */
} t_key_region;

/* If exterior, there are 12 possible auto placements.  Since
   left/right/center with top/bottom/center can only define 9
   locations, further subdivide the exterior region into four
   subregions for which left/right/center (TMARGIN/BMARGIN)
   and top/bottom/center (LMARGIN/RMARGIN) creates 12 locations. */
typedef enum en_key_ext_region {
    GPKEY_TMARGIN,
    GPKEY_BMARGIN,
    GPKEY_LMARGIN,
    GPKEY_RMARGIN
} t_key_ext_region;

/* Key sample to the left or the right of the plot title? */
typedef enum en_key_sample_positioning {
    GPKEY_LEFT,
    GPKEY_RIGHT
} t_key_sample_positioning;

typedef struct {
    int opt_given; /* option given / not given (otherwise default) */
    int closeto;   /* from list FILLEDCURVES_CLOSED, ... */
    double at;	   /* value for FILLEDCURVES_AT... */
    double aty;	   /* the other value for FILLEDCURVES_ATXY */
    int oneside;   /* -1 if fill below bound only; +1 if fill above bound only */
} filledcurves_opts;
#define EMPTY_FILLEDCURVES_OPTS { 0, 0, 0.0, 0.0, 0 }

#ifdef EAM_HISTOGRAMS
typedef struct histogram_style {
    int type;		/* enum t_histogram_type */
    int gap;		/* set style hist gap <n> (space between clusters) */
    int clustersize;	/* number of datasets in this histogram */
    double start;	/* X-coord of first histogram entry */
    double end;		/* X-coord of last histogram entry */
    int startcolor;	/* LT_UNDEFINED or explicit color for first entry */
    int startpattern;	/* LT_UNDEFINED or explicit pattern for first entry */
    double bar_lw;	/* linewidth for error bars */
    struct histogram_style *next;
    struct text_label title;
} histogram_style;
typedef enum histogram_type {
	HT_NONE,
	HT_STACKED_IN_LAYERS,
	HT_STACKED_IN_TOWERS,
	HT_CLUSTERED,
	HT_ERRORBARS
} t_histogram_type;
#define DEFAULT_HISTOGRAM_STYLE { HT_NONE, 2, 1, 0.0, 0.0, LT_UNDEFINED, LT_UNDEFINED, 0, NULL, EMPTY_LABELSTRUCT }

#endif

/***********************************************************/
/* Variables defined by gadgets.c needed by other modules. */
/***********************************************************/



/* EAM Feb 2003 - Move all global variables related to key into a */
/* single structure. Eventually this will allow multiple keys.    */

typedef enum keytitle_type {
    NOAUTO_KEYTITLES, FILENAME_KEYTITLES, COLUMNHEAD_KEYTITLES
} keytitle_type;

typedef struct {
    TBOOLEAN visible;		/* Do we show this key at all? */
    t_key_region region;	/* if so: where? */
    t_key_ext_region margin;	/* if exterior: where outside? */
    struct position user_pos;	/* if user specified position, this is it */
    VERT_JUSTIFY vpos;		/* otherwise these guide auto-positioning */
    JUSTIFY hpos;
    t_key_sample_positioning just;
    t_key_stack_direction stack_dir;
    double swidth;		/* 'width' of the linestyle sample line in the key */
    double vert_factor;		/* user specified vertical spacing multiplier */
    double width_fix;		/* user specified additional (+/-) width of key titles */
    double height_fix;
    keytitle_type auto_titles;	/* auto title curves unless plotted 'with notitle' */
    TBOOLEAN reverse;		/* key back to front */
    TBOOLEAN invert;		/* key top to bottom */
    TBOOLEAN enhanced;		/* enable/disable enhanced text of key titles */
    struct lp_style_type box;	/* linetype of box around key:  */
    char title[MAX_LINE_LEN+1];	/* title line for the key as a whole */
    char *font;			/* Will be used for both key title and plot titles */
    struct t_colorspec textcolor;	/* Will be used for both key title and plot titles */
} legend_key;

extern legend_key keyT;

# define DEFAULT_KEYBOX_LP { 0, LT_NODRAW, 0, 1.0, 1.0, 0 }

#define DEFAULT_KEY_POSITION { graph, graph, graph, 0.9, 0.9, 0. }

#define DEFAULT_KEY_PROPS \
		{ TRUE, \
		GPKEY_AUTO_INTERIOR_LRTBC, GPKEY_RMARGIN, \
		DEFAULT_KEY_POSITION, \
		JUST_TOP, RIGHT, \
		GPKEY_RIGHT, GPKEY_VERTICAL, \
		4.0, 1.0, 0.0, 0.0, \
		FILENAME_KEYTITLES, \
		FALSE, FALSE, TRUE, \
		DEFAULT_KEYBOX_LP, \
		"", \
		NULL, {TC_LT, LT_BLACK, 0.0} }

/* bounding box position, in terminal coordinates */
typedef struct {
    int xleft;
    int xright;
    int ybot;
    int ytop;
} BoundingBox;


/*
 * EAM Jan 2006 - Move colorbox structure definition to here from color.h
 * in order to be able to use struct position
 */

#define SMCOLOR_BOX_NO      'n'
#define SMCOLOR_BOX_DEFAULT 'd'
#define SMCOLOR_BOX_USER    'u'

typedef struct {
  char where;
    /* where
	SMCOLOR_BOX_NO .. do not draw the colour box
	SMCOLOR_BOX_DEFAULT .. draw it at default position and size
	SMCOLOR_BOX_USER .. draw it at the position given by user
    */
  char rotation; /* 'v' or 'h' vertical or horizontal box */
  char border; /* if non-null, a border will be drawn around the box (default) */
  int border_lt_tag;
  int layer; /* front or back */
  struct position origin;
  struct position size;
} color_box_struct;

extern color_box_struct color_box;
extern color_box_struct default_color_box;

extern BoundingBox plot_bounds;	/* Plot Boundary */
extern BoundingBox canvas; 	/* Writable area on terminal */
extern BoundingBox *clip_area;	/* Current clipping box */

extern float xsize;		/* x scale factor for size */
extern float ysize;		/* y scale factor for size */
extern float zsize;		/* z scale factor for size */
extern float xoffset;		/* x origin setting */
extern float yoffset;		/* y origin setting */
extern float aspect_ratio;	/* 1.0 for square */

/* plot border autosizing overrides, in characters (-1: autosize) */
extern t_position lmargin, bmargin, rmargin, tmargin;
#define DEFAULT_MARGIN_POSITION {character, character, character, -1, -1, -1}

extern FILE *table_outfile;
extern TBOOLEAN table_mode;

extern struct arrow_def *first_arrow;

extern struct text_label *first_label;

extern struct linestyle_def *first_linestyle;

extern struct arrowstyle_def *first_arrowstyle;

#ifdef EAM_OBJECTS
extern struct object *first_object;
#endif

extern text_label title;

extern text_label timelabel;
#ifndef DEFAULT_TIMESTAMP_FORMAT
/* asctime() format */
# define DEFAULT_TIMESTAMP_FORMAT "%a %b %d %H:%M:%S %Y"
#endif
extern int timelabel_rotate;
extern int timelabel_bottom;

extern TBOOLEAN	polar;

#define ZERO 1e-8		/* default for 'zero' set option */
extern double zero;		/* zero threshold, not 0! */

extern double pointsize;

#define SOUTH		1 /* 0th bit */
#define WEST		2 /* 1th bit */
#define NORTH		4 /* 2th bit */
#define EAST		8 /* 3th bit */
#define border_east	(draw_border & EAST)
#define border_west	(draw_border & WEST)
#define border_south	(draw_border & SOUTH)
#define border_north	(draw_border & NORTH)
#define border_complete	((draw_border & 15) == 15)
extern int draw_border;
extern int border_layer;

extern struct lp_style_type border_lp;
extern const struct lp_style_type default_border_lp;

extern TBOOLEAN	clip_lines1;
extern TBOOLEAN	clip_lines2;
extern TBOOLEAN	clip_points;

#define SAMPLES 100		/* default number of samples for a plot */
extern int samples_1;
extern int samples_2;

extern double ang2rad; /* 1 or pi/180 */

extern enum PLOT_STYLE data_style;
extern enum PLOT_STYLE func_style;

extern TBOOLEAN parametric;

extern TBOOLEAN is_3d_plot;

#ifdef WITH_IMAGE
extern TBOOLEAN is_cb_plot;
#endif

#ifdef VOLATILE_REFRESH
extern int refresh_ok;		/* 0 = no;  2 = 2D ok;  3 = 3D ok */
extern int refresh_nplots;
#else
#define refresh_ok FALSE
#endif
extern TBOOLEAN volatile_data;

/* Plot layer definitions are collected here. */
/* Someday they might actually be used.       */
#define LAYER_BEHIND     -1
#define LAYER_BACK        0
#define LAYER_FRONT       1
#define LAYER_PLOTLABELS 99

/* Functions exported by gadgets.c */

/* moved here from util3d: */
void draw_clip_line __PROTO((int, int, int, int));
void draw_clip_arrow __PROTO((int, int, int, int, int));
int clip_line __PROTO((int *, int *, int *, int *));
int clip_point __PROTO((unsigned int, unsigned int));
void clip_put_text __PROTO((unsigned int, unsigned int, char *));

/* moved here from graph3d: */
void clip_move __PROTO((unsigned int x, unsigned int y));
void clip_vector __PROTO((unsigned int x, unsigned int y));

/* Common routines for setting line or text color from t_colorspec */
void apply_pm3dcolor __PROTO((struct t_colorspec *tc, const struct termentry *t));
void reset_textcolor __PROTO((const struct t_colorspec *tc, const struct termentry *t));

extern fill_style_type default_fillstyle;

#ifdef EAM_OBJECTS
extern struct object default_rectangle;
#define DEFAULT_RECTANGLE_STYLE { NULL, -1, 0, OBJ_RECTANGLE,	\
	{FS_SOLID, 100, 0, LT_BLACK},   			\
	{1, LT_BACKGROUND, 0, 1.0, 0.0},			\
	{{0, {0,0.,0.,0.}, {0,0.,0.,0.}, {0,0.,0.,0.}, {0,0.,0.,0.}}} }
#endif

/* filledcurves style options set by 'set style [data|func] filledcurves opts' */
extern filledcurves_opts filledcurves_opts_data;
extern filledcurves_opts filledcurves_opts_func;

/* Prefer line styles over plain line types */
extern TBOOLEAN prefer_line_styles;

#ifdef EAM_HISTOGRAMS
extern histogram_style histogram_opts;
#endif

void default_arrow_style __PROTO((struct arrow_style_type *arrow));

#ifdef EAM_DATASTRINGS
void free_labels __PROTO((struct text_label *tl));
#endif

void get_offsets __PROTO((struct text_label *this_label,
	struct termentry *t, int *htic, int *vtic));
void write_label __PROTO((unsigned int x, unsigned int y, struct text_label *label));

#endif /* GNUPLOT_GADGETS_H */
