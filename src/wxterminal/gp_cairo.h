/* GNUPLOT - gp_cairo.h */

/*[
 * Copyright 2005,2006   Timothee Lecomte
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
 *
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above. If you wish to allow
 * use of your version of this file only under the terms of the GPL and not
 * to allow others to use your version of this file under the above gnuplot
 * license, indicate your decision by deleting the provisions above and replace
 * them with the notice and other provisions required by the GPL. If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the GPL or the gnuplot license.
]*/

/* -----------------------------------------------------
 * This code uses the cairo library, a 2D graphics library with
 * support for multiple output devices.
 * Cairo is distributed under the LGPL licence.
 *
 * See http://www.cairographics.org for details.

 * It also uses the pango library, a text-layout rendering library.
 * Pango is distributed under the LGPL licence.
 *
 * See http://www.pango.org for details.
 * -----------------------------------------------------*/

/* ------------------------------------------------------
 * This is the header for all cairo related functions,
 * which provide drawing facilities to implement gnuplot's needs.
 *
 * In particular, we have here :
 * - all the basic calls (lines, polygons for pm3d, custom patterns),
 * - image support,
 * - enhanced text mode
 *
 * The text rendering is done via pango.
 * ------------------------------------------------------*/

#ifndef GNUPLOT_WXT_CAIRO_H
# define GNUPLOT_WXT_CAIRO_H

#ifdef __cplusplus
extern "C" {
#endif /*__cplusplus*/

/* for JUSTIFY, set_encoding_id, *term definitions, color.h */
# include "term_api.h"
/* for rgb functions */
# include "getcolor.h"

# include <cairo.h>

/* oversampling scale */
#define GP_CAIRO_SCALE 20

/* linked list in reverse order used to draw polygons more efficiently */
typedef struct path_item {
	gpiPoint *corners;
	int n;
	rgb_color color;
	struct path_item *previous;
} path_item;

/* plot structure containing all the information cairo needs to execute
 * the drawing commands.
 * Don't forget to update gp_cairo_initialize_plot when a new entry is added there. */
typedef struct plot_struct {
	/* scaling and conversion between gnuplot and device coordinates.
	 * For a static terminal, scales are 1 and sizes are those of the term table.
	 * For an interactive terminal, they are used to handle window resizing.
	 * xmax and ymax are the sizes known by gnuplot.
	 * current_xmax and current_ymax are the device size, which may change when the window
	 * is resized.
	 * xmax and ymax are stored here in addition to term->xmax and term->ymax,
	 * to handle the case when another terminal is chosen (x11 for example), which
	 * may change these variables whereas we want to keep the one used for our plot */
	double xscale, yscale;
	int current_xmax, current_ymax;
	int xmax, ymax;

	/* either GP_CAIRO_SCALE or 1, depending on rendering */
	int oversampling_scale;

	/* handle vertical/horizontal lines properly */
	double current_x, current_y;
	double orig_current_x, orig_current_y;
	
	/* style data used while processing gnuplot commands */
	JUSTIFY justify_mode;
	int linetype;
	double linewidth;
	int linestyle;
	double pointsize;
	double text_angle;
	rgb_color color;

	/* "polyline" */
	TBOOLEAN opened_path;

	/* font handling */
	char fontname[MAX_ID_LEN + 1];
	double fontsize;
	enum set_encoding_id encoding;

	/* state of the cairo context creation */
	TBOOLEAN success;

	TBOOLEAN antialiasing;

	TBOOLEAN oversampling;

	/* hinting option for horizontal and vertical lines :
	 * Hinting is the process of fitting outlines to the pixel grid
	 * in order to improve the appearance of the result.
	 * Since hinting outlines involves distorting them,
	 * it also reduces the faithfulness to the original outline shapes.
	 * hinting = 100 means full hinting
	 * hinting = 0 means no hinting */
	int hinting;

	/* cairo drawing context */
	cairo_t *cr;

	/* polygons list */
	path_item *polygon_path_last;

	/* flag set to true when the user hit ctrl-c */
	TBOOLEAN interrupt;
} plot_struct;

/* linetype enums */
enum {
GP_CAIRO_SOLID,
GP_CAIRO_DASH
};

/* correspondance between gnuplot's linetypes and colors */
extern rgb_color gp_cairo_linetype2color( int linetype );

/* functions to handle scaling between device and gnuplot coordinates */
extern double device_x(plot_struct *plot, double x);
extern double device_y(plot_struct *plot, double y);
extern double gnuplot_x(plot_struct *plot, double x);
extern double gnuplot_y(plot_struct *plot, double y);

/* initialize all fields of the plot structure */
extern void gp_cairo_initialize_plot(plot_struct *plot);
/* initialize the transformation matrix of the drawing context and other details */
/* Depends on the setting of xscale and yscale */
extern void gp_cairo_initialize_context(plot_struct *plot);

/* functions used to process gnuplot commands */
extern void gp_cairo_move(plot_struct *plot, int x, int y);
extern void gp_cairo_vector(plot_struct *plot, int x, int y);
extern void gp_cairo_stroke(plot_struct *plot);
extern void gp_cairo_draw_text(plot_struct *plot, int x1, int y1, const char* str);
extern void gp_cairo_draw_enhanced_text(plot_struct *plot, int x1, int y1, const char* str);
extern void gp_cairo_draw_point(plot_struct *plot, int x1, int y1, int style);
extern void gp_cairo_draw_fillbox(plot_struct *plot, int x, int y, int width, int height, int style);
extern void gp_cairo_draw_polygon(plot_struct *plot, int n, gpiPoint *corners);
extern void gp_cairo_end_polygon(plot_struct *plot);
#ifdef WITH_IMAGE
extern void gp_cairo_draw_image(plot_struct *plot, coordval * image, int x1, int y1, int x2, int y2, int x3, int y3, int x4, int y4, int M, int N, t_imagecolor color_mode);
#endif /*WITH_IMAGE*/
extern void gp_cairo_set_color(plot_struct *plot, rgb_color color);
extern void gp_cairo_set_linestyle(plot_struct *plot, int linestyle);
extern void gp_cairo_set_linetype(plot_struct *plot, int linetype);
extern void gp_cairo_set_pointsize(plot_struct *plot, double pointsize);
extern void gp_cairo_set_justify(plot_struct *plot, JUSTIFY mode);
extern void gp_cairo_set_font(plot_struct *plot, const char *name, int fontsize);
extern void gp_cairo_set_linewidth(plot_struct *plot, double linewidth);
extern void gp_cairo_set_textangle(plot_struct *plot, double angle);

/* erase the contents of the cairo drawing context */
extern void gp_cairo_clear(plot_struct *plot);

/* fill term->h_char, v_char, h_tic, v_tic
 * Depends on plot->fontsize and fontname */
extern void gp_cairo_set_termvar(plot_struct *plot);

/* translate plot->encoding int to char* suitable for glib */
extern const char* gp_cairo_get_encoding(plot_struct *plot);

#ifdef __cplusplus
}
#endif /*__cplusplus*/

#endif /* gnuplot_wxt_cairo_h */
