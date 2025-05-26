/* GNUPLOT - term.c */

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
 * Bookkeeping and support routines for 'set multiplot layout ...'
 * Jul 2004 Volker Dobler     layout rows, columns
 * Feb 2013 Christoph Bersch  layout margins spacing
 * Mar 2014 Ethan A Merritt   refactor into separate file (used to be in term.c)
 */

#include "term_api.h"

#include "command.h"
#include "datablock.h"
#include "gadgets.h"
#include "graphics.h"
#include "misc.h"
#include "multiplot.h"
#include "parse.h"
#include "setshow.h"
#include "util.h"

/* Support for multiplot playback */
TBOOLEAN multiplot_playback = FALSE;	  /* TRUE while inside "remultiplot" playback */
TBOOLEAN suppress_multiplot_save = FALSE; /* TRUE inside a for/while loop */
static t_value multiplot_udv = {
	.type = DATABLOCK,
	.v.data_array = NULL
};
int multiplot_last_panel = 0;

/* Local prototypes */
static void mp_layout_size_and_offset(void);
static void mp_layout_margins_and_spacing(void);
static void mp_layout_set_margin_or_spacing(t_position *);
static void init_multiplot_datablock(void);
static void multiplot_previous(void);

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

# define MP_LAYOUT_DEFAULT {          \
    FALSE,	/* auto_layout */         \
    0,		/* current_panel */       \
    0, 0,	/* num_rows, num_cols */  \
    FALSE,	/* row_major */           \
    TRUE,	/* downwards */           \
    0, 0,	/* act_row, act_col */    \
    1, 1,	/* xscale, yscale */      \
    0, 0,	/* xoffset, yoffset */    \
    FALSE,	/* auto_layout_margins */ \
    {screen, screen, screen, 0.1, -1, -1},  /* lmargin */ \
    {screen, screen, screen, 0.9, -1, -1},  /* rmargin */ \
    {screen, screen, screen, 0.1, -1, -1},  /* bmargin */ \
    {screen, screen, screen, 0.9, -1, -1},  /* tmargin */ \
    {screen, screen, screen, 0.05, -1, -1}, /* xspacing */ \
    {screen, screen, screen, 0.05, -1, -1}, /* yspacing */ \
    0,0,0,0,	/* prev_ sizes and offsets */ \
    DEFAULT_MARGIN_POSITION, \
    DEFAULT_MARGIN_POSITION, \
    DEFAULT_MARGIN_POSITION, \
    DEFAULT_MARGIN_POSITION,  /* prev_ margins */ \
    EMPTY_LABELSTRUCT, 0.0 \
}

static struct {
    TBOOLEAN auto_layout;  /* automatic layout if true */
    int current_panel;     /* initialized to 0, incremented after each plot */
    int num_rows;          /* number of rows in layout */
    int num_cols;          /* number of columns in layout */
    TBOOLEAN row_major;    /* row major mode if true, column major else */
    TBOOLEAN downwards;    /* prefer downwards or upwards direction */
    int act_row;           /* actual row in layout */
    int act_col;           /* actual column in layout */
    double xscale;         /* factor for horizontal scaling */
    double yscale;         /* factor for vertical scaling */
    double xoffset;        /* horizontal shift */
    double yoffset;        /* horizontal shift */
    TBOOLEAN auto_layout_margins;
    t_position lmargin, rmargin, bmargin, tmargin;
    t_position xspacing, yspacing;
    double prev_xsize, prev_ysize, prev_xoffset, prev_yoffset;
    t_position prev_lmargin, prev_rmargin, prev_tmargin, prev_bmargin;
			   /* values before 'set multiplot layout' */
    text_label title;      /* goes above complete set of plots */
    double title_height;   /* fractional height reserved for title */
} mp_layout = MP_LAYOUT_DEFAULT;

BoundingBox panel_bounds;/* terminal coords of next panel to be drawn */

/* Helper routines */
void
multiplot_next()
{
    mp_layout.current_panel++;
    if (mp_layout.auto_layout) {
	if (mp_layout.row_major) {
	    mp_layout.act_row++;
	    if (mp_layout.act_row == mp_layout.num_rows) {
		mp_layout.act_row = 0;
		mp_layout.act_col++;
		if (mp_layout.act_col == mp_layout.num_cols) {
		    /* int_warn(NO_CARET,"will overplot first plot"); */
		    mp_layout.act_col = 0;
		}
	    }
	} else { /* column-major */
	    mp_layout.act_col++;
	    if (mp_layout.act_col == mp_layout.num_cols ) {
		mp_layout.act_col = 0;
		mp_layout.act_row++;
		if (mp_layout.act_row == mp_layout.num_rows ) {
		    /* int_warn(NO_CARET,"will overplot first plot"); */
		    mp_layout.act_row = 0;
		}
	    }
	}
	multiplot_reset();
    }
}

static void
multiplot_previous(void)
{
    mp_layout.current_panel--;
    if (mp_layout.auto_layout) {
	if (mp_layout.row_major) {
	    mp_layout.act_row--;
	    if (mp_layout.act_row < 0) {
		mp_layout.act_row = mp_layout.num_rows-1;
		mp_layout.act_col--;
		if (mp_layout.act_col < 0) {
		    /* int_warn(NO_CARET,"will overplot first plot"); */
		    mp_layout.act_col = mp_layout.num_cols-1;
		}
	    }
	} else { /* column-major */
	    mp_layout.act_col--;
	    if (mp_layout.act_col < 0) {
		mp_layout.act_col = mp_layout.num_cols-1;
		mp_layout.act_row--;
		if (mp_layout.act_row < 0) {
		    /* int_warn(NO_CARET,"will overplot first plot"); */
		    mp_layout.act_row = mp_layout.num_rows-1;
		}
	    }
	}
	multiplot_reset();
    }
}

int
multiplot_current_panel()
{
    return mp_layout.current_panel;
}

void
multiplot_start()
{
    TBOOLEAN set_spacing = FALSE;
    TBOOLEAN set_margins = FALSE;

    c_token++;

    /* Only a few options are possible if we are already in multiplot mode */
    if (in_multiplot > 0) {
	if (equals(c_token, "next")) {
	    c_token++;
	    if (!mp_layout.auto_layout)
		int_error(c_token, "only valid inside an auto-layout multiplot");
	    multiplot_next();
	    return;
	} else if (almost_equals(c_token, "prev$ious")) {
	    c_token++;
	    if (!mp_layout.auto_layout)
		int_error(c_token, "only valid inside an auto-layout multiplot");
	    multiplot_previous();
	    return;
	} else {
	    term_end_multiplot();
	}
    }

    /* FIXME: more options should be reset/initialized each time */
    mp_layout.auto_layout = FALSE;
    mp_layout.auto_layout_margins = FALSE;
    mp_layout.current_panel = 0;
    mp_layout.title.noenhanced = FALSE;
    free(mp_layout.title.text);
    mp_layout.title.text = NULL;
    free(mp_layout.title.font);
    mp_layout.title.font = NULL;
    mp_layout.title.boxed = 0;

    /* Parse options */
    while (!END_OF_COMMAND) {

	if (almost_equals(c_token, "ti$tle")) {
	    c_token++;
	    parse_label_options(&mp_layout.title, 2);
	    if (!END_OF_COMMAND)
		mp_layout.title.text = try_to_get_string();
	    parse_label_options(&mp_layout.title, 2);
 	    continue;
	}

	if (almost_equals(c_token, "lay$out")) {
	    if (mp_layout.auto_layout)
		int_error(c_token, "too many layout commands");
	    else
		mp_layout.auto_layout = TRUE;

	    c_token++;
	    if (END_OF_COMMAND)
		int_error(c_token,"expecting '<num_cols>,<num_rows>'");

	    /* read row,col */
	    mp_layout.num_rows = int_expression();
	    if (END_OF_COMMAND || !equals(c_token,",") )
		int_error(c_token, "expecting ', <num_cols>'");

	    c_token++;
	    if (END_OF_COMMAND)
		int_error(c_token, "expecting <num_cols>");
	    mp_layout.num_cols = int_expression();

	    /* remember current values of the plot size and the margins */
	    mp_layout.prev_xsize = xsize;
	    mp_layout.prev_ysize = ysize;
	    mp_layout.prev_xoffset = xoffset;
	    mp_layout.prev_yoffset = yoffset;
	    mp_layout.prev_lmargin = lmargin;
	    mp_layout.prev_rmargin = rmargin;
	    mp_layout.prev_bmargin = bmargin;
	    mp_layout.prev_tmargin = tmargin;

	    mp_layout.act_row = 0;
	    mp_layout.act_col = 0;

	    continue;
	}

	/* The remaining options are only valid for auto-layout mode */
	if (!mp_layout.auto_layout)
	    int_error(c_token, "only valid in the context of an auto-layout command");

	switch(lookup_table(&set_multiplot_tbl[0],c_token)) {
	    case S_MULTIPLOT_COLUMNSFIRST:
		mp_layout.row_major = TRUE;
		c_token++;
		break;
	    case S_MULTIPLOT_ROWSFIRST:
		mp_layout.row_major = FALSE;
		c_token++;
		break;
	    case S_MULTIPLOT_DOWNWARDS:
		mp_layout.downwards = TRUE;
		c_token++;
		break;
	    case S_MULTIPLOT_UPWARDS:
		mp_layout.downwards = FALSE;
		c_token++;
		break;
	    case S_MULTIPLOT_SCALE:
		c_token++;
		mp_layout.xscale = real_expression();
		mp_layout.yscale = mp_layout.xscale;
		if (!END_OF_COMMAND && equals(c_token,",") ) {
		    c_token++;
		    if (END_OF_COMMAND) {
			int_error(c_token, "expecting <yscale>");
		    }
		    mp_layout.yscale = real_expression();
		}
		break;
	    case S_MULTIPLOT_OFFSET:
		c_token++;
		mp_layout.xoffset = real_expression();
		mp_layout.yoffset = mp_layout.xoffset;
		if (!END_OF_COMMAND && equals(c_token,",") ) {
		    c_token++;
		    if (END_OF_COMMAND) {
			int_error(c_token, "expecting <yoffset>");
		    }
		    mp_layout.yoffset = real_expression();
		}
		break;
	    case S_MULTIPLOT_MARGINS:
		c_token++;
		if (END_OF_COMMAND)
		    int_error(c_token,"expecting '<left>,<right>,<bottom>,<top>'");
		
		mp_layout.lmargin.scalex = screen;
		mp_layout_set_margin_or_spacing(&(mp_layout.lmargin));
		if (!END_OF_COMMAND && equals(c_token,",") ) {
		    c_token++;
		    if (END_OF_COMMAND)
			int_error(c_token, "expecting <right>");

		    mp_layout.rmargin.scalex = mp_layout.lmargin.scalex;
		    mp_layout_set_margin_or_spacing(&(mp_layout.rmargin));
		} else {
		    int_error(c_token, "expecting <right>");
		}
		if (!END_OF_COMMAND && equals(c_token,",") ) {
		    c_token++;
		    if (END_OF_COMMAND)
			int_error(c_token, "expecting <top>");

		    mp_layout.bmargin.scalex = mp_layout.rmargin.scalex;
		    mp_layout_set_margin_or_spacing(&(mp_layout.bmargin));
		} else {
		    int_error(c_token, "expecting <bottom>");
		}
		if (!END_OF_COMMAND && equals(c_token,",") ) {
		    c_token++;
		    if (END_OF_COMMAND)
			int_error(c_token, "expecting <bottom>");

		    mp_layout.tmargin.scalex = mp_layout.bmargin.scalex;
		    mp_layout_set_margin_or_spacing(&(mp_layout.tmargin));
		} else {
		    int_error(c_token, "expecting <top>");
		}
		set_margins = TRUE;
		break;
	    case S_MULTIPLOT_SPACING:
		c_token++;
		if (END_OF_COMMAND)
		    int_error(c_token,"expecting '<xspacing>,<yspacing>'");
		mp_layout.xspacing.scalex = screen;
		mp_layout_set_margin_or_spacing(&(mp_layout.xspacing));
		mp_layout.yspacing = mp_layout.xspacing;

		if (!END_OF_COMMAND && equals(c_token, ",")) {
		    c_token++;
		    if (END_OF_COMMAND)
			int_error(c_token, "expecting <yspacing>");
		    mp_layout_set_margin_or_spacing(&(mp_layout.yspacing));
		}
		set_spacing = TRUE;
		break;
	    default:
		int_error(c_token,"invalid or duplicate option");
		break;
	}
    }

    if (set_spacing || set_margins) {
	if (set_spacing && set_margins) {
	    if (mp_layout.lmargin.x >= 0 && mp_layout.rmargin.x >= 0 
	    &&  mp_layout.tmargin.x >= 0 && mp_layout.bmargin.x >= 0 
	    &&  mp_layout.xspacing.x >= 0 && mp_layout.yspacing.x >= 0)
		mp_layout.auto_layout_margins = TRUE;
	    else
		int_error(NO_CARET, "must give positive margin and spacing values");
	} else if (set_margins) {
	    mp_layout.auto_layout_margins = TRUE;
	    mp_layout.xspacing.scalex = screen;
	    mp_layout.xspacing.x = 0.05;
	    mp_layout.yspacing.scalex = screen;
	    mp_layout.yspacing.x = 0.05;
	}
	/* Sanity check that screen tmargin is > screen bmargin */
	if (mp_layout.bmargin.scalex == screen && mp_layout.tmargin.scalex == screen)
	    if (mp_layout.bmargin.x > mp_layout.tmargin.x) {
		double tmp = mp_layout.bmargin.x;
		mp_layout.bmargin.x = mp_layout.tmargin.x;
		mp_layout.tmargin.x = tmp;
	    }
    }

    /* If we reach here, then the command has been successfully parsed.
     * Call term_start_plot() before setting multiplot so that
     * the wxt and qt terminals will reset the plot count to 0 before
     * ignoring subsequent TERM_LAYER_RESET requests. 
     */
    term_start_plot();
    in_multiplot = lf_head ? lf_head->depth + 1 : 1;
    multiplot_count = 0;
    fill_gpval_integer("GPVAL_MULTIPLOT", 1);
    init_multiplot_datablock();

    /* Place overall title before doing anything else */
    if (mp_layout.title.text) {
	unsigned int x, y;
	char *p = mp_layout.title.text;

	x = term->xmax  / 2;
	y = term->ymax - term->v_char;

	write_label(x, y, &(mp_layout.title));
	reset_textcolor(&(mp_layout.title.textcolor));

	/* Calculate fractional height of title compared to entire page */
	/* If it would fill the whole page, forget it! */
	for (y=1; *p; p++)
	    if (*p == '\n')
		y++;

	/* Oct 2012 - v_char depends on the font used */
	if (mp_layout.title.font && *mp_layout.title.font)
	    term->set_font(mp_layout.title.font);
	mp_layout.title_height = (double)(y * term->v_char) / (double)term->ymax;
	if (mp_layout.title.font && *mp_layout.title.font)
	    term->set_font("");

	if (mp_layout.title_height > 0.9)
	    mp_layout.title_height = 0.05;
    } else {
	mp_layout.title_height = 0.0;
    }

    multiplot_reset();
}

void
multiplot_end()
{
    in_multiplot = 0;
    multiplot_count = 0;
    fill_gpval_integer("GPVAL_MULTIPLOT", 0);
    /* reset plot size, origin and margins to values before 'set
       multiplot layout' */
    if (mp_layout.auto_layout) {
	xsize = mp_layout.prev_xsize;
	ysize = mp_layout.prev_ysize;
	xoffset = mp_layout.prev_xoffset;
	yoffset = mp_layout.prev_yoffset;

	lmargin = mp_layout.prev_lmargin;
	rmargin = mp_layout.prev_rmargin;
	bmargin = mp_layout.prev_bmargin;
	tmargin = mp_layout.prev_tmargin;
    }
    /* reset automatic multiplot layout */
    mp_layout.auto_layout = FALSE;
    mp_layout.auto_layout_margins = FALSE;
    mp_layout.xscale = mp_layout.yscale = 1.0;
    mp_layout.xoffset = mp_layout.yoffset = 0.0;
    mp_layout.lmargin.scalex = mp_layout.rmargin.scalex = screen;
    mp_layout.bmargin.scalex = mp_layout.tmargin.scalex = screen;
    mp_layout.lmargin.x = mp_layout.rmargin.x = mp_layout.bmargin.x = mp_layout.tmargin.x = -1;
    mp_layout.xspacing.scalex = mp_layout.yspacing.scalex = screen;
    mp_layout.xspacing.x = mp_layout.yspacing.x = -1;

    if (mp_layout.title.text) {
	free(mp_layout.title.text);
	mp_layout.title.text = NULL;
    }

    if (!multiplot_playback) {
	/* Create or recycle a user-visible datablock for replay */
	struct udvt_entry *datablock = add_udv_by_name("$GPVAL_LAST_MULTIPLOT");
	free_value(&datablock->udv_value);

	/* Add the closing "unset multiplot" command before saving */
	append_to_datablock(&multiplot_udv, strdup("unset multiplot"));

	datablock->udv_value = multiplot_udv;
	multiplot_udv.v.data_array = NULL;

	/* Save panel number of last-drawn plot */
	multiplot_last_panel = mp_layout.current_panel;
    }
    last_plot_was_multiplot = TRUE;
}

/* Helper functions for multiplot auto layout to issue size and offset cmds */
TBOOLEAN multiplot_auto()
{
    return mp_layout.auto_layout_margins;
}

void
multiplot_reset()
{
    if (mp_layout.auto_layout_margins)
	mp_layout_margins_and_spacing();
    else
	mp_layout_size_and_offset();
}

void
multiplot_use_size_and_origin()
{
    panel_bounds.xleft = xoffset * term->xmax;
    panel_bounds.xright = panel_bounds.xleft + xsize * term->xmax;
    panel_bounds.ybot = yoffset * term->ymax;
    panel_bounds.ytop = panel_bounds.ybot + ysize * term->ymax;
}

static void
mp_layout_size_and_offset(void)
{
    if (!mp_layout.auto_layout)
	return;

    /* the 'set size' command */
    xsize = mp_layout.xscale / mp_layout.num_cols;
    ysize = mp_layout.yscale / mp_layout.num_rows;

    /* the 'set origin' command */
    xoffset = (double)(mp_layout.act_col) / mp_layout.num_cols;
    if (mp_layout.downwards)
	yoffset = 1.0 - (double)(mp_layout.act_row+1) / mp_layout.num_rows;
    else
	yoffset = (double)(mp_layout.act_row) / mp_layout.num_rows;

    /* Allow a little space at the top for a title */
    if (mp_layout.title.text) {
	ysize *= (1.0 - mp_layout.title_height);
	yoffset *= (1.0 - mp_layout.title_height);
    }

    /* corrected for x/y-scaling factors and user defined offsets */
    xoffset -= (mp_layout.xscale-1)/(2*mp_layout.num_cols);
    yoffset -= (mp_layout.yscale-1)/(2*mp_layout.num_rows);
    /* fprintf(stderr,"  xoffset==%g  yoffset==%g\n", xoffset,yoffset); */
    xoffset += mp_layout.xoffset;
    yoffset += mp_layout.yoffset;

    /* At this point we know the boundary of the next multiplot panel to be drawn.
     * Save this somewhere for use by "clear"; maybe also for subsequent mousing.
     */
    panel_bounds.xleft = xoffset * term->xmax;
    panel_bounds.xright = (xoffset + xsize) * term->xmax;
    panel_bounds.ybot = yoffset * term->ymax;
    panel_bounds.ytop = (yoffset + ysize) * term->ymax;
    FPRINTF((stderr, "next multiplot panel\t%d\t%d\t%d\t%d\n",
	    panel_bounds.xleft, panel_bounds.xright, panel_bounds.ybot, panel_bounds.ytop));
}

/* Helper function for multiplot auto layout to set the explicit plot margins, 
   if requested with 'margins' and 'spacing' options. */
static void
mp_layout_margins_and_spacing(void)
{
    /* width and height of a single sub plot. */
    double tmp_width, tmp_height;
    double leftmargin, rightmargin, topmargin, bottommargin, xspacing, yspacing;

    if (!mp_layout.auto_layout_margins) return;

    if (mp_layout.lmargin.scalex == screen)
	leftmargin = mp_layout.lmargin.x;
    else
	leftmargin = (mp_layout.lmargin.x * term->h_char) / term->xmax;

    if (mp_layout.rmargin.scalex == screen)
	rightmargin = mp_layout.rmargin.x;
    else
	rightmargin = 1 - (mp_layout.rmargin.x * term->h_char) / term->xmax;

    if (mp_layout.tmargin.scalex == screen)
	topmargin = mp_layout.tmargin.x;
    else
	topmargin = 1 - (mp_layout.tmargin.x * term->v_char) / term->ymax;

    if (mp_layout.bmargin.scalex == screen)
	bottommargin = mp_layout.bmargin.x;
    else
	bottommargin = (mp_layout.bmargin.x * term->v_char) / term->ymax;

    if (mp_layout.xspacing.scalex == screen)
	xspacing = mp_layout.xspacing.x;
    else
	xspacing = (mp_layout.xspacing.x * term->h_char) / term->xmax;

    if (mp_layout.yspacing.scalex == screen)
	yspacing = mp_layout.yspacing.x;
    else
	yspacing = (mp_layout.yspacing.x * term->v_char) / term->ymax;

    tmp_width = (rightmargin - leftmargin - (mp_layout.num_cols - 1) * xspacing) 
	        / mp_layout.num_cols;
    tmp_height = (topmargin - bottommargin - (mp_layout.num_rows - 1) * yspacing) 
	        / mp_layout.num_rows;

    lmargin.x = leftmargin + mp_layout.act_col * (tmp_width + xspacing);
    lmargin.scalex = screen;
    rmargin.x = lmargin.x + tmp_width;
    rmargin.scalex = screen;

    if (mp_layout.downwards) {
	bmargin.x = bottommargin + (mp_layout.num_rows - mp_layout.act_row - 1) 
	            * (tmp_height + yspacing);
    } else {
	bmargin.x = bottommargin + mp_layout.act_row * (tmp_height + yspacing);
    }
    bmargin.scalex = screen;
    tmargin.x = bmargin.x + tmp_height;
    tmargin.scalex = screen;

    /* At this point we know the boundary of the next multiplot panel to be drawn.
     * Save this somewhere for use by "clear"; maybe also for subsequent mousing.
     */
    panel_bounds.xleft = mp_layout.act_col * term->xmax / mp_layout.num_cols;
    panel_bounds.xright = panel_bounds.xleft + term->xmax / mp_layout.num_cols;
    panel_bounds.ytop = term->ymax - mp_layout.act_row * term->ymax / mp_layout.num_rows;
    panel_bounds.ybot = panel_bounds.ytop - term->ymax / mp_layout.num_rows;
    FPRINTF((stderr, "next multiplot panel:\t%d\t%d\t%d\t%d\n",
	    panel_bounds.xleft, panel_bounds.xright, panel_bounds.ybot, panel_bounds.ytop));
}

static void
mp_layout_set_margin_or_spacing(t_position *margin)
{
    margin->x = -1;

    if (END_OF_COMMAND)
	return;

    if (almost_equals(c_token, "sc$reen")) {
	margin->scalex = screen;
	c_token++;
    } else if (almost_equals(c_token, "char$acter")) {
	margin->scalex = character;
	c_token++;
    }

    margin->x = real_expression();
    if (margin->x < 0)
	margin->x = -1;

    if (margin->scalex == screen) {
	if (margin->x < 0)
	    margin->x = 0;
	if (margin->x > 1)
	    margin->x = 1;
    }
}

/* Begin accummulating lines that can later be replayed by replay_multiplot().
 * The first line to be stored is a comment.
 * Subsequent lines will be appended as they are read in.
 */
static void
init_multiplot_datablock()
{
    gpfree_datablock(&multiplot_udv);
    multiplot_udv.type = DATABLOCK;
    append_to_datablock(&multiplot_udv, strdup("# saved multiplot"));
}

/* Append one line to the multiplot history.
 * Called from two places:
 *	command.c:com_line() catches direct input from stdin
 *	misc.c:load_file() catches lines from load/call
 * When the multiplot is exited via multiplot_end(), all lines will be
 * copied to the user-visible datblock $GPVAL_LAST_MULTIPLOT.
 */
void
append_multiplot_line(char *line)
{
    if (line == NULL || *line == '\0')
	return;
    if (*line == '$' && strstr(line,"<<"))
	return;
    if (suppress_multiplot_save)
	return;

    append_to_datablock(&multiplot_udv, strdup(line));
}

/* This is the implementation of "remultiplot".
 * The load operation is protected by a flag to prevent infinite recursion.
 */
void
replay_multiplot()
{
    multiplot_playback = TRUE;
    load_file(NULL, strdup("$GPVAL_LAST_MULTIPLOT"), 6);
    multiplot_playback = FALSE;
}

/* reset needed if int_error() is invoked during multiplot playback */
void
multiplot_reset_after_error()
{
    if (!multiplot_playback)
	return;
    multiplot_end();
    multiplot_playback = FALSE;
    suppress_multiplot_save = FALSE;
}
