/* GNUPLOT - pm3d.c */

/*[
 *
 * Petr Mikulik, since December 1998
 * Copyright: open source as much as possible
 *
 * What is here: global variables and routines for the pm3d splotting mode.
 *
]*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "pm3d.h"
#include "alloc.h"
#include "getcolor.h"		/* for rgb1_from_gray */
#include "graphics.h"
#include "hidden3d.h"		/* p_vertex & map3d_xyz() */
#include "plot2d.h"
#include "plot3d.h"
#include "setshow.h"		/* for surface_rot_z */
#include "term_api.h"		/* for lp_use_properties() */
#include "command.h"		/* for c_token */
#include "voxelgrid.h"		/* for isosurface_options */

#include <stdlib.h> /* qsort() */

/* Used to initialize `set pm3d border` */
struct lp_style_type default_pm3d_border = DEFAULT_LP_STYLE_TYPE;

/* Set by plot styles that use pm3d quadrangles even in non-pm3d mode */
TBOOLEAN track_pm3d_quadrangles;

/*
  Global options for pm3d algorithm (to be accessed by set / show).
*/
pm3d_struct pm3d;	/* Initialized via init_session->reset_command->pm3d_reset */

lighting_model pm3d_shade;

typedef struct {
    double gray;
    double depth;		/* line-of-sight z after rotation to graph coordinates */
    union {
	gpdPoint corners[4];	/* The normal case. vertices stored right here */
	int array_index;	/* Stored elsewhere if there are more than 4 */
    } vertex;
    struct surface_points *from_plot;
    unsigned int qcolor; 	/* Only used by depthorder processing */
    short index;		/* Index into from_plot->zclip, if it exists */
				/* this is really the fillstyle if this is an object */
    short type;			/* QUAD_TYPE_NORMAL or QUAD_TYPE_LARGEPOLYGON etc */
#ifdef WITH_2ND_SORTKEY
    int sequence;		/* The original order of added quadrangles */
#endif
} quadrangle;

#define QUAD_TYPE_NORMAL   0
#define QUAD_TYPE_SKIP    -1
#define QUAD_TYPE_TRIANGLE 3
#define QUAD_TYPE_4SIDES   4
#define QUAD_TYPE_LARGEPOLYGON 5

#define PM3D_USE_COLORSPEC_INSTEAD_OF_GRAY  -12345
#define PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY  -12346
#define PM3D_USE_BACKGROUND_INSTEAD_OF_GRAY -12347

static int allocated_quadrangles = 0;
static int current_quadrangle = 0;
static quadrangle* quadrangles = (quadrangle*)0;

static gpdPoint *polygonlist = NULL;	/* holds polygons with >4 vertices */
static int next_polygon = 0;		/* index of next slot in the list */
static int current_polygon = 0;		/* index of the current polygon */
static int polygonlistsize = 0;

static int pm3d_plot_at = 0;	/* flag so that top/base polygons are not clipped against z */

/* Internal prototypes for this module */
static TBOOLEAN plot_has_palette;
static double geomean4(double, double, double, double);
static double harmean4(double, double, double, double);
static double median4(double, double, double, double);
static double rms4(double, double, double, double);
static void pm3d_plot(struct surface_points *, int);
static void pm3d_option_at_error(void);
static void pm3d_rearrange_part(struct iso_curve *, const int, struct iso_curve ***, int *);
static int apply_lighting_model(struct coordinate *, struct coordinate *,
				struct coordinate *, struct coordinate *,
				double gray, TBOOLEAN gray_is_rgb );
static void illuminate_one_quadrangle( quadrangle *q );

static void filled_polygon(struct surface_points *from_plot, int index,
			    gpdPoint *corners, int nv);
static int clip_filled_polygon(struct surface_points *from_plot, int index,
			    gpdPoint *inpts, gpdPoint *outpts, int nv );

static TBOOLEAN color_from_rgbvar = FALSE;
static double light[3];

static TBOOLEAN reserve_quadrangles(int needed, int chunk);

static gpdPoint *get_polygon(int size);
static void free_polygonlist(void);

static void split_intersecting_surface_tiles(void);

/*
 * Utility routines.
 */

/* Geometrical mean = pow( prod(x_i > 0) x_i, 1/N )
 * In order to facilitate plotting surfaces with all color coords negative,
 * All 4 corners positive - return positive geometric mean
 * All 4 corners negative - return negative geometric mean
 * otherwise return 0
 * EAM Oct 2012: This is a CHANGE
 */
static double
geomean4 (double x1, double x2, double x3, double x4)
{
    int neg = (x1 < 0) + (x2 < 0) + (x3 < 0) + (x4 < 0);
    double product = x1 * x2 * x3 * x4;

    if (product == 0) return 0;
    if (neg == 1 || neg == 2 || neg == 3) return 0;

    product = sqrt(sqrt(fabs(product)));
    return (neg == 0) ? product : -product;
}

static double
harmean4 (double x1, double x2, double x3, double x4)
{
    if (x1 <= 0 || x2 <= 0 || x3 <= 0 || x4 <= 0)
	return not_a_number();
    else
	return 4 / ((1/x1) + (1/x2) + (1/x3) + (1/x4));
}


/* Median: sort values, and then: for N odd, it is the middle value; for N even,
 * it is mean of the two middle values.
 */
static double
median4 (double x1, double x2, double x3, double x4)
{
    double tmp;
    /* sort them: x1 < x2 and x3 < x4 */
    if (x1 > x2) { tmp = x2; x2 = x1; x1 = tmp; }
    if (x3 > x4) { tmp = x3; x3 = x4; x4 = tmp; }
    /* sum middle numbers */
    tmp = (x1 < x3) ? x3 : x1;
    tmp += (x2 < x4) ? x2 : x4;
    return tmp * 0.5;
}


/* Minimum of 4 numbers.
 */
static double
minimum4 (double x1, double x2, double x3, double x4)
{
    x1 = GPMIN(x1, x2);
    x3 = GPMIN(x3, x4);
    return GPMIN(x1, x3);
}


/* Maximum of 4 numbers.
 */
static double
maximum4 (double x1, double x2, double x3, double x4)
{
    x1 = GPMAX(x1, x2);
    x3 = GPMAX(x3, x4);
    return GPMAX(x1, x3);
}

/* The root mean square of the 4 numbers */
static double
rms4 (double x1, double x2, double x3, double x4)
{
	return 0.5*sqrt(x1*x1 + x2*x2 + x3*x3 + x4*x4);
}

/*
* Now the routines which are really just those for pm3d.c
*/

/*
 * Rescale cb (color) value into the interval of grays [0,1], taking care
 * of palette being positive or negative.
 * Note that it is OK for logarithmic cb-axis too.
 */
double
cb2gray(double cb)
{
    AXIS *cbaxis = &CB_AXIS;

    if (cb <= cbaxis->min)
	return (sm_palette.positive == SMPAL_POSITIVE) ? 0 : 1;
    if (cb >= cbaxis->max)
	return (sm_palette.positive == SMPAL_POSITIVE) ? 1 : 0;

    if (nonlinear(cbaxis)) {
	cbaxis = cbaxis->linked_to_primary;
	cb = eval_link_function(cbaxis, cb);
    }

    cb = (cb - cbaxis->min) / (cbaxis->max - cbaxis->min);
    return (sm_palette.positive == SMPAL_POSITIVE) ? cb : 1-cb;
}


/*
 * Rearrange...
 */
static void
pm3d_rearrange_part(
    struct iso_curve *src,
    const int len,
    struct iso_curve ***dest,
    int *invert)
{
    struct iso_curve *scanA;
    struct iso_curve *scanB;
    struct iso_curve **scan_array;
    int i, scan;
    int invert_order = 0;

    /* loop over scans in one surface
       Scans are linked from this_plot->iso_crvs in the opposite order than
       they are in the datafile.
       Therefore it is necessary to make vector scan_array of iso_curves.
       Scans are sorted in scan_array according to pm3d.direction (this can
       be PM3D_SCANS_FORWARD or PM3D_SCANS_BACKWARD).
     */
    scan_array = *dest = gp_alloc(len * sizeof(scanA), "pm3d scan array");

    if (pm3d.direction == PM3D_SCANS_AUTOMATIC) {
	int cnt;
	int len2 = len;
	TBOOLEAN exit_outer_loop = 0;

	for (scanA = src; scanA && 0 == exit_outer_loop; scanA = scanA->next, len2--) {

	    int from, i;
	    vertex vA, vA2;

	    if ((cnt = scanA->p_count - 1) <= 0)
		continue;

	    /* ordering within one scan */
	    for (from=0; from<=cnt; from++) /* find 1st non-undefined point */
		if (scanA->points[from].type != UNDEFINED) {
		    map3d_xyz(scanA->points[from].x, scanA->points[from].y, 0, &vA);
		    break;
		}
	    for (i=cnt; i>from; i--) /* find the last non-undefined point */
		if (scanA->points[i].type != UNDEFINED) {
		    map3d_xyz(scanA->points[i].x, scanA->points[i].y, 0, &vA2);
		    break;
		}

	    if (i - from > cnt * 0.1)
		/* it is completely arbitrary to request at least
		 * 10% valid samples in this scan. (joze Jun-05-2002) */
		*invert = (vA2.z > vA.z) ? 0 : 1;
	    else
		continue; /* all points were undefined, so check next scan */


	    /* check the z ordering between scans
	     * Find last scan. If this scan has all points undefined,
	     * find last but one scan, an so on. */

	    for (; len2 >= 3 && !exit_outer_loop; len2--) {
		for (scanB = scanA-> next, i = len2 - 2; i && scanB; i--)
		    scanB = scanB->next; /* skip over to last scan */
		if (scanB && scanB->p_count) {
		    vertex vB;
		    for (i = from /* we compare vA.z with vB.z */; i<scanB->p_count; i++) {
		       	/* find 1st non-undefined point */
			if (scanB->points[i].type != UNDEFINED) {
			    map3d_xyz(scanB->points[i].x, scanB->points[i].y, 0, &vB);
			    invert_order = (vB.z > vA.z) ? 0 : 1;
			    exit_outer_loop = 1;
			    break;
			}
		    }
		}
	    }
	}
    }

    FPRINTF((stderr, "(pm3d_rearrange_part) invert       = %d\n", *invert));
    FPRINTF((stderr, "(pm3d_rearrange_part) invert_order = %d\n", invert_order));

    for (scanA = src, scan = len - 1, i = 0; scan >= 0; --scan, i++) {
	if (pm3d.direction == PM3D_SCANS_AUTOMATIC) {
	    switch (invert_order) {
	    case 1:
		scan_array[scan] = scanA;
		break;
	    case 0:
	    default:
		scan_array[i] = scanA;
		break;
	    }
	} else if (pm3d.direction == PM3D_SCANS_FORWARD)
	    scan_array[scan] = scanA;
	else			/* PM3D_SCANS_BACKWARD: i counts scans */
	    scan_array[i] = scanA;
	scanA = scanA->next;
    }
}


/*
 * Rearrange scan array
 *
 * Allocates *first_ptr (and eventually *second_ptr)
 * which must be freed by the caller
 */
void
pm3d_rearrange_scan_array(
    struct surface_points *this_plot,
    struct iso_curve ***first_ptr,
    int *first_n, int *first_invert,
    struct iso_curve ***second_ptr,
    int *second_n, int *second_invert)
{
    if (first_ptr) {
	pm3d_rearrange_part(this_plot->iso_crvs, this_plot->num_iso_read, first_ptr, first_invert);
	*first_n = this_plot->num_iso_read;
    }
    if (second_ptr) {
	struct iso_curve *icrvs = this_plot->iso_crvs;
	struct iso_curve *icrvs2;
	int i;
	/* advance until second part */
	for (i = 0; i < this_plot->num_iso_read; i++)
	    icrvs = icrvs->next;
	/* count the number of scans of second part */
	for (i = 0, icrvs2 = icrvs; icrvs2; icrvs2 = icrvs2->next)
	    i++;
	if (i > 0) {
	    *second_n = i;
	    pm3d_rearrange_part(icrvs, i, second_ptr, second_invert);
	} else {
	    *second_ptr = (struct iso_curve **) 0;
	}
    }
}

static int
compare_quadrangles(const void* v1, const void* v2)
{
    const quadrangle* q1 = (const quadrangle*)v1;
    const quadrangle* q2 = (const quadrangle*)v2;

    if (q1->depth > q2->depth)
	return 1;
    else if (q1->depth < q2->depth)
	return -1;
#ifdef WITH_2ND_SORTKEY
    else if (q1->sequence > q2->sequence)
	return 1;
    else if (q1->sequence < q2->sequence)
	return -1;
#endif
    else
	return 0;
}

void
pm3d_depth_queue_clear(void)
{
    free(quadrangles);
    quadrangles = NULL;
    allocated_quadrangles = 0;
    current_quadrangle = 0;
}

void
pm3d_depth_queue_flush(void)
{
    if (pm3d.direction != PM3D_DEPTH && !track_pm3d_quadrangles)
	return;

    split_intersecting_surface_tiles();

    term->layer(TERM_LAYER_BEGIN_PM3D_FLUSH);

    if (current_quadrangle > 0 && quadrangles) {

	quadrangle* qp;
	quadrangle* qe;
	gpdPoint* gpdPtr;
	vertex out;
	double zbase = 0;
	int nv, i;

	if (pm3d.base_sort)
	    cliptorange(zbase, Z_AXIS.min, Z_AXIS.max);

	for (qp = quadrangles, qe = &quadrangles[current_quadrangle]; qp != qe; qp++) {
	    double zmean = 0;

	    if (qp->type == QUAD_TYPE_LARGEPOLYGON) {
		gpdPtr = &polygonlist[qp->vertex.array_index];
		nv = gpdPtr[2].c;
	    } else {
		gpdPtr = qp->vertex.corners;
		nv = (qp->type == QUAD_TYPE_TRIANGLE) ? 3 : 4;
	    }

	    for (i = 0; i < nv; i++, gpdPtr++) {
		/* 3D boxes want to be sorted on z of the base, not the mean z */
		if (pm3d.base_sort)
		    map3d_xyz(gpdPtr->x, gpdPtr->y, zbase, &out);
		else
		    map3d_xyz(gpdPtr->x, gpdPtr->y, gpdPtr->z, &out);
		zmean += out.z;
	    }

	    qp->depth = zmean / nv;
	}

	gp_qsort(quadrangles, current_quadrangle, sizeof (quadrangle), compare_quadrangles);

	for (qp = quadrangles, qe = &quadrangles[current_quadrangle]; qp != qe; qp++) {

	    /* skip this one (e.g. if it was removed by split_intersecting_surface_tiles) */
	    if (qp->type == QUAD_TYPE_SKIP)
		continue;

	    /* set the color */
	    if (qp->from_plot && qp->from_plot->plot_style == CONTOURFILL) {
		if (qp->from_plot->zclip) {
		    t_colorspec *color = &(qp->from_plot->zclip[qp->index].color);
		    apply_pm3dcolor(color);
		}	/* else { something went badly wrong, give an error? } */
	    } else if (qp->gray == PM3D_USE_COLORSPEC_INSTEAD_OF_GRAY)
		apply_pm3dcolor(&qp->from_plot->lp_properties.pm3d_color);
	    else if (qp->gray == PM3D_USE_BACKGROUND_INSTEAD_OF_GRAY)
		term->linetype(LT_BACKGROUND);
	    else if (qp->gray == PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY)
		set_rgbcolor_var(qp->qcolor);
	    else if (pm3d_shade.strength > 0)
		set_rgbcolor_var((unsigned int)qp->gray);
	    else
		set_color(qp->gray);

	    if (qp->type == QUAD_TYPE_LARGEPOLYGON) {
		gpdPoint *vertices = &polygonlist[qp->vertex.array_index];
		int nv = vertices[2].c;
		filled_polygon(qp->from_plot, qp->index, vertices, nv);
	    } else {
		filled_polygon(qp->from_plot, qp->index, qp->vertex.corners, 4);
	    }
	}
    }

    pm3d_depth_queue_clear();
    free_polygonlist();

    term->layer(TERM_LAYER_END_PM3D_FLUSH);
}

/*
 * Now the implementation of the pm3d (s)plotting mode
 */
static void
pm3d_plot(struct surface_points *this_plot, int at_which_z)
{
    int j, i, i1, ii, ii1, from, scan, up_to, up_to_minus, invert = 0;
    int go_over_pts, max_pts;
    int are_ftriangles, ftriangles_low_pt = -999, ftriangles_high_pt = -999;
    struct iso_curve *scanA, *scanB;
    struct coordinate *pointsA, *pointsB;
    struct iso_curve **scan_array;
    int scan_array_n;
    double avgC, gray = 0;
    double cb1, cb2, cb3, cb4;
    gpdPoint corners[4];
    int interp_i, interp_j;
    gpdPoint **bl_point = NULL; /* used for bilinear interpolation */
    TBOOLEAN color_from_column = FALSE;
    TBOOLEAN color_from_fillcolor = FALSE;
    udvt_entry *private_colormap = NULL;

    /* should never happen */
    if (this_plot == NULL)
	return;

    /* just a shortcut */
    color_from_column = this_plot->pm3d_color_from_column;
    color_from_rgbvar = FALSE;

    if (this_plot->lp_properties.pm3d_color.type == TC_RGB
    &&  this_plot->lp_properties.pm3d_color.value == -1)
	color_from_rgbvar = TRUE;

    if (this_plot->fill_properties.border_color.type == TC_RGB
    ||  this_plot->fill_properties.border_color.type == TC_LINESTYLE) {
	color_from_rgbvar = TRUE;
	color_from_fillcolor = TRUE;
    }

    if (this_plot->lp_properties.colormap)
	private_colormap = this_plot->lp_properties.colormap;

    /* Feb 2023:  not necessary and may be counter-productive
	term_apply_lp_properties(&this_plot->lp_properties);
     */

    if (at_which_z != PM3D_AT_BASE && at_which_z != PM3D_AT_TOP && at_which_z != PM3D_AT_SURFACE)
	return;
    else
	pm3d_plot_at = at_which_z;	/* clip_filled_polygon() will check this */

    /* return if the terminal does not support filled polygons */
    if (!term->filled_polygon)
	return;

    /* for pm3dCompress.awk and pm3dConvertToImage.awk */
    if (pm3d.direction != PM3D_DEPTH)
	term->layer(TERM_LAYER_BEGIN_PM3D_MAP);

    switch (at_which_z) {
    case PM3D_AT_BASE:
	corners[0].z = corners[1].z = corners[2].z = corners[3].z = base_z;
	break;
    case PM3D_AT_TOP:
	corners[0].z = corners[1].z = corners[2].z = corners[3].z = ceiling_z;
	break;
	/* the 3rd possibility is surface, PM3D_AT_SURFACE, coded below */
    }

    scanA = this_plot->iso_crvs;

    pm3d_rearrange_scan_array(this_plot, &scan_array, &scan_array_n, &invert, NULL, NULL, NULL);

    interp_i = pm3d.interp_i;
    interp_j = pm3d.interp_j;

    if (interp_i <= 0 || interp_j <= 0) {
	/* Number of interpolations will be determined from desired number of points.
	   Search for number of scans and maximal number of points in a scan for points
	   which will be plotted (INRANGE). Then set interp_i,j so that number of points
	   will be a bit larger than |interp_i,j|.
	   If (interp_i,j==0) => set this number of points according to DEFAULT_OPTIMAL_NB_POINTS.
	   Ideally this should be comparable to the resolution of the output device, which
	   can hardly by done at this high level instead of the driver level.
   	*/
	#define DEFAULT_OPTIMAL_NB_POINTS 200
	int max_scan_pts = 0;
	int max_scans = 0;
	int pts;
	for (scan = 0; scan < this_plot->num_iso_read - 1; scan++) {
	    scanA = scan_array[scan];
	    pointsA = scanA->points;
	    pts = 0;
	    for (j=0; j<scanA->p_count; j++)
		if (pointsA[j].type == INRANGE) pts++;
	    if (pts > 0) {
		max_scan_pts = GPMAX(max_scan_pts, pts);
		max_scans++;
	    }
	}

	if (max_scan_pts == 0 || max_scans == 0)
	    int_error(NO_CARET, "all scans empty");

	if (interp_i <= 0) {
	    ii = (interp_i == 0) ? DEFAULT_OPTIMAL_NB_POINTS : -interp_i;
	    interp_i = floor(ii / max_scan_pts) + 1;
	}
	if (interp_j <= 0) {
	    ii = (interp_j == 0) ? DEFAULT_OPTIMAL_NB_POINTS : -interp_j;
	    interp_j = floor(ii / max_scans) + 1;
	}
#if 0
	fprintf(stderr, "pm3d.interp_i=%i\t pm3d.interp_j=%i\n", pm3d.interp_i, pm3d.interp_j);
	fprintf(stderr, "INRANGE: max_scans=%i  max_scan_pts=%i\n", max_scans, max_scan_pts);
	fprintf(stderr, "setting interp_i=%i\t interp_j=%i => there will be %i and %i points\n",
		interp_i, interp_j, interp_i*max_scan_pts, interp_j*max_scans);
#endif
    }

    if (pm3d.direction == PM3D_DEPTH) {
	int needed_quadrangles = 0;

	for (scan = 0; scan < this_plot->num_iso_read - 1; scan++) {

	    scanA = scan_array[scan];
	    scanB = scan_array[scan + 1];

	    are_ftriangles = pm3d.ftriangles && (scanA->p_count != scanB->p_count);
	    if (!are_ftriangles)
		needed_quadrangles += GPMIN(scanA->p_count, scanB->p_count) - 1;
	    else {
		needed_quadrangles += GPMAX(scanA->p_count, scanB->p_count) - 1;
	    }
	}
	needed_quadrangles *= (interp_i > 1) ? interp_i : 1;
	needed_quadrangles *= (interp_j > 1) ? interp_j : 1;
	if (needed_quadrangles > 0)
	    reserve_quadrangles(needed_quadrangles, 0);
    }

#if 0
    /* debugging: print scan_array */
    for (scan = 0; scan < this_plot->num_iso_read; scan++) {
	printf("**** SCAN=%d  points=%d\n", scan, scan_array[scan]->p_count);
    }
#endif

#if 0
    /* debugging: this loop prints properties of all scans */
    for (scan = 0; scan < this_plot->num_iso_read; scan++) {
	struct coordinate *points;
	scanA = scan_array[scan];
	printf("\n#IsoCurve = scan nb %d, %d points\n#x y z type(in,out,undef)\n", scan, scanA->p_count);
	for (i = 0, points = scanA->points; i < scanA->p_count; i++) {
	    printf("%g %g %g %c\n",
		   points[i].x, points[i].y, points[i].z, points[i].type == INRANGE ? 'i' : points[i].type == OUTRANGE ? 'o' : 'u');
	    /* Note: INRANGE, OUTRANGE, UNDEFINED */
	}
    }
    printf("\n");
#endif

    /*
     * if bilinear interpolation is enabled, allocate memory for the
     * interpolated points here
     */
    if (interp_i > 1 || interp_j > 1) {
	bl_point = (gpdPoint **)gp_alloc(sizeof(gpdPoint*) * (interp_i+1), "bl-interp along scan");
	for (i1 = 0; i1 <= interp_i; i1++)
	    bl_point[i1] = (gpdPoint *)gp_alloc(sizeof(gpdPoint) * (interp_j+1), "bl-interp between scan");
    }

    /*
     * this loop does the pm3d draw of joining two curves
     *
     * How the loop below works:
     * - scanB = scan last read; scanA = the previous one
     * - link the scan from A to B, then move B to A, then read B, then draw
     */
    for (scan = 0; scan < this_plot->num_iso_read - 1; scan++) {
	scanA = scan_array[scan];
	scanB = scan_array[scan + 1];

	FPRINTF((stderr,"\n#IsoCurveA = scan nb %d has %d points   ScanB has %d points\n",
		scan, scanA->p_count, scanB->p_count));

	pointsA = scanA->points;
	pointsB = scanB->points;
	/* if the number of points in both scans is not the same, then the
	 * starting index (offset) of scan B according to the flushing setting
	 * has to be determined
	 */
	from = 0;		/* default is pm3d.flush==PM3D_FLUSH_BEGIN */
	if (pm3d.flush == PM3D_FLUSH_END)
	    from = abs(scanA->p_count - scanB->p_count);
	else if (pm3d.flush == PM3D_FLUSH_CENTER)
	    from = abs(scanA->p_count - scanB->p_count) / 2;
	/* find the minimal number of points in both scans */
	up_to = GPMIN(scanA->p_count, scanB->p_count) - 1;
	up_to_minus = up_to - 1; /* calculate only once */
	are_ftriangles = pm3d.ftriangles && (scanA->p_count != scanB->p_count);
	if (!are_ftriangles)
	    go_over_pts = up_to;
	else {
	    max_pts = GPMAX(scanA->p_count, scanB->p_count);
	    go_over_pts = max_pts - 1;
	    /* the j-subrange of quadrangles; in the remainder of the interval
	     * [0..up_to] the flushing triangles are to be drawn */
	    ftriangles_low_pt = from;
	    ftriangles_high_pt = from + up_to_minus;
	}
	/* Go over
	 *   - the minimal number of points from both scans, if only quadrangles.
	 *   - the maximal number of points from both scans if flush triangles
	 *     (the missing points in the scan of lower nb of points will be
	 *     duplicated from the begin/end points).
	 *
	 * Notice: if it would be once necessary to go over points in `backward'
	 * direction, then the loop body below would require to replace the data
	 * point indices `i' by `up_to-i' and `i+1' by `up_to-i-1'.
	 */
	for (j = 0; j < go_over_pts; j++) {
	    /* Now i be the index of the scan with smaller number of points,
	     * ii of the scan with larger number of points. */
	    if (are_ftriangles && (j < ftriangles_low_pt || j > ftriangles_high_pt)) {
		i = (j <= ftriangles_low_pt) ? 0 : ftriangles_high_pt-from+1;
		ii = j;
		i1 = i;
		ii1 = ii + 1;
	    } else {
		int jj = are_ftriangles ? j - from : j;
		i = jj;
		if (PM3D_SCANS_AUTOMATIC == pm3d.direction && invert)
		    i = up_to_minus - jj;
		ii = i + from;
		i1 = i + 1;
		ii1 = ii + 1;
	    }
	    /* From here, i is index to scan A, ii to scan B */
	    if (scanA->p_count > scanB->p_count) {
		int itmp = i;
		i = ii;
		ii = itmp;
		itmp = i1;
		i1 = ii1;
		ii1 = itmp;
	    }

	    FPRINTF((stderr,"j=%i:  i=%i i1=%i  [%i]   ii=%i ii1=%i  [%i]\n",
	    		j,i,i1,scanA->p_count,ii,ii1,scanB->p_count));

	    /* choose the clipping method */
	    if (pm3d.clip == PM3D_CLIP_4IN) {
		/* (1) all 4 points of the quadrangle must be in x and y range */
		if (!(pointsA[i].type == INRANGE && pointsA[i1].type == INRANGE &&
		      pointsB[ii].type == INRANGE && pointsB[ii1].type == INRANGE))
		    continue;
	    } else {
		/* (2) all 4 points of the quadrangle must be defined */
		if (pointsA[i].type == UNDEFINED || pointsA[i1].type == UNDEFINED ||
		    pointsB[ii].type == UNDEFINED || pointsB[ii1].type == UNDEFINED)
		    continue;
		/* and at least 1 point of the quadrangle must be in x and y range */
		if (pointsA[i].type == OUTRANGE && pointsA[i1].type == OUTRANGE &&
		    pointsB[ii].type == OUTRANGE && pointsB[ii1].type == OUTRANGE)
		    continue;
	    }

	    if ((interp_i <= 1 && interp_j <= 1) || pm3d.direction == PM3D_DEPTH) {
	      {
		/* Get the gray as the average of the corner z- or gray-positions
		   (note: log scale is already included). The average is calculated here
		   if there is no interpolation (including the "pm3d depthorder" option),
		   otherwise it is done for each interpolated quadrangle later.
		 */
		if (color_from_column) {
		    /* color is set in plot3d.c:get_3ddata() */
		    cb1 = pointsA[i].CRD_COLOR;
		    cb2 = pointsA[i1].CRD_COLOR;
		    cb3 = pointsB[ii].CRD_COLOR;
		    cb4 = pointsB[ii1].CRD_COLOR;
		} else if (color_from_fillcolor) {
		    /* color is set by "fc <rgbvalue>" */
		    cb1 = cb2 = cb3 = cb4 = this_plot->fill_properties.border_color.lt;
		    /* pm3d fc linestyle N generates
		     * top/bottom color difference as with hidden3d
		     */
		    if (this_plot->fill_properties.border_color.type == TC_LINESTYLE) {
			struct lp_style_type style;
			int side = pm3d_side( &pointsA[i], &pointsA[i1], &pointsB[ii]);
			lp_use_properties(&style, side < 0 ? cb1 + 1 : cb1);
			cb1 = cb2 = cb3 = cb4 = style.pm3d_color.lt;
		    }
		} else {
		    cb1 = pointsA[i].z;
		    cb2 = pointsA[i1].z;
		    cb3 = pointsB[ii].z;
		    cb4 = pointsB[ii1].z;
		}
		/* Fancy averages of RGB color make no sense */
		if (color_from_rgbvar) {
		    unsigned int r, g, b, a;
		    unsigned int u1 = cb1;
		    unsigned int u2 = cb2;
		    unsigned int u3 = cb3;
		    unsigned int u4 = cb4;
		    switch (pm3d.which_corner_color) {
			default:
			    r = (u1&0xff0000) + (u2&0xff0000) + (u3&0xff0000) + (u4&0xff0000);
			    g = (u1&0xff00) + (u2&0xff00) + (u3&0xff00) + (u4&0xff00);
			    b = (u1&0xff) + (u2&0xff) + (u3&0xff) + (u4&0xff);
			    avgC = ((r>>2)&0xff0000) + ((g>>2)&0xff00) + ((b>>2)&0xff);
			    a = ((u1>>24)&0xff) + ((u2>>24)&0xff) + ((u3>>24)&0xff) + ((u4>>24)&0xff);
			    avgC += (a<<22)&0xff000000;
			    break;
			case PM3D_WHICHCORNER_C1: avgC = cb1; break;
			case PM3D_WHICHCORNER_C2: avgC = cb2; break;
			case PM3D_WHICHCORNER_C3: avgC = cb3; break;
			case PM3D_WHICHCORNER_C4: avgC = cb4; break;
		    }

		/* But many different averages are possible for gray values */
		} else  {
		    switch (pm3d.which_corner_color) {
			default:
			case PM3D_WHICHCORNER_MEAN: avgC = (cb1 + cb2 + cb3 + cb4) * 0.25; break;
			case PM3D_WHICHCORNER_GEOMEAN: avgC = geomean4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_HARMEAN: avgC = harmean4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_MEDIAN: avgC = median4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_MIN: avgC = minimum4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_MAX: avgC = maximum4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_RMS: avgC = rms4(cb1, cb2, cb3, cb4); break;
			case PM3D_WHICHCORNER_C1: avgC = cb1; break;
			case PM3D_WHICHCORNER_C2: avgC = cb2; break;
			case PM3D_WHICHCORNER_C3: avgC = cb3; break;
			case PM3D_WHICHCORNER_C4: avgC = cb4; break;
		    }
		}

		/* The value is out of range, but we didn't figure it out until now */
		if (isnan(avgC))
		    continue;

		/* Option to not drawn quadrangles with cb out of range */
		if (pm3d.no_clipcb && (avgC > CB_AXIS.max || avgC < CB_AXIS.min))
		    continue;

		if (color_from_rgbvar) /* we were given an RGB color */
			gray = avgC;
		else if (private_colormap)
			gray = map2gray(avgC, private_colormap);
		else /* transform z value to gray, i.e. to interval [0,1] */
			gray = cb2gray(avgC);

		/* apply lighting model */
		if (pm3d_shade.strength > 0) {
		    if (at_which_z == PM3D_AT_SURFACE) {
			TBOOLEAN gray_is_rgb = color_from_rgbvar;
			if (private_colormap) {
			    gray = rgb_from_colormap(gray, private_colormap);
			    gray_is_rgb = TRUE;
			}
			gray = apply_lighting_model( &pointsA[i], &pointsA[i1],
					&pointsB[ii], &pointsB[ii1], gray, gray_is_rgb);
		    }
		    /* Don't apply lighting model to TOP/BOTTOM projections  */
		    /* but convert from floating point 0<gray<1 to RGB color */
		    /* since that is what would have been returned from the  */
		    /* lighting code.					     */
		    else if (private_colormap) {
			gray = rgb_from_colormap(gray, private_colormap);
		    } else if (!color_from_rgbvar) {
			unsigned int temp = rgb_from_gray(gray);
			gray = (long)temp;
		    }
		}

		/* set the color */
		if (pm3d.direction != PM3D_DEPTH) {
		    if (color_from_rgbvar || pm3d_shade.strength > 0)
			set_rgbcolor_var((unsigned int)gray);
		    else if (private_colormap)
			set_rgbcolor_var(rgb_from_colormap(gray, private_colormap));
		    else
			set_color(gray);
		}
	      }
	    }

	    corners[0].x = pointsA[i].x;
	    corners[0].y = pointsA[i].y;
	    corners[1].x = pointsB[ii].x;
	    corners[1].y = pointsB[ii].y;
	    corners[2].x = pointsB[ii1].x;
	    corners[2].y = pointsB[ii1].y;
	    corners[3].x = pointsA[i1].x;
	    corners[3].y = pointsA[i1].y;

	    if (interp_i > 1 || interp_j > 1 || at_which_z == PM3D_AT_SURFACE) {
		corners[0].z = pointsA[i].z;
		corners[1].z = pointsB[ii].z;
		corners[2].z = pointsB[ii1].z;
		corners[3].z = pointsA[i1].z;
		if (color_from_column) {
		    corners[0].c = pointsA[i].CRD_COLOR;
		    corners[1].c = pointsB[ii].CRD_COLOR;
		    corners[2].c = pointsB[ii1].CRD_COLOR;
		    corners[3].c = pointsA[i1].CRD_COLOR;
		}
	    }
	    if (interp_i > 1 || interp_j > 1) {
		/* Interpolation is enabled.
		 * interp_i is the # of points along scan lines
		 * interp_j is the # of points between scan lines
		 * Algorithm is to first sample i points along the scan lines
		 * defined by corners[3],corners[0] and corners[2],corners[1]. */
		int j1;
		for (i1 = 0; i1 <= interp_i; i1++) {
		    bl_point[i1][0].x =
			((corners[3].x - corners[0].x) / interp_i) * i1 + corners[0].x;
		    bl_point[i1][interp_j].x =
			((corners[2].x - corners[1].x) / interp_i) * i1 + corners[1].x;
		    bl_point[i1][0].y =
			((corners[3].y - corners[0].y) / interp_i) * i1 + corners[0].y;
		    bl_point[i1][interp_j].y =
			((corners[2].y - corners[1].y) / interp_i) * i1 + corners[1].y;
		    bl_point[i1][0].z =
			((corners[3].z - corners[0].z) / interp_i) * i1 + corners[0].z;
		    bl_point[i1][interp_j].z =
			((corners[2].z - corners[1].z) / interp_i) * i1 + corners[1].z;
		    if (color_from_column) {
			bl_point[i1][0].c =
			    ((corners[3].c - corners[0].c) / interp_i) * i1 + corners[0].c;
			bl_point[i1][interp_j].c =
			    ((corners[2].c - corners[1].c) / interp_i) * i1 + corners[1].c;
		    }
		    /* Next we sample j points between each of the new points
		     * created in the previous step (this samples between
		     * scan lines) in the same manner. */
		    for (j1 = 1; j1 < interp_j; j1++) {
			bl_point[i1][j1].x =
			    ((bl_point[i1][interp_j].x - bl_point[i1][0].x) / interp_j) *
				j1 + bl_point[i1][0].x;
			bl_point[i1][j1].y =
			    ((bl_point[i1][interp_j].y - bl_point[i1][0].y) / interp_j) *
				j1 + bl_point[i1][0].y;
			bl_point[i1][j1].z =
			    ((bl_point[i1][interp_j].z - bl_point[i1][0].z) / interp_j) *
				j1 + bl_point[i1][0].z;
			if (color_from_column)
			    bl_point[i1][j1].c =
				((bl_point[i1][interp_j].c - bl_point[i1][0].c) / interp_j) *
				    j1 + bl_point[i1][0].c;
		    }
		}
		/* Once all points are created, move them into an appropriate
		 * structure and call set_color on each to retrieve the
		 * correct color mapping for this new sub-sampled quadrangle. */
		for (i1 = 0; i1 < interp_i; i1++) {
		    for (j1 = 0; j1 < interp_j; j1++) {
			corners[0].x = bl_point[i1][j1].x;
			corners[0].y = bl_point[i1][j1].y;
			corners[0].z = bl_point[i1][j1].z;
			corners[1].x = bl_point[i1+1][j1].x;
			corners[1].y = bl_point[i1+1][j1].y;
			corners[1].z = bl_point[i1+1][j1].z;
			corners[2].x = bl_point[i1+1][j1+1].x;
			corners[2].y = bl_point[i1+1][j1+1].y;
			corners[2].z = bl_point[i1+1][j1+1].z;
			corners[3].x = bl_point[i1][j1+1].x;
			corners[3].y = bl_point[i1][j1+1].y;
			corners[3].z = bl_point[i1][j1+1].z;
			if (color_from_column) {
			    corners[0].c = bl_point[i1][j1].c;
			    corners[1].c = bl_point[i1+1][j1].c;
			    corners[2].c = bl_point[i1+1][j1+1].c;
			    corners[3].c = bl_point[i1][j1+1].c;
			}

			FPRINTF((stderr,"(%g,%g),(%g,%g),(%g,%g),(%g,%g)\n",
			    corners[0].x, corners[0].y, corners[1].x, corners[1].y,
			    corners[2].x, corners[2].y, corners[3].x, corners[3].y));

			/* Is this quadrangle masked? */
			if (this_plot->plot_filter == FILTER_MASK)
			    if (masked(corners[0].x, corners[0].y, mask_3Dpolygon_set))
				continue;

			/* If the colors are given separately, we already loaded them above */
			if (color_from_column) {
			    cb1 = corners[0].c;
			    cb2 = corners[1].c;
			    cb3 = corners[2].c;
			    cb4 = corners[3].c;
			} else {
			    cb1 = corners[0].z;
			    cb2 = corners[1].z;
			    cb3 = corners[2].z;
			    cb4 = corners[3].z;
			}
			switch (pm3d.which_corner_color) {
			    default:
			    case PM3D_WHICHCORNER_MEAN: avgC = (cb1 + cb2 + cb3 + cb4) * 0.25; break;
			    case PM3D_WHICHCORNER_GEOMEAN: avgC = geomean4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_HARMEAN: avgC = harmean4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MEDIAN: avgC = median4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MIN: avgC = minimum4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MAX: avgC = maximum4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_RMS: avgC = rms4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_C1: avgC = cb1; break;
			    case PM3D_WHICHCORNER_C2: avgC = cb2; break;
			    case PM3D_WHICHCORNER_C3: avgC = cb3; break;
			    case PM3D_WHICHCORNER_C4: avgC = cb4; break;
			}

			if (color_from_fillcolor) {
			    /* color is set by "fc <rgbval>" */
			    gray = this_plot->fill_properties.border_color.lt;
			    /* pm3d fc linestyle N generates
			     * top/bottom color difference as with hidden3d
			     */
			    if (this_plot->fill_properties.border_color.type == TC_LINESTYLE) {
				struct lp_style_type style;
				int side = pm3d_side( &pointsA[i], &pointsB[ii], &pointsB[ii1]);
				lp_use_properties(&style, side < 0 ? gray + 1 : gray);
				gray = style.pm3d_color.lt;
			    }
			} else if (color_from_rgbvar) {
			    /* we were given an explicit color */
			    gray = avgC;
			} else {
			    /* clip if out of range */
			    if (pm3d.no_clipcb && (avgC < CB_AXIS.min || avgC > CB_AXIS.max))
				continue;
			    /* transform z value to gray, i.e. to interval [0,1] */
			    gray = cb2gray(avgC);
			}

			/* apply lighting model */
			if (pm3d_shade.strength > 0) {
			    /* FIXME: coordinate->quadrangle->coordinate seems crazy */
			    coordinate corcorners[4];
			    int i;
			    for (i=0; i<4; i++) {
				corcorners[i].x = corners[i].x;
				corcorners[i].y = corners[i].y;
				corcorners[i].z = corners[i].z;
			    }

			    if (private_colormap) {
				gray = rgb_from_colormap(gray, private_colormap);
				if (at_which_z == PM3D_AT_SURFACE)
				    gray = apply_lighting_model( &corcorners[0], &corcorners[1],
					    &corcorners[2], &corcorners[3], gray, TRUE);

			    } else if (at_which_z == PM3D_AT_SURFACE) {
				gray = apply_lighting_model( &corcorners[0], &corcorners[1],
					&corcorners[2], &corcorners[3], gray, color_from_rgbvar);

			    /* Don't apply lighting model to TOP/BOTTOM projections  */
			    /* but convert from floating point 0<gray<1 to RGB color */
			    /* since that is what would have been returned from the  */
			    /* lighting code.					     */
			    } else if (!color_from_rgbvar) {
				unsigned int temp = rgb_from_gray(gray);
				gray = (long)temp;
			    }
			}

			/* FIXME: overwriting the z coordinates loses the option to
			 * do smooth clipping on z in the top/bottom planes
			 */
			if (at_which_z == PM3D_AT_BASE)
			    corners[0].z = corners[1].z = corners[2].z = corners[3].z
				= base_z;
			else if (at_which_z == PM3D_AT_TOP)
			    corners[0].z = corners[1].z = corners[2].z = corners[3].z
				= ceiling_z;

			if (pm3d.direction == PM3D_DEPTH) {
			    /* copy quadrangle */
			    quadrangle* qp = &quadrangles[current_quadrangle];
			    memcpy(qp->vertex.corners, corners, 4 * sizeof (gpdPoint));
			    qp->from_plot = this_plot;
			    if (color_from_rgbvar || pm3d_shade.strength > 0) {
				qp->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
				qp->qcolor = (unsigned int)gray;
			    } else if (private_colormap) {
				qp->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
				qp->qcolor = rgb_from_colormap(gray, private_colormap);
			    } else {
				qp->gray = gray;
			    }
			    qp->index = this_plot->zclip_index;
			    qp->type = QUAD_TYPE_NORMAL;
#ifdef WITH_2ND_SORTKEY
			    qp->sequence = current_quadrangle;
#endif
			    current_quadrangle++;
			} else {
			    if (pm3d_shade.strength > 0 || color_from_rgbvar)
				set_rgbcolor_var((unsigned int)gray);
			    else if (private_colormap)
				set_rgbcolor_var(rgb_from_colormap(gray, private_colormap));
			    else
				set_color(gray);
			    filled_polygon(this_plot, this_plot->zclip_index, corners, 4);
			}
		    }
		}
	    } else { /* thus (interp_i == 1 && interp_j == 1) */

		/* Is this quadrangle masked? */
		if (this_plot->plot_filter == FILTER_MASK)
		    if (masked(corners[0].x, corners[0].y, mask_3Dpolygon_set))
			continue;

		if (pm3d.direction != PM3D_DEPTH) {
		    filled_polygon(this_plot, this_plot->zclip_index, corners, 4);
		} else {
		    /* copy quadrangle */
		    quadrangle* qp = &quadrangles[current_quadrangle];
		    memcpy(qp->vertex.corners, corners, 4 * sizeof (gpdPoint));
		    qp->from_plot = this_plot;
		    if (color_from_rgbvar || pm3d_shade.strength > 0) {
			qp->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
			qp->qcolor = (unsigned int)gray;
		    } else if (private_colormap) {
			qp->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
			qp->qcolor = rgb_from_colormap(gray, private_colormap);
		    } else {
			qp->gray = gray;
		    }
		    qp->index = this_plot->zclip_index;
		    qp->type = QUAD_TYPE_NORMAL;
#ifdef WITH_2ND_SORTKEY
		    qp->sequence = current_quadrangle;
#endif
		    current_quadrangle++;
		}
	    } /* interpolate between points */
	} /* loop quadrangles over points of two subsequent scans */
    } /* loop over scans */

    if (bl_point) {
	for (i1 = 0; i1 <= interp_i; i1++)
	    free(bl_point[i1]);
	free(bl_point);
    }
    /* free memory allocated by scan_array */
    free(scan_array);

    /* reset local flags */
    pm3d_plot_at = 0;

    /* for pm3dCompress.awk and pm3dConvertToImage.awk */
    if (pm3d.direction != PM3D_DEPTH)
	term->layer(TERM_LAYER_END_PM3D_MAP);
}				/* end of pm3d splotting mode */


/*
 * unset pm3d for the reset command
 */
void
pm3d_reset()
{
    strcpy(pm3d.where, "s");
    pm3d_plot_at = 0;
    pm3d.flush = PM3D_FLUSH_BEGIN;
    pm3d.ftriangles = 0;
    pm3d.clip = PM3D_CLIP_Z;	/* prior to Dec 2019 default was PM3D_CLIP_4IN */
    pm3d.no_clipcb = FALSE;
    pm3d.direction = PM3D_SCANS_AUTOMATIC;
    pm3d.base_sort = FALSE;
    pm3d.implicit = PM3D_EXPLICIT;
    pm3d.which_corner_color = PM3D_WHICHCORNER_MEAN;
    pm3d.interp_i = 1;
    pm3d.interp_j = 1;
    pm3d.border = default_pm3d_border;
    pm3d.border.l_type = LT_NODRAW;

    pm3d_shade.strength = 0.0;
    pm3d_shade.spec = 0.0;
    pm3d_shade.fixed = TRUE;
}


/*
 * Draw (one) PM3D color surface.
 */
void
pm3d_draw_one(struct surface_points *plot)
{
    int i = 0;
    char *where = plot->pm3d_where[0] ? plot->pm3d_where : pm3d.where;
	/* Draw either at 'where' option of the given surface or at pm3d.where
	 * global option. */

    if (!where[0])
	return;

    /* Initialize lighting model */
    if (pm3d_shade.strength > 0)
	pm3d_init_lighting_model();

    for (; where[i]; i++) {
	pm3d_plot(plot, where[i]);
    }
}


/*
 * Add one pm3d quadrangle to the mix.
 * Called by zerrorfill() and by plot3d_boxes().
 * Also called by vplot_isosurface().
 */
void
pm3d_add_quadrangle(struct surface_points *plot, gpdPoint corners[])
{
    pm3d_add_polygon(plot, corners, 4);
}

/*
 * The general case.
 * (plot == NULL) if we were called from do_polygon().
 */
void
pm3d_add_polygon(struct surface_points *plot, gpdPoint corners[], int vertices)
{
    quadrangle *q;

    if (!plot || (plot->plot_style == ISOSURFACE))
	/* I have no idea how to estimate the number of facets for an isosurface */
	reserve_quadrangles(100, 1000);
    else
	reserve_quadrangles(plot->iso_crvs->p_count, 0);

    q = &quadrangles[current_quadrangle];
#ifdef WITH_2ND_SORTKEY
    q->sequence = current_quadrangle;
#endif
    current_quadrangle++;
    memcpy(q->vertex.corners, corners, 4*sizeof(gpdPoint));
    q->from_plot = plot;
    if (plot)
	q->index = plot->zclip_index;
    else /* this is a polygon object; "index" is really fillstyle */
	q->index = 0;

    /* For triangles and normal quadrangles, the vertices are stored in
     * q->vertex.corners.  For larger polygons we store them in external array
     * polygonlist and keep only an index into the array q->vertex.array.
     */
    q->type = QUAD_TYPE_NORMAL;
    if (corners[3].x == corners[2].x && corners[3].y == corners[2].y
    &&  corners[3].z == corners[2].z)
	q->type = QUAD_TYPE_TRIANGLE;
    if (vertices > 4) {
	gpdPoint *save_corners = get_polygon(vertices);
	q->vertex.array_index = current_polygon;
	q->type = QUAD_TYPE_LARGEPOLYGON;
	memcpy(save_corners, corners, vertices * sizeof(gpdPoint));
	save_corners[2].c = vertices;
    }

    if (!plot) {
	/* This quadrangle came from a polygon object rather than from splot */
	if (corners[0].c == LT_BACKGROUND) {
	    q->gray = PM3D_USE_BACKGROUND_INSTEAD_OF_GRAY;
	} else {
	    q->qcolor = corners[0].c;
	    q->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
	}
	/* "index" is really fillstyle */
	q->index = corners[1].c;

    } else if (plot->pm3d_color_from_column
		&& !(plot->plot_style == POLYGONS)) {
	/* This is the usual path for 'splot with boxes' */
	color_from_rgbvar = TRUE;
	if (pm3d_shade.strength > 0) {
	    q->gray = plot->lp_properties.pm3d_color.lt;
	    illuminate_one_quadrangle(q);
	} else {
	    q->qcolor = plot->lp_properties.pm3d_color.lt;
	    q->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;
	}

    } else if (plot->lp_properties.pm3d_color.type == TC_Z) {
	/* This is a special case for 'splot with boxes lc palette z' */
	q->gray = cb2gray(corners[1].z);
	color_from_rgbvar = FALSE;
	if (pm3d_shade.strength > 0)
	    illuminate_one_quadrangle(q);

    } else if (plot->plot_style == ISOSURFACE
           ||  plot->plot_style == POLYGONS) {

	int rgb_color = corners[0].c;
	if (corners[0].c == LT_BACKGROUND)
	    q->gray = PM3D_USE_BACKGROUND_INSTEAD_OF_GRAY;
	else
	    q->gray = PM3D_USE_RGB_COLOR_INSTEAD_OF_GRAY;

	if (plot->plot_style == ISOSURFACE && isosurface_options.inside_offset > 0) {
	    struct lp_style_type style;
	    struct coordinate v[3];
	    int i;
	    for (i=0; i<3; i++) {
		v[i].x = corners[i].x;
		v[i].y = corners[i].y;
		v[i].z = corners[i].z;
	    }
	    i = plot->hidden3d_top_linetype + 1;
	    if (pm3d_side( &v[0], &v[1], &v[2] ) < 0)
		i += isosurface_options.inside_offset;
	    lp_use_properties(&style, i);
	    rgb_color = style.pm3d_color.lt;
	}
	q->qcolor = rgb_color;
	if (pm3d_shade.strength > 0) {
	    q->gray = rgb_color;
	    color_from_rgbvar = TRUE;
	    illuminate_one_quadrangle(q);
	}

    } else {
	/* This is the usual [only?] path for 'splot with zerror'.
	 * It also handles some cases of 'splot with boxes'.
	 */
	q->gray = PM3D_USE_COLORSPEC_INSTEAD_OF_GRAY;
    }
}



/* Display an error message for the routine get_pm3d_at_option() below.
 */
static void
pm3d_option_at_error()
{
    int_error(c_token, "\
parameter to `pm3d at` requires combination of up to 6 characters b,s,t\n\
\t(drawing at bottom, surface, top)");
}


/* Read the option for 'pm3d at' command.
 * Used by 'set pm3d at ...' or by 'splot ... with pm3d at ...'.
 * If no option given, then returns empty string, otherwise copied there.
 * The string is unchanged on error, and 1 is returned.
 * On success, 0 is returned.
 */
int
get_pm3d_at_option(char *pm3d_where)
{
    char* c;

    if (END_OF_COMMAND || token[c_token].length >= sizeof(pm3d.where)) {
	pm3d_option_at_error();
	return 1;
    }
    memcpy(pm3d_where, gp_input_line + token[c_token].start_index,
	   token[c_token].length);
    pm3d_where[ token[c_token].length ] = 0;
    /* verify the parameter */
    for (c = pm3d_where; *c; c++) {
	    if (*c != PM3D_AT_BASE && *c != PM3D_AT_TOP && *c != PM3D_AT_SURFACE) {
		pm3d_option_at_error();
		return 1;
	}
    }
    c_token++;
    return 0;
}

/* Set flag plot_has_palette to TRUE if there is any element in the
 * plot that requires the palette.   The program will test this when
 * deciding whether to draw the colorbox.
 * Nov 2021
 * We used to check also for objects or labels (but not arrows, ...)
 * because otherwise they could not use palette coloring.
 * Now we allow them to use palette coloring regardless, so there is
 * no reason to set the flag just because of an object or label.
 */
void
set_plot_with_palette(int plot_num, int plot_mode)
{
    struct surface_points *this_3dplot = first_3dplot;
    struct curve_points *this_2dplot = first_plot;
    int surface = 0;

    plot_has_palette = TRUE;

    /* Is pm3d switched on globally? */
    if (pm3d.implicit == PM3D_IMPLICIT)
	return;

    /* Check 2D plots */
    if (plot_mode == MODE_PLOT) {
	while (this_2dplot) {
	    if (this_2dplot->plot_style == IMAGE)
		return;
	    if (this_2dplot->lp_properties.pm3d_color.type == TC_CB
	    ||  this_2dplot->lp_properties.pm3d_color.type == TC_FRAC
	    ||  this_2dplot->lp_properties.pm3d_color.type == TC_Z)
		return;
	    if (this_2dplot->labels
	    && (this_2dplot->labels->textcolor.type == TC_CB
	    ||  this_2dplot->labels->textcolor.type == TC_FRAC
	    ||  this_2dplot->labels->textcolor.type == TC_Z))
		return;
	    this_2dplot = this_2dplot->next;
	}
    }

    /* Check 3D plots */
    if (plot_mode == MODE_SPLOT) {
	/* Any surface 'with pm3d', 'with image' or 'with line|dot palette'? */
	while (surface++ < plot_num) {
	    int type;
	    if (this_3dplot->plot_style == CONTOURFILL)
		return;
	    if (this_3dplot->plot_style == PM3DSURFACE)
		return;
	    if (this_3dplot->plot_style == IMAGE)
		return;

	    type = this_3dplot->lp_properties.pm3d_color.type;
	    if (type == TC_LT || type == TC_LINESTYLE || type == TC_RGB)
		; /* don't return yet */
	    else
		/* TC_DEFAULT: splot x with line|lp|dot palette */
		return;

	    if (this_3dplot->labels &&
		this_3dplot->labels->textcolor.type >= TC_CB)
		return;
	    this_3dplot = this_3dplot->next_sp;
	}
    }

    /* Palette with continuous colors is not used. */
    plot_has_palette = FALSE; /* otherwise it stays TRUE */
}

TBOOLEAN
is_plot_with_palette()
{
    return plot_has_palette;
}

TBOOLEAN
is_plot_with_colorbox()
{
    return plot_has_palette && (color_box.where != SMCOLOR_BOX_NO);
}

/*
 * Must be called before trying to apply lighting model
 */
void
pm3d_init_lighting_model()
{
    light[0] = cos(-DEG2RAD*pm3d_shade.rot_x)*cos(-(DEG2RAD*pm3d_shade.rot_z));
    light[2] = cos(-DEG2RAD*pm3d_shade.rot_x)*sin(-(DEG2RAD*pm3d_shade.rot_z));
    light[1] = sin(-DEG2RAD*pm3d_shade.rot_x);
}

/*
 * Layer on layer of coordinate conventions
 */
static void
illuminate_one_quadrangle( quadrangle *q )
{
    struct coordinate c1, c2, c3, c4;
    vertex vtmp;

    map3d_xyz(q->vertex.corners[0].x, q->vertex.corners[0].y, q->vertex.corners[0].z, &vtmp);
    c1.x = vtmp.x; c1.y = vtmp.y; c1.z = vtmp.z;
    map3d_xyz(q->vertex.corners[1].x, q->vertex.corners[1].y, q->vertex.corners[1].z, &vtmp);
    c2.x = vtmp.x; c2.y = vtmp.y; c2.z = vtmp.z;
    map3d_xyz(q->vertex.corners[2].x, q->vertex.corners[2].y, q->vertex.corners[2].z, &vtmp);
    c3.x = vtmp.x; c3.y = vtmp.y; c3.z = vtmp.z;
    map3d_xyz(q->vertex.corners[3].x, q->vertex.corners[3].y, q->vertex.corners[3].z, &vtmp);
    c4.x = vtmp.x; c4.y = vtmp.y; c4.z = vtmp.z;
    q->gray = apply_lighting_model( &c1, &c2, &c3, &c4, q->gray, color_from_rgbvar);
}

/*
 * Adjust current RGB color based on pm3d lighting model.
 * Jan 2019: preserve alpha channel
 *	     This isn't quite right because specular highlights should
 *	     not be affected by transparency.
 */
int
apply_lighting_model( struct coordinate *v0, struct coordinate *v1,
		struct coordinate *v2, struct coordinate *v3,
		double gray, TBOOLEAN gray_is_rgb )
{
    double normal[3];
    double normal1[3];
    double reflect[3];
    double t;
    double phi;
    double psi;
    unsigned int rgb;
    unsigned int alpha = 0;
    rgb_color color;
    double r, g, b, tmp_r, tmp_g, tmp_b;
    double dot_prod, shade_fact, spec_fact;

    if (gray_is_rgb) {
	rgb = gray;
	r = (double)((rgb >> 16) & 0xFF) / 255.;
	g = (double)((rgb >>  8) & 0xFF) / 255.;
	b = (double)((rgb      ) & 0xFF) / 255.;
	alpha = rgb & 0xff000000;
    } else {
	rgb1_from_gray(gray, &color);
	r = color.r;
	g = color.g;
	b = color.b;
    }

    psi = -DEG2RAD*(surface_rot_z);
    phi = -DEG2RAD*(surface_rot_x);

    normal[0] = (v1->y-v0->y)*(v2->z-v0->z)*yscale3d*zscale3d
	      - (v1->z-v0->z)*(v2->y-v0->y)*yscale3d*zscale3d;
    normal[1] = (v1->z-v0->z)*(v2->x-v0->x)*xscale3d*zscale3d
	      - (v1->x-v0->x)*(v2->z-v0->z)*xscale3d*zscale3d;
    normal[2] = (v1->x-v0->x)*(v2->y-v0->y)*xscale3d*yscale3d
	      - (v1->y-v0->y)*(v2->x-v0->x)*xscale3d*yscale3d ;

    t = sqrt( normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2] );

    /* Trap and handle degenerate case of two identical vertices.
     * Aug 2020
     * FIXME: The problem case that revealed this problem always involved
     *        v2 == (v0 or v1) but some unknown example might instead yield
     *        v0 == v1, which is not addressed here.
     */
    if (t < 1.e-12) {
	if (v2 == v3) /* 2nd try; give up and return original color */
	    return (gray_is_rgb)
		? gray
		:   ((unsigned char)(r*255.) << 16)
		  + ((unsigned char)(g*255.) <<  8)
		  + ((unsigned char)(b*255.));
	else
	    return apply_lighting_model(  v0, v1, v3, v3, gray, gray_is_rgb);
    }

    normal[0] /= t;
    normal[1] /= t;
    normal[2] /= t;

    /* Correct for the view angle so that the illumination is "fixed" with */
    /* respect to the viewer rather than rotating with the surface.        */
    if (pm3d_shade.fixed) {
	normal1[0] =  cos(psi)*normal[0] -  sin(psi)*normal[1] + 0*normal[2];
	normal1[1] =  sin(psi)*normal[0] +  cos(psi)*normal[1] + 0*normal[2];
	normal1[2] =  0*normal[0] +                0*normal[1] + 1*normal[2];

	normal[0] =  1*normal1[0] +         0*normal1[1] +         0*normal1[2];
	normal[1] =  0*normal1[0] +  cos(phi)*normal1[1] -  sin(phi)*normal1[2];
	normal[2] =  0*normal1[0] +  sin(phi)*normal1[1] +  cos(phi)*normal1[2];
    }

    if (normal[2] < 0.0) {
	normal[0] *= -1.0;
	normal[1] *= -1.0;
	normal[2] *= -1.0;
    }

    dot_prod = normal[0]*light[0] + normal[1]*light[1] + normal[2]*light[2];
    shade_fact = (dot_prod < 0) ? -dot_prod : 0;

    tmp_r = r*(pm3d_shade.ambient - pm3d_shade.strength + shade_fact*pm3d_shade.strength);
    tmp_g = g*(pm3d_shade.ambient - pm3d_shade.strength + shade_fact*pm3d_shade.strength);
    tmp_b = b*(pm3d_shade.ambient - pm3d_shade.strength + shade_fact*pm3d_shade.strength);

    /* Specular highlighting */
    if (pm3d_shade.spec > 0.0) {

	reflect[0] = -light[0]+2*dot_prod*normal[0];
	reflect[1] = -light[1]+2*dot_prod*normal[1];
	reflect[2] = -light[2]+2*dot_prod*normal[2];
	t = sqrt( reflect[0]*reflect[0] + reflect[1]*reflect[1] + reflect[2]*reflect[2] );
	reflect[0] /= t;
	reflect[1] /= t;
	reflect[2] /= t;

	dot_prod = -reflect[2];

	/* old-style Phong equation; no need for bells or whistles */
	spec_fact = pow(fabs(dot_prod), pm3d_shade.Phong);

	/* White spotlight from direction phi,psi */
	if (dot_prod > 0) {
	    tmp_r += pm3d_shade.spec * spec_fact;
	    tmp_g += pm3d_shade.spec * spec_fact;
	    tmp_b += pm3d_shade.spec * spec_fact;
	}
    }

    /* Optional extra light source
     * The first spotlight was white and had fixed origin phi, psi.
     * For this one "set pm3d spotlight ..." changes the color and origin,
     * "set pm3d lighting spec2 <value>" controls the relative contribution.
     */
    if (pm3d_shade.spec2 > 0.0) {

	double light2[3];
	light2[0] = cos(-DEG2RAD*pm3d_shade.spec2_rot_x)*cos(-(DEG2RAD*pm3d_shade.spec2_rot_z));
	light2[2] = cos(-DEG2RAD*pm3d_shade.spec2_rot_x)*sin(-(DEG2RAD*pm3d_shade.spec2_rot_z));
	light2[1] = sin(-DEG2RAD*pm3d_shade.spec2_rot_x);

	dot_prod = normal[0]*light2[0] + normal[1]*light2[1] + normal[2]*light2[2];

	reflect[0] = -light2[0]+2*dot_prod*normal[0];
	reflect[1] = -light2[1]+2*dot_prod*normal[1];
	reflect[2] = -light2[2]+2*dot_prod*normal[2];
	t = sqrt( reflect[0]*reflect[0] + reflect[1]*reflect[1] + reflect[2]*reflect[2] );
	reflect[0] /= t;
	reflect[1] /= t;
	reflect[2] /= t;

	dot_prod = -reflect[2];

	spec_fact = pow(fabs(dot_prod), pm3d_shade.spec2_Phong);

	if (dot_prod < 0) {
	    tmp_r += pm3d_shade.spec2 * spec_fact
			* ((pm3d_shade.spec2_rgb >> 16) & 0xff) / 255.;
	    tmp_g += pm3d_shade.spec2 * spec_fact
			* ((pm3d_shade.spec2_rgb >> 8) & 0xff) / 255.;
	    tmp_b += pm3d_shade.spec2 * spec_fact
			* ((pm3d_shade.spec2_rgb & 0xff)) / 255.;
	}
    }

    tmp_r = clip_to_01(tmp_r);
    tmp_g = clip_to_01(tmp_g);
    tmp_b = clip_to_01(tmp_b);

    rgb = ((unsigned char)((tmp_r)*255.) << 16)
	+ ((unsigned char)((tmp_g)*255.) <<  8)
	+ ((unsigned char)((tmp_b)*255.));

    /* restore alpha value if there was one */
    rgb |= alpha;

    return rgb;
}


/* The pm3d code works with gpdPoint data structures (double: x,y,z,color)
 * term->filled_polygon wants gpiPoint data (int: x,y,style).
 * This routine converts from gpdPoint to gpiPoint
 * The "index" parameter is an index into from_plot->zclip[]
 * However if the polygon is from an object rather than a plot, "index"
 * is really the fill style of that object.
 */
static void
filled_polygon(struct surface_points *from_plot, int index, gpdPoint *corners, int nv)
{
    int i;
    double x, y;
    int fillstyle;

    /* For normal pm3d surfaces and tessellation the constituent polygons
     * have a small number of vertices, usually 4.
     * However generalized polygons like cartographic areas could have a
     * vastly larger number of vertices, so we allow for dynamic reallocation.
     */
    static int max_vertices = 0;
    static gpiPoint *icorners = NULL;
    static gpiPoint *ocorners = NULL;
    static gpdPoint *clipcorners = NULL;

    if (!(term->filled_polygon))
	return;

    if (nv > max_vertices) {
	max_vertices = nv;
	icorners = gp_realloc( icorners, (2*max_vertices) * sizeof(gpiPoint), "filled_polygon");
	ocorners = gp_realloc( ocorners, (2*max_vertices) * sizeof(gpiPoint), "filled_polygon");
	clipcorners = gp_realloc( clipcorners, (2*max_vertices) * sizeof(gpdPoint), "filled_polygon");
    }

    /* pm3d_plot_at is always 0 when we are called from pm3d depthorder
     * processing so it cannot be used to distinguish top/bottom quadrangles
     * (pm3d at [tb]) from surface quadrangles (pm3d at s).
     * The original z values have been replaced by base_z or ceiling_z,
     * so we can identify base plane quadrangles by testing for z == base_z.
     * FIXME: However that means it is too late to get smooth z clipping for
     *        the top/bottom plane contents.
     */
    if (pm3d.clip == PM3D_CLIP_Z) {
	int new = 0;
	if (corners[0].z == base_z)
	    new = 0;
	else
	    new = clip_filled_polygon( from_plot, index, corners, clipcorners, nv );
	if (new < 0) {	/* All vertices out of range */
	    return;
	}
	if (new > 0) {	/* Some got clipped */
	    nv = new;
	    corners = clipcorners;
	}
    }

    for (i = 0; i < nv; i++) {
	map3d_xy_double(corners[i].x, corners[i].y, corners[i].z, &x, &y);
	icorners[i].x = x;
	icorners[i].y = y;
    }

    /* Clip to x/y only in 2D projection.  */
    if (splot_map && (pm3d.clip == PM3D_CLIP_Z)) {
	for (i=0; i<nv; i++)
	    ocorners[i] = icorners[i];
	clip_polygon( ocorners, icorners, nv, &nv );
    }

    /* If this polygon came from an object, the "index" is really a fill style.
     * If it came from a plot, retrieve the style from the plot header.
     */
    if (from_plot)
	fillstyle = style_from_fill(&from_plot->fill_properties);
    else
	fillstyle = index;
    if (fillstyle)
	icorners[0].style = fillstyle;
    else if (default_fillstyle.fillstyle == FS_EMPTY)
	icorners[0].style = FS_OPAQUE;
    else
	icorners[0].style = style_from_fill(&default_fillstyle);

    term->filled_polygon(nv, icorners);

    /* FIXME: Should this apply to other plot styles as well? */
    /*        Should we apply a full set of line properties?  */
    if (from_plot && from_plot->plot_style == BOXES) {
	t_colorspec *bordercolor = &(from_plot->fill_properties.border_color);
	if (bordercolor->type == TC_LT && bordercolor->lt == LT_NODRAW)
	    return;
	apply_pm3dcolor(bordercolor);
    } else {
	if (pm3d.border.l_type == LT_NODRAW)
	    return;
	/* LT_DEFAULT means draw border in current color (set pm3d border retrace) */
	if (pm3d.border.l_type != LT_DEFAULT)
	    term_apply_lp_properties(&pm3d.border);
    }

    term->move(icorners[0].x, icorners[0].y);
    for (i = nv-1; i >= 0; i--) {
	term->vector(icorners[i].x, icorners[i].y);
    }
}

/*
 * Clip existing filled polygon again zmax or zmin.
 * We already know this is a convex polygon with at least one vertex in range.
 * The clipped polygon may have as few as 3 vertices or as many as n+2.
 * Returns the new number of vertices after clipping.
 */
int
clip_filled_polygon( struct surface_points *from_plot, int index,
		    gpdPoint *inpts, gpdPoint *outpts, int nv )
{
    int current = 0;	/* The vertex we are now considering */
    int next = 0;	/* The next vertex */
    int nvo = 0;	/* Number of vertices after clipping */
    int noutrange = 0;	/* Number of out-range points */
    int nover = 0;
    int nunder = 0;
    double fraction;
    double zmin = axis_array[FIRST_Z_AXIS].min;
    double zmax = axis_array[FIRST_Z_AXIS].max;

    /* classify inrange/outrange vertices */
    static int *outrange = NULL;
    static int maxvert = 0;
    if (nv > maxvert) {
	maxvert = nv;
	outrange = gp_realloc(outrange, maxvert * sizeof(int), NULL);
    }
    if (zmin > zmax) {
	zmax = axis_array[FIRST_Z_AXIS].min;
	zmin = axis_array[FIRST_Z_AXIS].max;
    }

    /* clipping limits used by pm3d zclip and CONTOURFILL*/
    if (from_plot && from_plot->zclip) {
	if (zmin < from_plot->zclip[index].zlow)
	    zmin = from_plot->zclip[index].zlow;
	if (zmax > from_plot->zclip[index].zhigh)
	    zmax = from_plot->zclip[index].zhigh;
    }
    for (current = 0; current < nv; current++) {
	if (inrange( inpts[current].z, zmin, zmax )) {
	    outrange[current] = 0;
	} else if (inpts[current].z > zmax) {
	    outrange[current] = 1;
	    noutrange++;
	    nover++;
	} else if (inpts[current].z < zmin) {
	    outrange[current] = -1;
	    noutrange++;
	    nunder++;
	}
    }

    /* If all are in range, nothing to do */
    if (noutrange == 0)
	return 0;

    /* If all vertices are out of range on the same side, draw nothing */
    if (nunder == nv || nover == nv)
	return -1;

    for (current = 0; current < nv; current++) {
	next = (current+1) % nv;

	/* Current point is out of range */
	if (outrange[current]) {
	    if (!outrange[next]) {
		/* Current point is out-range but next point is in-range.
		 * Clip line segment from current-to-next and store as new vertex.
		 */
		fraction = ((inpts[current].z >= zmax ? zmax : zmin) - inpts[next].z)
			 / (inpts[current].z - inpts[next].z);
		outpts[nvo].x = inpts[next].x + fraction * (inpts[current].x - inpts[next].x);
		outpts[nvo].y = inpts[next].y + fraction * (inpts[current].y - inpts[next].y);
		outpts[nvo].z = inpts[current].z >= zmax ? zmax : zmin;
		nvo++;
	    }
	    else if ( /* clip_lines2 && */ (outrange[current] * outrange[next] < 0)) {
		/* Current point and next point are out of range on opposite
		 * sides of the z range.  Clip both ends of the segment.
		 */
		fraction = ((inpts[current].z >= zmax ? zmax : zmin) - inpts[next].z)
			 / (inpts[current].z - inpts[next].z);
		outpts[nvo].x = inpts[next].x + fraction * (inpts[current].x - inpts[next].x);
		outpts[nvo].y = inpts[next].y + fraction * (inpts[current].y - inpts[next].y);
		outpts[nvo].z = inpts[current].z >= zmax ? zmax : zmin;
		nvo++;
		fraction = ((inpts[next].z >= zmax ? zmax : zmin) - outpts[nvo-1].z)
			 / (inpts[next].z - outpts[nvo-1].z);
		outpts[nvo].x = outpts[nvo-1].x + fraction * (inpts[next].x - outpts[nvo-1].x);
		outpts[nvo].y = outpts[nvo-1].y + fraction * (inpts[next].y - outpts[nvo-1].y);
		outpts[nvo].z = inpts[next].z >= zmax ? zmax : zmin;
		nvo++;
	    }

	/* Current point is in range */
	} else {
	    outpts[nvo++] = inpts[current];
	    if (outrange[next]) {
		/* Current point is in-range but next point is out-range.
		 * Clip line segment from current-to-next and store as new vertex.
		 */
		fraction = ((inpts[next].z >= zmax ? zmax : zmin) - inpts[current].z)
			 / (inpts[next].z - inpts[current].z);
		outpts[nvo].x = inpts[current].x + fraction * (inpts[next].x - inpts[current].x);
		outpts[nvo].y = inpts[current].y + fraction * (inpts[next].y - inpts[current].y);
		outpts[nvo].z = inpts[next].z >= zmax ? zmax : zmin;
		nvo++;
	    }
	}
    }

    /* this is not supposed to happen, but ... */
    if (nvo <= 1)
	return -1;

    return nvo;
}


/*
 * returns 1 for top of pm3d surface towards the viewer
 *        -1 for bottom of pm3d surface towards the viewer
 * NB: the ordering of the quadrangle vertices depends on the scan direction.
 *     In the case of depth ordering, the user does not have good control
 *     over this.
 */
int
pm3d_side( struct coordinate *p0, struct coordinate *p1, struct coordinate *p2)
{
    struct vertex v[3];
    double u0, u1, v0, v1;

    /* Apply current view rotation to corners of this quadrangle */
    map3d_xyz(p0->x, p0->y, p0->z, &v[0]);
    map3d_xyz(p1->x, p1->y, p1->z, &v[1]);
    map3d_xyz(p2->x, p2->y, p2->z, &v[2]);

    /* projection of two adjacent edges */
    u0 = v[1].x - v[0].x;
    u1 = v[1].y - v[0].y;
    v0 = v[2].x - v[0].x;
    v1 = v[2].y - v[0].y;

    /* cross-product */
    return sgn(u0*v1 - u1*v0);
}

/*
 * Check required number of new quadrangles against current allocation.
 * If necessary, reallocate list of quadrangles.
 * By default extend to double the current list size, non-zero chunk
 * provides a different extension increment.
 */
TBOOLEAN
reserve_quadrangles( int needed, int chunk )
{
    int newsize = allocated_quadrangles;
    int increment = (chunk > 0) ? chunk : allocated_quadrangles;

    if (increment == 0)
	increment = 100;

    while (current_quadrangle + needed >= newsize)
	newsize += increment;
    if (newsize == allocated_quadrangles)
	return FALSE;

    quadrangles = gp_realloc(quadrangles, newsize * sizeof(quadrangle),
			"extend_quadrangles");
    allocated_quadrangles = newsize;
    return TRUE;
}

/*
 * Returns a pointer into the list of polygon vertices.
 * If necessary reallocates the entire list to ensure there is enough
 * room for the requested number of vertices.
 */
static gpdPoint *get_polygon( int size )
{
    /* Check size of current list, allocate more space if needed */
    if (next_polygon + size >= polygonlistsize) {
	polygonlistsize = 2*polygonlistsize + size;
	polygonlist = gp_realloc(polygonlist, polygonlistsize * sizeof(gpdPoint), NULL);
    }

    current_polygon = next_polygon;
    next_polygon = current_polygon + size;
    return &polygonlist[current_polygon];
}

void
free_polygonlist()
{
    free(polygonlist);
    polygonlist = NULL;
    current_polygon = 0;
    next_polygon = 0;
    polygonlistsize = 0;
}

void
pm3d_reset_after_error()
{
    pm3d_plot_at = 0;
    free_polygonlist();
}

/*
 * Ethan Merritt Sep 2021
 * When two pm3d surfaces intersect, the intersection line is jagged because
 * a decision is forced for each quadrangle it runs through.  One of the two
 * surfaces is treated as the "top" for the purpose of assigning a drawing
 * order, and the entire tile is drawn, occluding part of the other one.
 * This code implements a smoother treatment. Instead of picking one tile or
 * the other, both are split into two pieces sharing the true intersection
 * line as a new edge.
 * NOTES:
 * -	We only handle gridded surfaces where the intersecting rectangular
 *	tiles share [x,y] coordinates at all four corners. Each such tile is
 *	split into either two new rectangles or a triangle plus a pentagon.
 */

/*
 * The sort on x/y is imperfect because we use the coords from only
 * one of the four corners.  Remember that the other corners may
 * be +/- one grid spacing.
 */
static int
compare_xy_quad(SORTFUNC_ARGS arg1, SORTFUNC_ARGS arg2)
{
    const quadrangle* q1 = arg1;
    const quadrangle* q2 = arg2;

    if (q1->vertex.corners[0].x > q2->vertex.corners[0].x) return  1;
    if (q1->vertex.corners[0].x < q2->vertex.corners[0].x) return -1;
    if (q1->vertex.corners[0].y > q2->vertex.corners[0].y) return  1;
    if (q1->vertex.corners[0].y < q2->vertex.corners[0].y) return -1;
    return 0;
}

#define xy_same(q1,q2) \
   ((fabs(q1->vertex.corners[0].x - q2->vertex.corners[0].x) < x_tolerance) \
&&  (fabs(q1->vertex.corners[1].x - q2->vertex.corners[1].x) < x_tolerance) \
&&  (fabs(q1->vertex.corners[2].x - q2->vertex.corners[2].x) < x_tolerance) \
&&  (fabs(q1->vertex.corners[3].x - q2->vertex.corners[3].x) < x_tolerance) \
&&  (fabs(q1->vertex.corners[0].y - q2->vertex.corners[0].y) < y_tolerance) \
&&  (fabs(q1->vertex.corners[1].y - q2->vertex.corners[1].y) < y_tolerance) \
&&  (fabs(q1->vertex.corners[2].y - q2->vertex.corners[2].y) < y_tolerance) \
&&  (fabs(q1->vertex.corners[3].y - q2->vertex.corners[3].y) < y_tolerance))

#define too_far(q1,q2) \
    (fabs(q2->vertex.corners[0].x - q1->vertex.corners[0].x) > 2.*x_spacing)

static void
find_intersection( gpdPoint *v1, gpdPoint *v2, gpdPoint *v3, gpdPoint *v4,
                   gpdPoint *intersection )
{
    double t = fabs((v3->z - v1->z) / ((v1->z - v2->z) - (v3->z -v4->z)));
    intersection->x = v1->x + t * (v2->x - v1->x);
    intersection->y = v1->y + t * (v2->y - v1->y);
    intersection->z = v1->z + t * (v2->z - v1->z);
}

static void
split_intersecting_surface_tiles()
{
    quadrangle *q1, *q2, *qnew;
    int iq1, iq2;
    int s0, s1, s2, s3;		/* -1 = bottom surface; 0 = intersection; 1 = top surface */
    int nv1, nv2;		/* number of vertices in piece1, piece2 */
    gpdPoint *ptlist[8];	/* list of 4 vertices of q1 plus possible intersections */
    int side[8];		/* top/bottom/intersection status of points in ptlist */
    gpdPoint intersection[2];	/* We know there are only two intersections */
    gpdPoint piece1[5], piece2[5];
    int pass;

    /* These calculations all assume a simple grid shared by all plots in the superposition */
    double x_spacing = fabs(axis_array[U_AXIS].max -axis_array[U_AXIS].min)/(samples_1-1);
    double y_spacing = fabs(axis_array[V_AXIS].max -axis_array[V_AXIS].min)/(iso_samples_1-1);
    double x_tolerance = x_spacing * 1.e-8;
    double y_tolerance = y_spacing * 1.e-8;

    if (!current_quadrangle || !quadrangles)
	return;

    /* Sort quadrangles on x and y */
    gp_qsort(quadrangles, current_quadrangle, sizeof(quadrangle), compare_xy_quad);

    /* Step through the list of quadrangles.
     * For each quadrangle look ahead to find another with the same [x,y] corners.
     * Check this pair for intersection along z.
     * If no, proceed to next in list.
     * If yes, split each of them into two new 'quadrangles' (may be triangle or pentagon)
     */
    for (iq1 = 1; iq1 < current_quadrangle; iq1++) {
	q1 = &(quadrangles[iq1]);
	if (q1->type != QUAD_TYPE_NORMAL)
	    continue;
	for (iq2 = iq1+1; iq2 < current_quadrangle; iq2++) {
	    q2 = &(quadrangles[iq2]);
	    if (q2->type != QUAD_TYPE_NORMAL)
		continue;
	    if (too_far(q1,q2))
		break;
	    if (!xy_same(q1,q2))
		continue;

	    s0 = (q1->vertex.corners[0].z - q2->vertex.corners[0].z) >= 0 ? 1 : -1;
	    s1 = (q1->vertex.corners[1].z - q2->vertex.corners[1].z) >= 0 ? 1 : -1;
	    s2 = (q1->vertex.corners[2].z - q2->vertex.corners[2].z) >= 0 ? 1 : -1;
	    s3 = (q1->vertex.corners[3].z - q2->vertex.corners[3].z) >= 0 ? 1 : -1;
	    if (s0 > 0 && s1 > 0 && s2 > 0 && s3 > 0)
		continue;
	    if (s0 < 0 && s1 < 0 && s2 < 0 && s3 < 0)
		continue;

	    /* Found a pair of intersecting quadrangles!
	     * Make sure the list is long enough to hold new pieces.
	     */
	    if (reserve_quadrangles(4, 4*samples_1)) {
		q1 = &(quadrangles[iq1]);
		q2 = &(quadrangles[iq2]);
	    }

	    /* Construct a list of pointers to the 4 original vertices of q1
	     * and to the 2 intersection points.  We don't yet know which sides
	     * the intersections lie on, so we leave room for 2 NULL pointers
	     * corresponding to the sides with no intersection.
	     */
	    ptlist[1] = ptlist[3] = ptlist[5] = ptlist[7] = NULL;
	    side[1] = side[3] = side[5] = side[7] = 0;
	    side[0] = s0;
	    side[2] = s1;
	    side[4] = s2;
	    side[6] = s3;

	    for (pass = 1; pass <= 2; pass++) {
		quadrangle *qt = (pass==1) ? q1 : q2;
		quadrangle *qb = (pass==1) ? q2 : q1;
		int next_int = 0;
		int i;

		ptlist[0] = &qt->vertex.corners[0];
		ptlist[2] = &qt->vertex.corners[1];
		ptlist[4] = &qt->vertex.corners[2];
		ptlist[6] = &qt->vertex.corners[3];
		if (s0*s1 != 1) {
		    find_intersection(&(qb->vertex.corners[0]), &(qb->vertex.corners[1]),
				      &(qt->vertex.corners[0]), &(qt->vertex.corners[1]),
				      &intersection[next_int]);
		    ptlist[1] = &intersection[next_int++];
		}
		if (s1*s2 != 1) {
		    find_intersection(&(qb->vertex.corners[1]), &(qb->vertex.corners[2]),
				      &(qt->vertex.corners[1]), &(qt->vertex.corners[2]),
				      &intersection[next_int]);
		    ptlist[3] = &intersection[next_int++];
		}
		if (s2*s3 != 1) {
		    find_intersection(&(qb->vertex.corners[2]), &(qb->vertex.corners[3]),
				      &(qt->vertex.corners[2]), &(qt->vertex.corners[3]),
				      &intersection[next_int]);
		    ptlist[5] = &intersection[next_int++];
		}
		if (s3*s0 != 1) {
		    find_intersection(&(qb->vertex.corners[3]), &(qb->vertex.corners[0]),
				      &(qt->vertex.corners[3]), &(qt->vertex.corners[0]),
				      &intersection[next_int]);
		    ptlist[7] = &intersection[next_int++];
		}
		/* Should never happen */
		if (next_int != 2)
		    int_error(NO_CARET, "calculation of surface intersection failed n=%d", next_int);

		/* Copy the "top" vertices to piece1, "bottom" to piece2 */
		nv1 = nv2 = 0;
		for (i=0; i<8; i++) {
		    if (ptlist[i] != NULL && side[i] >= 0)
			piece1[nv1++] = *ptlist[i];
		    if (ptlist[i] != NULL && side[i] <= 0)
			piece2[nv2++] = *ptlist[i];
		}

		/* Add piece1 as a new quadrangle at the end of the list */
		if (nv1 > 2) {
		    qnew = &quadrangles[current_quadrangle++];
		    qnew->from_plot = qt->from_plot;
		    qnew->gray = qt->gray;
		    qnew->index = qt->index;
		    qnew->qcolor = qt->qcolor;
#ifdef WITH_2ND_SORTKEY
		    qnew->sequence = qt->sequence;
#endif

		    qnew->vertex.corners[0] = piece1[0];
		    qnew->vertex.corners[1] = piece1[1];
		    qnew->vertex.corners[2] = piece1[2];
		    qnew->vertex.corners[3] = piece1[3];
		    qnew->type = QUAD_TYPE_4SIDES;
		    if (nv1 == 3) {
			qnew->vertex.corners[3] = piece1[2];
			qnew->type = QUAD_TYPE_TRIANGLE;
		    }
		    if (nv1 == 5) {
			gpdPoint *save_corners = get_polygon(nv1);
			qnew->vertex.array_index = current_polygon;
			memcpy(save_corners, piece1, nv1 * sizeof(gpdPoint));
			save_corners[2].c = nv1;
			qnew->type = QUAD_TYPE_LARGEPOLYGON;
		    }
		}

		/* Add piece2 as a new quadrangle at the end of the list */
		if (nv2 > 2) {
		    qnew = &quadrangles[current_quadrangle++];
		    qnew->from_plot = qt->from_plot;
		    qnew->gray = qt->gray;
		    qnew->index = qt->index;
		    qnew->qcolor = qt->qcolor;
#ifdef WITH_2ND_SORTKEY
		    qnew->sequence = qt->sequence;
#endif

		    qnew->vertex.corners[0] = piece2[0];
		    qnew->vertex.corners[1] = piece2[1];
		    qnew->vertex.corners[2] = piece2[2];
		    qnew->vertex.corners[3] = piece2[3];
		    qnew->type = QUAD_TYPE_4SIDES;
		    if (nv2 == 3) {
			qnew->vertex.corners[3] = piece2[2];
			qnew->type = QUAD_TYPE_TRIANGLE;
		    }
		    if (nv2 == 5) {
			gpdPoint *save_corners = get_polygon(nv2);
			qnew->vertex.array_index = current_polygon;
			qnew->type = QUAD_TYPE_LARGEPOLYGON;
			memcpy(save_corners, piece2, nv2 * sizeof(gpdPoint));
			save_corners[2].c = nv2;
		    }
		}
	    }

	    /* Mark original q1 and q2 as not to be drawn */
	    q1->type = QUAD_TYPE_SKIP;
	    q2->type = QUAD_TYPE_SKIP;
	    break;
	}
    }

}
