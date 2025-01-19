/* Support routines for "set mark" "plot with marks" and marks in general */

/*[
 * Copyright Hiroki Motoyoshi 2024
 *
 * Gnuplot license:
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
]*/

#include "alloc.h"
#include "command.h"
#include "datafile.h"
#include "graphics.h"
#include "marks.h"
#include "misc.h"
#include "save.h"


/* Pointer to first mark instance in linked list */
struct mark_data *first_mark = NULL;

/* used by df_generate_pseudodata() if mark has sampling range */
struct udvt_entry *mark_sample_var = NULL;

/* Local prototypes */
static struct mark_data * read_mark_data(void);
static void mark_append(struct mark_data *dst, struct mark_data *src);
static struct mark_data * mark_allocate (int size);
static TBOOLEAN set_mark_properties( struct mark_data *mark );

/*
 * Utility routine to search through list to find a mark
 * with the requested tag.
 */
struct mark_data *
get_mark(struct mark_data *first, int tag)
{
    struct mark_data *this;

    for (this=first; this!=NULL; this=this->next) {
        if (tag == this->tag)
            return this;
    }
    return NULL;
}


/*
   The argument 'plot' can be NULL if the mark is drawn as part of an object
 */
void
do_mark (struct mark_data *mark,
	 double x, double y, double xscale, double yscale, double angle,
	 int units,
	 TBOOLEAN clip,
	 struct fill_style_type *parent_fill_properties,
	 struct lp_style_type *parent_lp_properties,
	 struct curve_points *plot, double varcolor,
	 int max_vertices, gpiPoint *vertex, gpiPoint *fillarea)
{
    double aspect = (double)term->v_tic / (double)term->h_tic;
    struct polygon *polygon;
    struct fill_style_type *my_fillstyle;
    unsigned int my_strokergb;
    t_colorspec *my_fillcolor;
    double cosa, sina, rada;
    double dx, dy, mx1, my1, mx2, my2;
    int draw_style;
    int points, in;
    int k;
    TBOOLEAN withborder = FALSE;
    TBOOLEAN has_varcolor = FALSE;
    TBOOLEAN has_bordercolor = FALSE;

    if (!mark)
       return;

    polygon = &(mark->polygon);
    mx1 = mark->xmin;
    mx2 = mark->xmax;
    my1 = mark->ymin;
    my2 = mark->ymax;

    /* Fill style is taken from the mark structure if specified by "set mark",
     * otherwise it is inherited from the parent object or plot.
     */
    if (mark->mark_fillstyle.fillstyle != FS_DEFAULT)
	my_fillstyle = &mark->mark_fillstyle;
    else
	my_fillstyle = parent_fill_properties;

    /* Fill color is taken from the mark structure if specified by "set mark",
     * otherwise it is inherited from the parent object or plot.
     */
    if (mark->mark_fillcolor.type != TC_DEFAULT) {
	my_fillcolor = &mark->mark_fillcolor;
    } else if (!plot) {
	/* parent must be an object */
	my_fillcolor = &parent_lp_properties->pm3d_color;
    } else if (check_for_variable_color(plot, 0)) {
	/* this check applies a color immediately, but we will need to re-apply it later */
	apply_variable_color(plot, &varcolor);
	has_varcolor = TRUE;
	my_fillcolor = NULL;	/* should not be used! */
    } else {
	/* parent must be a plot without varcolor */
	my_fillcolor = &parent_lp_properties->pm3d_color;
    }

    /* Stroke color is taken from the mark border color if specified by "set mark",
     * otherwise it is inherited from the parent object or plot.
     */
    if (mark->mark_fillstyle.border_color.type != TC_DEFAULT) {
	has_bordercolor = TRUE;
	my_strokergb = rgb_from_colorspec(&mark->mark_fillstyle.border_color);
    } else if (parent_fill_properties->border_color.type == TC_DEFAULT)
	my_strokergb = rgb_from_colorspec(&parent_lp_properties->pm3d_color);
    else {
	if (parent_fill_properties->border_color.type == TC_LT
	&&  parent_fill_properties->border_color.lt == LT_NODRAW)
	    my_strokergb = 0xffffffff;
	else {
	    has_bordercolor = TRUE;
	    my_strokergb = rgb_from_colorspec(&parent_fill_properties->border_color);
        }
    }

    /* Check border should be drawn */
    if (my_fillstyle->border_color.type == TC_LT && my_fillstyle->border_color.lt == LT_NODRAW)
	withborder = FALSE;
    else
	withborder = TRUE;

    switch (units) {
    case MARK_UNITS_XY:
	xscale = map_x_double(xscale) - map_x_double(0.0);
	yscale = map_y_double(yscale) - map_y_double(0.0);
	break;
    case MARK_UNITS_XX:
	xscale = map_x_double(xscale) - map_x_double(0.0);
	yscale = map_x_double(yscale) - map_x_double(0.0);
	break;
    case MARK_UNITS_YY:
	xscale = map_y_double(xscale) - map_y_double(0.0);
	yscale = map_y_double(yscale) - map_y_double(0.0);
	break;
    case MARK_UNITS_GXY:
	xscale = xscale*(plot_bounds.xright - plot_bounds.xleft);
	yscale = yscale*(plot_bounds.ytop - plot_bounds.ybot);
	break;
    case MARK_UNITS_GXX:
	xscale = xscale*(plot_bounds.xright - plot_bounds.xleft);
	yscale = yscale*(plot_bounds.xright - plot_bounds.xleft);
	break;
    case MARK_UNITS_GYY:
	xscale = xscale*(plot_bounds.ytop - plot_bounds.ybot);
	yscale = yscale*(plot_bounds.ytop - plot_bounds.ybot);
	break;
    case MARK_UNITS_PS:
	xscale = term->h_tic * xscale;
	yscale = term->v_tic * yscale;
	break;
    default:
	break;
    }

    rada = angle*M_PI/180.0*theta_direction;
    cosa = cos(rada);
    sina = sin(rada);

    if (clip) {
	double xx[5] = { x,
		         x + xscale*mx1 * cosa - yscale*my1 * sina,
		         x + xscale*mx1 * cosa - yscale*my2 * sina,
		         x + xscale*mx2 * cosa - yscale*my1 * sina,
		         x + xscale*mx2 * cosa - yscale*my2 * sina };
	double yy[5] = { y,
		         y + xscale*mx1 * sina + yscale*my1 * cosa,
		         y + xscale*mx1 * sina + yscale*my2 * cosa,
		         y + xscale*mx2 * sina + yscale*my1 * cosa,
		         y + xscale*mx2 * sina + yscale*my2 * cosa };
	TBOOLEAN is_inrange = FALSE;
	for (k=0; k<5; k++) {
	    if ( inrange(round(xx[k]), clip_area->xleft, clip_area->xright)
	    &&   inrange(round(yy[k]), clip_area->ybot, clip_area->ytop) ) {
		is_inrange = TRUE;
		break;
	    }
	}
	if ( is_inrange == FALSE )
	    return;
    }

    k = 0;
    points = 0;
    while ( k < mark->vertices ) {

	for ( ; k < mark->vertices; k++) {
	    dx = xscale * polygon->vertex[k].x;
	    dy = yscale * polygon->vertex[k].y;
	    if ( isnan(dx) || isnan(dy) ) {
	        k++;
		break;
	    }
	    vertex[points].x = round(x + dx * cosa - dy * sina);
	    vertex[points].y = round(y + (dx * sina + dy * cosa) * aspect);
	    if (points == 0) {
		/* stroke/fill properties for this set of vertices */
	        draw_style = round(polygon->vertex[k].z);
	    }
	    points++;
	}

	if ( points > 1 ) {

	    /* Fill polygon
	     *
	     * if any of these conditions holds for the current set of vertices
	     * - the mode is MARKS_FILL
	     * - the mode is MARKS_FILL_STROKE
	     * - the mode is MARK_FILLSTYLE and the fillstyle is not FS_EMPTY
	     * - the mode is MARK_FILL_BACKGROUND
	     */

	    /* Background fill */
	    if (draw_style == MARKS_FILL_BACKGROUND) {
	        clip_polygon(vertex, fillarea, points, &in);
	        if (in > 1 && term->filled_polygon) {
		    t_colorspec background = {.type=TC_LT, .lt=LT_BACKGROUND};
		    apply_pm3dcolor(&background);
		    fillarea[0].style = FS_OPAQUE;
	            term->filled_polygon(in, fillarea);
		}
	    }

	    /* Normal fill */
	    if ((draw_style == MARKS_FILL)
	    ||  (draw_style == MARKS_FILL_STROKE)
	    ||  ((draw_style == MARKS_FILLSTYLE) && (my_fillstyle->fillstyle != FS_EMPTY))) {
	        clip_polygon(vertex, fillarea, points, &in);
	        if (in > 1 && term->filled_polygon) {
		    if (has_varcolor)
			apply_variable_color(plot, &varcolor);
		    else
			apply_pm3dcolor(my_fillcolor);
		    fillarea[0].style = style_from_fill(my_fillstyle);
		    /* Special case: inherited fillstyle is "empty" but mode is FILL */
		    if (my_fillstyle->fillstyle == FS_EMPTY)
			fillarea[0].style = FS_OPAQUE;
	            term->filled_polygon(in, fillarea);
	        }
	    }

	    /* Stroke polygon
	     *
	     * if any of these conditions holds for the current set of vertices
	     * - the mode is MARKS_STROKE
	     * - the mode is MARKS_FILL_STROKE
	     * - the mode is MARK_FILLSTYLE and the fillstyle is not set to
	     *   "noborder"
	     */
	    if ((draw_style == MARKS_STROKE)
	    ||  (draw_style == MARKS_FILL_STROKE)
	    ||  ((draw_style == MARKS_FILLSTYLE) && withborder)) {
		if (has_bordercolor)
		    set_rgbcolor_const(my_strokergb);
		else if (has_varcolor)
		    apply_variable_color(plot, &varcolor);
		else
		    set_rgbcolor_const(my_strokergb);
		if (my_strokergb != 0xffffffff)	/* forced stroke but stroke is LT_NODRAW */
		    draw_clip_polygon(points, vertex);
	    }

	}
	points = 0;
    }
}

/*  process 'set mark' command
 *      'set mark <tag> DATASPEC'
 *      'set mark <tag> append DATASPEC'
 */

static struct mark_data *
mark_allocate (int size)
{
    struct mark_data *mark;
    struct fill_style_type mark_fillstyle = DEFAULT_MARK_FILLSTYLE;
    t_colorspec mark_fillcolor = DEFAULT_COLORSPEC;

    if (size > MARK_MAX_VERTICES)
	int_error(NO_CARET, "too many vertices (> %d) in a mark", MARK_MAX_VERTICES);
    mark = gp_alloc(sizeof(struct mark_data), "mark_data");
    mark->next = NULL;
    mark->tag = -1;
    mark->asize = size;
    mark->vertices = 0;
    mark->title = NULL;
    if (size > 0) {
	mark->polygon.vertex = (t_position *) gp_alloc(size*sizeof(t_position), "mark vertex");
    } else {
	mark->polygon.vertex = NULL;
    }
    mark->xmin = VERYLARGE;
    mark->xmax = -VERYLARGE;
    mark->ymin = VERYLARGE;
    mark->ymax = -VERYLARGE;
    mark->mark_fillstyle = mark_fillstyle;
    mark->mark_fillcolor = mark_fillcolor;
    return mark;
}

static struct mark_data *
mark_reallocate (struct mark_data *mark, int size) 
{
    if (size > MARK_MAX_VERTICES)
	int_error(NO_CARET, "too many vertices (> %d) in a mark", MARK_MAX_VERTICES);
    if (size > 0) {
	mark->asize = size;
	mark->polygon.vertex = (t_position *) gp_realloc(mark->polygon.vertex,
						size*sizeof(t_position), "mark vertex");
    } else {
	free(mark->polygon.vertex);
	free(mark->title);
	mark->asize = 0;
	mark->polygon.vertex = NULL;
	mark->title = NULL;
    }
    return mark;
}

void
free_mark (struct mark_data *mark)
{
    if (mark) {
	mark_reallocate(mark, 0);
	free(mark);
    }
}

/*
 * updates the range of the enclosure (xmin,xmax,ymin,ymax) of polygons
 *             in the mark_data structure based on the current polygon data.
 */
static void
mark_update_limits (struct mark_data *mark)
{
    double mx1 = 10e30;
    double mx2 = -10e30;
    double my1 = 10e30;
    double my2 = -10e30;
    double mx, my;
    int i;

    for (i=0; i<mark->vertices; i++) {
        mx = mark->polygon.vertex[i].x;
        my = mark->polygon.vertex[i].y;
        mx1 = (mx < mx1) ? mx : mx1;
        mx2 = (mx > mx2) ? mx : mx2;
        my1 = (my < my1) ? my : my1;
        my2 = (my > my2) ? my : my2;
    }

    mark->xmin = mx1;
    mark->xmax = mx2;
    mark->ymin = my1;
    mark->ymax = my2;        
}

static void
mark_append(struct mark_data *dst, struct mark_data *src)
{
    int vertices;
    int dst_vertices = dst->vertices;
    int src_vertices = src->vertices;

    vertices = dst_vertices + 1 + src_vertices;
    dst = mark_reallocate(dst, vertices);
    dst->vertices = vertices;

    dst->polygon.vertex[dst_vertices].x = not_a_number();
    dst->polygon.vertex[dst_vertices].y = not_a_number();

    memcpy(dst->polygon.vertex+dst_vertices+1, src->polygon.vertex, (src_vertices)*sizeof(t_position));

    if (dst->xmin > src->xmin)
	dst->xmin = src->xmin;
    if (dst->xmax < src->xmax)
	dst->xmax = src->xmax;
    if (dst->ymin > src->ymin)
	dst->ymin = src->ymin;
    if (dst->ymax < src->ymax)
	dst->ymax = src->ymax;
}

/* Read optional mark fillstyle and fill color from the command line.
 * Returns TRUE if anything was set
 */
static TBOOLEAN
set_mark_properties( struct mark_data *mark )
{
    int original_token = c_token;

    while (!END_OF_COMMAND) {
	int save_token = c_token;
	parse_fillstyle(&mark->mark_fillstyle);
	if ((equals(c_token,"fc") || almost_equals(c_token,"fillc$olor")))
	    parse_colorspec(&mark->mark_fillcolor, TC_Z);
	if (almost_equals(c_token,"ti$tle")) {
	    c_token++;
	    mark->title = try_to_get_string();
	}
	if (c_token == save_token)
	    break;
    }
    return (c_token != original_token);
}

struct mark_data *
push_mark(struct mark_data *first, struct mark_data *mark)
{
    struct mark_data *this, *prev;
    if ( !first )
	first = mark;
    for (this=first, prev=NULL; this!=NULL; prev=this, this=this->next)
	;
    prev->next = mark;
    return mark;
}

static struct mark_data *
read_mark_data()
{
    int sample_range_token;
    t_value original_value_sample_var;
    struct mark_data *mark;
    t_position *vertex;
    double v[4];
    char *name_str;
    int lines, ierr;
    int j;

    /* Check for a sampling range. */
    mark_sample_var = NULL;
    init_sample_range(axis_array + FIRST_X_AXIS, DATA);
    sample_range_token = parse_range(SAMPLE_AXIS);
    if (sample_range_token != 0)
	axis_array[SAMPLE_AXIS].range_flags |= RANGE_SAMPLED;
    if (sample_range_token > 0) {
	mark_sample_var = add_udv(sample_range_token);
	original_value_sample_var = mark_sample_var->udv_value;
    }

    if ( ! (name_str = string_or_express(NULL)) ) /* WARNING: do NOT free name_str */
	int_error(c_token, "unrecognized data source for mark");

    /* Same interlock as plot/splot/stats */
    inside_plot_command = TRUE;

    df_set_plot_mode(MODE_QUERY);	/* Needed only for binary datafiles */
    ierr = df_open(name_str, 4, NULL);
    if (ierr < 0)
	int_error(NO_CARET, "could not open %s", name_str);

    if (!strcmp(df_filename,"++"))
	int_error(c_token, "pseudofile \"++\" can not be used for 'set mark'");

    mark = mark_allocate(0xff);
    vertex = mark->polygon.vertex;

    lines = 0;
    while ((j = df_readline(v, 4)) != DF_EOF) {
        if ( lines > mark->asize-1 ) {
            mark = mark_reallocate(mark, mark->asize+0xff);
            vertex = mark->polygon.vertex;
        }
	if (j > 3)	/* Ignore any excess using specs */
	    j = 3;
	switch (j) {
	case 2:
	    vertex[lines].x = v[0];
	    vertex[lines].y = v[1];
	    vertex[lines].z = MARKS_FILLSTYLE;
	    lines++;
	    break;
	case 3:
	    vertex[lines].x = v[0];
	    vertex[lines].y = v[1];
	    vertex[lines].z = v[2];	/* mode */
	    lines++;
	    break;
	case DF_FIRST_BLANK:
        case DF_UNDEFINED:
        case DF_MISSING:
            vertex[lines].x = not_a_number();
            vertex[lines].y = not_a_number();
            vertex[lines].z = not_a_number();
            lines++;
            break;
        case DF_SECOND_BLANK:
        case DF_COLUMN_HEADERS:
            continue;
            break;
        default:
            df_close();
            int_error(c_token, "Bad data on line %d", df_line_number);
            break;
        }
    }
    df_close();

    if (lines == 0)
	int_warn(NO_CARET, "no vertices read from file");

    mark = mark_reallocate(mark, lines);
    mark->vertices = lines;
    mark_update_limits(mark);

    inside_plot_command = FALSE;

    /* restore original value of sampling variable */
    if (sample_range_token > 0) {
	mark_sample_var->udv_value = original_value_sample_var;
	mark_sample_var = NULL;
    }

    return mark;
}

/* process 'set mark' command
 * set mark tag {<data source> | empty}
 *              {fill | fs <fillstyle>}
 *              {fillcolor | fc <colorspec>}
 *              {title <text>}
 * set mark append <data source>
 */

void
set_mark()
{
    int tag;
    struct mark_data *mark, *this;
    TBOOLEAN append = FALSE;

    c_token++;
    tag = int_expression();

    if (tag < 0)
       int_error(NO_CARET, "tag must be >= 0");

    /* Check for modification of existing mark properties */
    mark = get_mark(first_mark, tag);
    if (mark && set_mark_properties(mark))
	return;

    if (equals(c_token, "empty")) {
        c_token++;
        mark = mark_allocate(0);
        mark->tag = tag;

    } else {
	if (equals(c_token, "append")) {
	    c_token++;
	    append = TRUE;
	}
	/* The usual case; allocate mark and fill in the vertices */
	mark = read_mark_data();
	mark->tag = tag;
    } 

    if (append) {
	if (first_mark && (this = get_mark(first_mark, tag))) {
	    mark_append(this, mark);
	    free_mark(mark);	
	} else {
       	    int_error(c_token, "attempted to append data to undefined mark tag %d", tag);		
	}
	return;
    }

    set_mark_properties(mark);

    if (!first_mark) {  /* the mark list is empty */
	first_mark = mark;
    } else {
	this = get_mark(first_mark, tag);
	if (!this) {    /* no existing mark with the specified tag */
	    push_mark(first_mark, mark);
	} else {        /* replace content of old mark that had this tag */
	    struct mark_data *save_next = this->next;
	    mark_reallocate(this, -1);
	    *this = *mark;
	    this->next = save_next;
	    free(mark);
	}
    }
}

/*
 * Function for drawing key shape for marks and linesmarks styles
 */
void
do_key_sample_mark(struct curve_points *this_plot, int xl, int yl, int tag)
{
    struct mark_data *mark;
    double size = 1.0;
    int vertices;
    gpiPoint *vertex;
    gpiPoint *fillarea;
    double xscale;
    double yscale;

    if (this_plot->lp_properties.p_size > 0)
	size = this_plot->lp_properties.p_size;
    else
	size = pointsize;

    mark = get_mark(first_mark, tag);

    /* mark is found! */
    if (mark) {
	double width = mark->xmax - mark->xmin;
	double height = mark->ymax - mark->ymin;
	BoundingBox *clip_save = clip_area;

	clip_area = &canvas;

	/* Scaling */
	if (this_plot->marks_options.units == MARK_UNITS_PS) {
	    xscale = 1.0;
	    yscale = 1.0;
	} else {
	    if (width > height) {
		xscale = 1.0/width;
		yscale = 1.0/width;
	    } else {
		xscale = 1.0/height;
		yscale = 1.0/height;
	    }
	}

	xscale = xscale * size;
	yscale = yscale * size;

	/* Drawing a mark at key sample area */
	vertices = mark->vertices;
	vertex   = (gpiPoint *) gp_alloc(vertices*sizeof(gpiPoint), "draw mark");
	fillarea = (gpiPoint *) gp_alloc(2*vertices*sizeof(gpiPoint), "draw mark");
	do_mark(mark,
	    xl, yl, xscale, yscale, 0.0,
	    MARK_UNITS_PS,
	    FALSE,
	    &(this_plot->fill_properties),
	    &(this_plot->lp_properties),
	    NULL, 0,
	    vertices, vertex, fillarea);
	free(vertex);
	free(fillarea);

	clip_area = clip_save;
    }
}

/* Called by "save" command */
void
save_marks(FILE *fp)
{
    struct mark_data *this;
    int i, tag;
    double x, y, z;
    for (this=first_mark; this != NULL; this=this->next) {
        tag = this->tag;
        fprintf(fp, "$MARK_%i <<EOM\n", tag);
        for (i=0; i<this->vertices; i++) {
            x = this->polygon.vertex[i].x;
            y = this->polygon.vertex[i].y;
            z = this->polygon.vertex[i].z;
            if (isnan(x) || isnan(y))
		fprintf(fp, "\n");
            else
		fprintf(fp, "%g\t%g\t%i\n", x, y, (int) round(z));
        }
        fprintf(fp, "EOM\n");
        fprintf(fp, "set mark %i $MARK_%i ", tag, tag);
	if (this->title)
	    fprintf(fp, "title \"%s\" ", this->title);
	if (this->mark_fillcolor.type != TC_DEFAULT) {
	    fprintf(fp, "fillcolor ");
	    save_pm3dcolor(fp, &this->mark_fillcolor);
	}
	if (this->mark_fillstyle.fillstyle == FS_DEFAULT
	&&  this->mark_fillstyle.border_color.type == TC_DEFAULT) {
	    fprintf(fp, "\n");
	} else {
	    fprintf(fp, " fillstyle ");
	    save_fillstyle(fp, &this->mark_fillstyle);
	}
        fprintf(fp, "undefine $MARK_%i\n", tag);
        fprintf(fp, "\n");
    }
}

/* process 'unset mark' command */
void
delete_mark(struct mark_data *prev, struct mark_data *this)
{
    if (this != NULL) {		/* there really is something to delete */
	if (prev != NULL)	/* there is a previous rectangle */
	    prev->next = this->next;
	else			/* this = first_object so change first_object */
	    first_mark = this->next;
	/* NOTE:  Must free contents as well */
	free_mark(this);
    }
}

void
clear_mark()
{
    /* delete all marks */
    while (first_mark != NULL)
	delete_mark((struct mark_data *) NULL, first_mark);
}

void
unset_mark()
{
    int tag;
    struct mark_data *this, *prev;

    if (END_OF_COMMAND) {
	clear_mark();
	return;
    }

    tag = int_expression();
    for (this = first_mark, prev = NULL; this != NULL;
	 prev = this, this = this->next) {
	if (this->tag == tag) {
	    delete_mark(prev, this);
	    break;
	}
    }
}

/* Called from "show" */
void
show_one_mark(struct mark_data *mark)
{
    fprintf(stderr, "\tmarktype %i ", mark->tag);
    if (mark->title)
	fprintf(stderr, " title \"%s\" ", mark->title);
    fprintf(stderr, "polygon vertices %i ", mark->vertices);
    if (mark->mark_fillcolor.type != TC_DEFAULT) {
	fprintf(stderr, "fillcolor ");
	save_pm3dcolor(stderr, &mark->mark_fillcolor);
    }
    fprintf(stderr, " fillstyle ");
    save_fillstyle(stderr, &mark->mark_fillstyle);
    fprintf(stderr, "\n");
}

void
show_mark()
{
    int tag = -1;
    struct mark_data *this;

    if (!END_OF_COMMAND)
	tag = int_expression();

    if (tag < 0) {
        for (this = first_mark; this != NULL; this = this->next) {
	    show_one_mark(this);
        }
        return;
    }

    this = get_mark(first_mark, tag);
    if (!this)
	return;

    show_one_mark(this);
}

