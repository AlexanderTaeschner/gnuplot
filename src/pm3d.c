#ifndef lint
static char *RCSid() { return RCSid("$Id: pm3d.c,v 1.66 2006/09/23 22:13:26 sfeam Exp $"); }
#endif

/* GNUPLOT - pm3d.c */

/*[
 *
 * Petr Mikulik, since December 1998
 * Copyright: open source as much as possible
 *
 * What is here: global variables and routines for the pm3d splotting mode.
 * This file is included only if PM3D is defined.
 *
]*/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "pm3d.h"
#include "alloc.h"
#include "axis.h"
#include "graphics.h"
#include "graph3d.h"
#include "hidden3d.h"		/* p_vertex & map3d_xyz() */
#include "plot2d.h"
#include "plot3d.h"
#include "setshow.h"		/* for surface_rot_z */
#include "term_api.h"		/* for lp_use_properties() */
#include "command.h"		/* for c_token */

#include <stdlib.h> /* qsort() */


/*
  Global options for pm3d algorithm (to be accessed by set / show).
*/

pm3d_struct pm3d = {
    "s",			/* where[6] */
    PM3D_FLUSH_BEGIN,		/* flush */
    0,				/* no flushing triangles */
    PM3D_SCANS_AUTOMATIC,	/* scans direction is determined automatically */
    PM3D_CLIP_4IN,		/* clipping: all 4 points in ranges */
    0,				/* no pm3d hidden3d is drawn */
#if PM3D_HAVE_SOLID
    0,				/* solid (off by default, that means `transparent') */
#endif /* PM3D_HAVE_SOLID */
    PM3D_EXPLICIT,		/* implicit */
    PM3D_WHICHCORNER_MEAN,	/* color from which corner(s) */
    1,				/* interpolate along scanline */
    1				/* interpolate between scanlines */
};

typedef struct {
    double gray;
    double z; /* maximal z value after rotation to graph coordinate system */
    gpdPoint corners[4];
    gpiPoint icorners[4]; /* also if EXTENDED_COLOR_SPECS is not defined */
} quadrangle;

static int allocated_quadrangles = 0;
static int current_quadrangle = 0;
static quadrangle* quadrangles = (quadrangle*)0;

/* Internal prototypes for this module */
static TBOOLEAN plot_has_palette;
static TBOOLEAN plot_wants_colorbox;
static double geomean4 __PROTO((double, double, double, double));
static double median4 __PROTO((double, double, double, double));
static void pm3d_plot __PROTO((struct surface_points *, int));
static void pm3d_option_at_error __PROTO((void));
static void pm3d_rearrange_part __PROTO((struct iso_curve *, const int, struct iso_curve ***, int *));
static void filled_color_contour_plot  __PROTO((struct surface_points *, int));

/*
 * Utility routines.
 */

/* Geometrical mean = pow( prod(x_i > 0) x_i, 1/N )
 * Sign of the result: result is positive if 3 or 4 x_i are positive,
 * it is negative if 3 or all 4 x_i are negative. Helps to splot surface
 * with all color coordinates negative.
 */
static double
geomean4 (double x1, double x2, double x3, double x4)
{
#if 0
    /* return 0 if any of the number is negative */
    if (x1 <= 0) x1 = 1;
    if (x2 > 0) x1 *= x2;
    if (x3 > 0) x1 *= x3;
    if (x4 > 0) x1 *= x4;
    return pow(x1, 0.25);
#else
    /* honor signess, i.e. sign(geomean) = sign(prod(x_i)) */
    int neg = (x1 < 0) + (x2 < 0) + (x3 < 0) + (x4 < 0);
    x1 *= x2 * x3 * x4;
    if (x1 == 0) return 0;
    /* pow(x, 0.25) is slightly faster than sqrt(sqrt(x)) */
    x1 = sqrt(sqrt(fabs(x1)));
#if 0
    /* such a warning could be helpful, but under normal usage it is just an overhead */
    if (neg > 1 && interactive && notwarned) {
	    int notwarned = 1;  ... to be set on every new splot
	    if (notwarned)
		int_warn(NO_CARET, "corners2color geomean with negative data points");
	    notwarned = 0;
    }
#endif
    return (neg <= 2) ? x1 : -x1;
#endif
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


/*
* Now the routines which are really just those for pm3d.c
*/

/*
 * Rescale z to cb values. Nothing to do if both z and cb are linear or log of the
 * same base, other it has to un-log z and subsequently log it again.
 */
double
z2cb(double z)
{
    if (!Z_AXIS.log && !CB_AXIS.log) /* both are linear */
	return z;
    if (Z_AXIS.log && !CB_AXIS.log) /* log z, linear cb */
	return exp(z * Z_AXIS.log_base); /* unlog(z) */
    if (!Z_AXIS.log && CB_AXIS.log) /* linear z, log cb */
	return (log(z) / CB_AXIS.log_base);
    /* both are log */
    if (Z_AXIS.base==CB_AXIS.base) /* can we compare double numbers like that? */
	return z;
    return z * Z_AXIS.log_base / CB_AXIS.log_base; /* log_cb(unlog_z(z)) */
}


/*
 * Rescale cb (color) value into the interval of grays [0,1], taking care
 * of palette being positive or negative.
 * Note that it is OK for logarithmic cb-axis too.
 */
double
cb2gray(double cb)
{
    if (cb <= CB_AXIS.min)
	return (sm_palette.positive == SMPAL_POSITIVE) ? 0 : 1;
    if (cb >= CB_AXIS.max)
	return (sm_palette.positive == SMPAL_POSITIVE) ? 1 : 0;
    cb = (cb - CB_AXIS.min)
      / (CB_AXIS.max - CB_AXIS.min);
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

#if 0
    fprintf(stderr, "(pm3d_rearrange_part) invert       = %d\n", *invert);
    fprintf(stderr, "(pm3d_rearrange_part) invert_order = %d\n", invert_order);
#endif

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

static int compare_quadrangles(const void* v1, const void* v2)
{
    const quadrangle* q1 = (const quadrangle*)v1;
    const quadrangle* q2 = (const quadrangle*)v2;

    if (q1->z > q2->z)
	return 1;
    else if (q1->z < q2->z)
	return -1;
    else
	return 0;
}

void pm3d_depth_queue_clear(void)
{
    if (pm3d.direction != PM3D_DEPTH)
	return;

    if (quadrangles)
	free(quadrangles);
    quadrangles = (quadrangle*)0;
    allocated_quadrangles = 0;
    current_quadrangle = 0;
}

void pm3d_depth_queue_flush(void)
{
    if (pm3d.direction != PM3D_DEPTH)
	return;

    if (current_quadrangle > 0 && quadrangles) {

	quadrangle* qp;
	quadrangle* qe;
	gpdPoint* gpdPtr;
	gpiPoint* gpiPtr;
	vertex out;
	double z = 0; /* assignment keeps the compiler happy */
	double w = trans_mat[3][3];
	int i;

	for (qp = quadrangles, qe = quadrangles + current_quadrangle; qp != qe; qp++) {

	    gpdPtr = qp->corners;
	    gpiPtr = qp->icorners;

	    for (i = 0; i < 4; i++, gpdPtr++, gpiPtr++) {

		map3d_xyz(gpdPtr->x, gpdPtr->y, gpdPtr->z, &out);

		if (i == 0 || out.z > z)
		    z = out.z;

		gpiPtr->x = (unsigned int) ((out.x * xscaler / w) + xmiddle);
		gpiPtr->y = (unsigned int) ((out.y * yscaler / w) + ymiddle);
	    }

	    qp->z = z; /* maximal z value of all four corners */
	}

	qsort(quadrangles, current_quadrangle, sizeof (quadrangle), compare_quadrangles);

	for (qp = quadrangles, qe = quadrangles + current_quadrangle; qp != qe; qp++) {

	    set_color(qp->gray);
	    ifilled_quadrangle(qp->icorners);
	}
    }

    pm3d_depth_queue_clear();
}

/*
 * Now the implementation of the pm3d (s)plotting mode
 *
 * Note: the input parameter at_which_z is char, but an old HP cc requires
 * ANSI C K&R routines with int only.
 */
static void
pm3d_plot(struct surface_points *this_plot, int at_which_z)
{
    int j, i, i1, ii, ii1, from, curve, scan, up_to, up_to_minus, invert = 0;
    int go_over_pts, max_pts;
    int are_ftriangles, ftriangles_low_pt = -999, ftriangles_high_pt = -999;
    struct iso_curve *scanA, *scanB;
    struct coordinate GPHUGE *pointsA, *pointsB;
    struct iso_curve **scan_array;
    int scan_array_n;
    double avgC, gray;
    double cb1, cb2, cb3, cb4;
    gpdPoint corners[4];
#ifdef EXTENDED_COLOR_SPECS
    gpiPoint icorners[4];
#endif
    gpdPoint **bl_point = NULL; /* used for bilinear interpolation */

    /* just a shortcut */
    TBOOLEAN color_from_column = this_plot->pm3d_color_from_column;

    if (this_plot == NULL)
	return;

    if (at_which_z != PM3D_AT_BASE && at_which_z != PM3D_AT_TOP && at_which_z != PM3D_AT_SURFACE)
	return;

    /* return if the terminal does not support filled polygons */
    if (!term->filled_polygon)
	return;

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
    curve = 0;

    pm3d_rearrange_scan_array(this_plot, &scan_array, &scan_array_n, &invert, (struct iso_curve ***) 0, (int *) 0, (int *) 0);

    if (pm3d.direction == PM3D_DEPTH) {

	for (scan = 0; scan < this_plot->num_iso_read - 1; scan++) {

	    scanA = scan_array[scan];
	    scanB = scan_array[scan + 1];

	    are_ftriangles = pm3d.ftriangles && (scanA->p_count != scanB->p_count);
	    if (!are_ftriangles)
		allocated_quadrangles += GPMIN(scanA->p_count, scanB->p_count) - 1;
	    else {
		allocated_quadrangles += GPMAX(scanA->p_count, scanB->p_count) - 1;
	    }
	}

	quadrangles = (quadrangle*)gp_realloc(quadrangles, allocated_quadrangles * sizeof (quadrangle), "pm3d_plot->quadrangles");
	/* DEBUG: fprintf(stderr, "allocated_quadrangles = %d\n", allocated_quadrangles); */
    }
    /* pm3d_rearrange_scan_array(this_plot, (struct iso_curve***)0, (int*)0, &scan_array, &invert); */

#if 0
    /* debugging: print scan_array */
    for (scan = 0; scan < this_plot->num_iso_read; scan++) {
	printf("**** SCAN=%d  points=%d\n", scan, scan_array[scan]->p_count);
    }
#endif

#if 0
    /* debugging: this loop prints properties of all scans */
    for (scan = 0; scan < this_plot->num_iso_read; scan++) {
	struct coordinate GPHUGE *points;
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
    if (pm3d.interp_i > 1 || pm3d.interp_j > 1) {
	bl_point = (gpdPoint **)gp_alloc(sizeof(gpdPoint*) * (pm3d.interp_i+1), "bl-interp along scan");
	for (i1 = 0; i1 <= pm3d.interp_i; i1++)
	    bl_point[i1] = (gpdPoint *)gp_alloc(sizeof(gpdPoint) * (pm3d.interp_j+1), "bl-interp between scan");
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
#if 0
	printf("\n#IsoCurveA = scan nb %d has %d points   ScanB has %d points\n", scan, scanA->p_count, scanB->p_count);
#endif
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
	    /* the j-subrange of quadrangles; in the remaing of the interval
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
#if 0
	    fprintf(stderr,"j=%i:  i=%i i1=%i  [%i]   ii=%i ii1=%i  [%i]\n",j,i,i1,scanA->p_count,ii,ii1,scanB->p_count);
#endif
	    /* choose the clipping method */
	    if (pm3d.clip == PM3D_CLIP_4IN) {
		/* (1) all 4 points of the quadrangle must be in x and y range */
		if (!(pointsA[i].type == INRANGE && pointsA[i1].type == INRANGE &&
		      pointsB[ii].type == INRANGE && pointsB[ii1].type == INRANGE))
		    continue;
	    } else {		/* (pm3d.clip == PM3D_CLIP_1IN) */
		/* (2) all 4 points of the quadrangle must be defined */
		if (pointsA[i].type == UNDEFINED || pointsA[i1].type == UNDEFINED ||
		    pointsB[ii].type == UNDEFINED || pointsB[ii1].type == UNDEFINED)
		    continue;
		/* and at least 1 point of the quadrangle must be in x and y range */
		if (pointsA[i].type == OUTRANGE && pointsA[i1].type == OUTRANGE &&
		    pointsB[ii].type == OUTRANGE && pointsB[ii1].type == OUTRANGE)
		    continue;
	    }
#ifdef EXTENDED_COLOR_SPECS
	    if (!supply_extended_color_specs) {
#endif
		/* get the gray as the average of the corner z positions (note: log already in)
		   I always wonder what is faster: d*0.25 or d/4? Someone knows? -- 0.25 (joze) */
		if (color_from_column) {
		    /* color is set in plot3d.c:get_3ddata() */
		    cb1 = pointsA[i].CRD_COLOR;
		    cb2 = pointsA[i1].CRD_COLOR;
		    cb3 = pointsB[ii].CRD_COLOR;
		    cb4 = pointsB[ii1].CRD_COLOR;
		} else {
		    cb1 = z2cb(pointsA[i].z);
		    cb2 = z2cb(pointsA[i1].z);
		    cb3 = z2cb(pointsB[ii].z);
		    cb4 = z2cb(pointsB[ii1].z);
		}
		switch (pm3d.which_corner_color) {
		    case PM3D_WHICHCORNER_MEAN: avgC = (cb1 + cb2 + cb3 + cb4) * 0.25; break;
		    case PM3D_WHICHCORNER_GEOMEAN: avgC = geomean4(cb1, cb2, cb3, cb4); break;
		    case PM3D_WHICHCORNER_MEDIAN: avgC = median4(cb1, cb2, cb3, cb4); break;
		    case PM3D_WHICHCORNER_MIN: avgC = minimum4(cb1, cb2, cb3, cb4); break;
		    case PM3D_WHICHCORNER_MAX: avgC = maximum4(cb1, cb2, cb3, cb4); break;
		    case PM3D_WHICHCORNER_C1: avgC = cb1; break;
		    case PM3D_WHICHCORNER_C2: avgC = cb2; break;
		    case PM3D_WHICHCORNER_C3: avgC = cb3; break;
		    case PM3D_WHICHCORNER_C4: avgC = cb4; break;
		    default: int_error(NO_CARET, "cannot be here"); avgC = 0;
		}
		/* transform z value to gray, i.e. to interval [0,1] */
		gray = cb2gray(avgC);
#if 0
		/* print the quadrangle with the given color */
		printf("averageColor %g\tgray=%g\tM %g %g L %g %g L %g %g L %g %g\n",
		       avgC,
		       gray,
		       pointsA[i].x, pointsA[i].y, pointsB[ii].x, pointsB[ii].y,
		       pointsB[ii1].x, pointsB[ii1].y, pointsA[i1].x, pointsA[i1].y);
#endif
		/* set the color */
		set_color(gray);
#ifdef EXTENDED_COLOR_SPECS
	    }
#endif
	    corners[0].x = pointsA[i].x;
	    corners[0].y = pointsA[i].y;
	    corners[1].x = pointsB[ii].x;
	    corners[1].y = pointsB[ii].y;
	    corners[2].x = pointsB[ii1].x;
	    corners[2].y = pointsB[ii1].y;
	    corners[3].x = pointsA[i1].x;
	    corners[3].y = pointsA[i1].y;

	    if ( pm3d.interp_i > 1 || pm3d.interp_j > 1 || at_which_z == PM3D_AT_SURFACE) {
		/* always supply the z value if
		 * EXTENDED_COLOR_SPECS is defined
		 */
		corners[0].z = pointsA[i].z;
		corners[1].z = pointsB[ii].z;
		corners[2].z = pointsB[ii1].z;
		corners[3].z = pointsA[i1].z;
	    }
#ifdef EXTENDED_COLOR_SPECS
	    if (supply_extended_color_specs) {
		if (color_from_column) {
		    icorners[0].z = pointsA[i].CRD_COLOR;
		    icorners[1].z = pointsB[ii].CRD_COLOR;
		    icorners[2].z = pointsB[ii1].CRD_COLOR;
		    icorners[3].z = pointsA[i1].CRD_COLOR;
		} else {
		    /* the target wants z and gray value */
		    icorners[0].z = pointsA[i].z;
		    icorners[1].z = pointsB[ii].z;
		    icorners[2].z = pointsB[ii1].z;
		    icorners[3].z = pointsA[i1].z;
		}
		for (i = 0; i < 4; i++) {
		    icorners[i].spec.gray =
			cb2gray( color_from_column ? icorners[i].z : z2cb(icorners[i].z) );
		}
	    }
	    if (pm3d.direction == PM3D_DEPTH) {

		/* copy quadrangle */
		quadrangle* qp = quadrangles + current_quadrangle;
		memcpy(qp->corners, corners, 4 * sizeof (gpdPoint));
		qp->gray = gray;
		for (i = 0; i < 4; i++) {
		    qp->icorners[i].z = icorners[i].z;
		    qp->icorners[i].spec.gray = icorners[i].spec.gray;
		}
		current_quadrangle++;

	    } else
	    filled_quadrangle(corners, icorners);
#else
	    if (pm3d.direction == PM3D_DEPTH) {

		/* copy quadrangle */
		quadrangle* qp = quadrangles + current_quadrangle;
		memcpy(qp->corners, corners, 4 * sizeof (gpdPoint));
		qp->gray = gray;
		current_quadrangle++;

	    } else
	    if (pm3d.interp_i > 1 || pm3d.interp_j > 1) {
		/* Interpolation is enabled.
		 * interp_i is the # of points along scan lines
		 * interp_j is the # of points between scan lines
		 * Algorithm is to first sample i points along the scan lines
		 * defined by corners[3],corners[0] and corners[2],corners[1]. */
		int j1;
		for (i1 = 0; i1 <= pm3d.interp_i; i1++) {
		    bl_point[i1][0].x = 
			((corners[3].x - corners[0].x) / pm3d.interp_i) * i1 + corners[0].x;
		    bl_point[i1][pm3d.interp_j].x = 
			((corners[2].x - corners[1].x) / pm3d.interp_i) * i1 + corners[1].x;
		    bl_point[i1][0].y =
			((corners[3].y - corners[0].y) / pm3d.interp_i) * i1 + corners[0].y;
		    bl_point[i1][pm3d.interp_j].y =
			((corners[2].y - corners[1].y) / pm3d.interp_i) * i1 + corners[1].y;
		    bl_point[i1][0].z =
			((corners[3].z - corners[0].z) / pm3d.interp_i) * i1 + corners[0].z;
		    bl_point[i1][pm3d.interp_j].z =
			((corners[2].z - corners[1].z) / pm3d.interp_i) * i1 + corners[1].z;
		    /* Next we sample j points between each of the new points
		     * created in the previous step (this samples between
		     * scan lines) in the same manner. */
		    for (j1 = 1; j1 < pm3d.interp_j; j1++) {
			bl_point[i1][j1].x =
			    ((bl_point[i1][pm3d.interp_j].x - bl_point[i1][0].x) / pm3d.interp_j) *
				j1 + bl_point[i1][0].x;
			bl_point[i1][j1].y =
			    ((bl_point[i1][pm3d.interp_j].y - bl_point[i1][0].y) / pm3d.interp_j) *
				j1 + bl_point[i1][0].y;
			bl_point[i1][j1].z =
			    ((bl_point[i1][pm3d.interp_j].z - bl_point[i1][0].z) / pm3d.interp_j) *
				j1 + bl_point[i1][0].z;
		    }
		}
		/* Once all points are created, move them into an appropriate
		 * structure and call set_color on each to retrieve the
		 * correct color mapping for this new sub-sampled quadrangle. */
		for (i1 = 0; i1 < pm3d.interp_i; i1++) {
		    for (j1 = 0; j1 < pm3d.interp_j; j1++) {
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
#if 0
			printf("(%g,%g),(%g,%g),(%g,%g),(%g,%g)\n",
			    corners[0].x, corners[0].y,
			    corners[1].x, corners[1].y,
			    corners[2].x, corners[2].y,
			    corners[3].x, corners[3].y);
#endif
			cb1 = z2cb(corners[0].z);
		    	cb2 = z2cb(corners[1].z);
	    		cb3 = z2cb(corners[2].z);
    			cb4 = z2cb(corners[3].z);
			switch (pm3d.which_corner_color) {
			    case PM3D_WHICHCORNER_MEAN: avgC = (cb1 + cb2 + cb3 + cb4) * 0.25; break;
			    case PM3D_WHICHCORNER_GEOMEAN: avgC = geomean4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MEDIAN: avgC = median4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MIN: avgC = minimum4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_MAX: avgC = maximum4(cb1, cb2, cb3, cb4); break;
			    case PM3D_WHICHCORNER_C1: avgC = cb1; break;
			    case PM3D_WHICHCORNER_C2: avgC = cb2; break;
			    case PM3D_WHICHCORNER_C3: avgC = cb3; break;
			    case PM3D_WHICHCORNER_C4: avgC = cb4; break;
			    default: int_error(NO_CARET, "cannot be here"); avgC = 0;
			}
			/* transform z value to gray, i.e. to interval [0,1] */
			gray = cb2gray(avgC);
			set_color(gray);
			filled_quadrangle(corners);
		    }
		}
	    } else {
		filled_quadrangle(corners);
	    } /* interpolate between points */
#endif
	} /* loop quadrangles over points of two subsequent scans */
    } /* loop over scans */

    if (bl_point) {
	for (i1 = 0; i1 <= pm3d.interp_i; i1++)
	    free(bl_point[i1]);
	free(bl_point);
    }
    /* free memory allocated by scan_array */
    free(scan_array);
}				/* end of pm3d splotting mode */


/*
 *  Now the implementation of the filled color contour plot
*/
static void
filled_color_contour_plot(struct surface_points *this_plot, int contours_where)
{
    double gray;
    struct gnuplot_contours *cntr;

    /* just a shortcut */
    TBOOLEAN color_from_column = this_plot->pm3d_color_from_column;

    if (this_plot == NULL || this_plot->contours == NULL)
	return;
    if (contours_where != CONTOUR_SRF && contours_where != CONTOUR_BASE)
	return;

    /* return if the terminal does not support filled polygons */
    if (!term->filled_polygon)
	return;

    /* TODO: CHECK FOR NUMBER OF POINTS IN CONTOUR: IF TOO SMALL, THEN IGNORE! */
    cntr = this_plot->contours;
    while (cntr) {
	printf("# Contour: points %i, z %g, label: %s\n", cntr->num_pts, cntr->coords[0].z, (cntr->label) ? cntr->label : "<no>");
	if (cntr->isNewLevel) {
	    printf("\t...it isNewLevel\n");
	    /* contour split across chunks */
	    /* fprintf(gpoutfile, "\n# Contour %d, label: %s\n", number++, c->label); */
	    /* What is the color? */
	    /* get the z-coordinate */
	    /* transform contour z-coordinate value to gray, i.e. to interval [0,1] */
	    if (color_from_column)
		gray = cb2gray(cntr->coords[0].CRD_COLOR);
	    else
		gray = cb2gray( z2cb(cntr->coords[0].z) );
	    set_color(gray);
	}
	/* draw one countour */
	if (contours_where == CONTOUR_SRF)	/* at CONTOUR_SRF */
	    filled_polygon_3dcoords(cntr->num_pts, cntr->coords);
	else			/* at CONTOUR_BASE */
	    filled_polygon_3dcoords_zfixed(cntr->num_pts, cntr->coords, base_z);
	/* next contour */
	cntr = cntr->next;
    }
}				/* end of filled color contour plot splot mode */


/*
 * unset pm3d for the reset command
 */
void
pm3d_reset()
{
    strcpy(pm3d.where, "s");
    pm3d.flush = PM3D_FLUSH_BEGIN;
    pm3d.ftriangles = 0;
    pm3d.direction = PM3D_SCANS_AUTOMATIC;
    pm3d.clip = PM3D_CLIP_4IN;
    pm3d.hidden3d_tag = 0;
#if PM3D_HAVE_SOLID
    pm3d.solid = 0;
#endif /* PM3D_HAVE_SOLID */
    pm3d.implicit = PM3D_EXPLICIT;
    pm3d.which_corner_color = PM3D_WHICHCORNER_MEAN;
    pm3d.interp_i = 1;
    pm3d.interp_j = 1;
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

    if (!where[0]) {
	return;
    }

    /* for pm3dCompress.awk */
    if (gppsfile)
	fputs("%pm3d_map_begin\n", gppsfile);

    for (; where[i]; i++) {
	pm3d_plot(plot, where[i]);
    }

    if (strchr(where, 'C') != NULL) {
	/* !!!!! FILLED COLOR CONTOURS, *UNDOCUMENTED*
	   !!!!! LATER CHANGE TO STH LIKE
	   !!!!!   (if_filled_contours_requested)
	   !!!!!      ...
	   Currently filled color contours do not work because gnuplot generates
	   open contour lines, i.e. not closed on the graph boundary.
	 */
	if (draw_contour & CONTOUR_SRF)
	    filled_color_contour_plot(plot, CONTOUR_SRF);
	if (draw_contour & CONTOUR_BASE)
	    filled_color_contour_plot(plot, CONTOUR_BASE);
    }

    /* for pm3dCompress.awk */
    if (gppsfile)
	fputs("%pm3d_map_end\n", gppsfile);
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
	if (*c != 'C') /* !!!!! CONTOURS, UNDOCUMENTED, THIS LINE IS TEMPORARILY HERE !!!!! */
	    if (*c != PM3D_AT_BASE && *c != PM3D_AT_TOP && *c != PM3D_AT_SURFACE) {
		pm3d_option_at_error();
		return 1;
	}
    }
    c_token++;
    return 0;
}

/* Set flag plot_has_palette to TRUE if there is any element on the graph
 * which requires palette of continuous colors.
 */
void
set_plot_with_palette(int plot_num, int plot_mode)
{
    struct surface_points *this_3dplot = first_3dplot;
    struct curve_points *this_2dplot = first_plot;
    int surface = 0;
    struct text_label *this_label = first_label;
    TBOOLEAN want_palette_but_not_colorbox = FALSE;

    plot_has_palette = TRUE;
    plot_wants_colorbox = TRUE;
    /* Is pm3d switched on globally? */
    if (pm3d.implicit == PM3D_IMPLICIT)
	return;

    /* Check 2D plots */
    if (plot_mode == MODE_PLOT) {
	while (this_2dplot) {
#ifdef WITH_IMAGE
	    if (this_2dplot->plot_style == IMAGE)
		return;
#endif
	    if (this_2dplot->lp_properties.use_palette) {
		if (this_2dplot->lp_properties.pm3d_color.type <= TC_RGB)
		    want_palette_but_not_colorbox = TRUE;
		    /* don't return yet -- decide later whether showing color box is desirable */
		else
		    return;
	    }
#ifdef EAM_DATASTRINGS
	    if (this_2dplot->labels &&
		this_2dplot->labels->textcolor.type >= TC_CB)
		return;
#endif
	    this_2dplot = this_2dplot->next;
	}
    }

    /* Check 3D plots */
    if (plot_mode == MODE_SPLOT) {
	/* Any surface 'with pm3d', 'with image' or 'with line|dot palette'? */
	while (surface++ < plot_num) {
	    if (this_3dplot->plot_style == PM3DSURFACE)
		return;
#ifdef WITH_IMAGE
	    if (this_3dplot->plot_style == IMAGE)
		return;
#endif
	    if (this_3dplot->lp_properties.use_palette) {
	        int type = this_3dplot->lp_properties.pm3d_color.type;
		if (type == TC_LT || type == TC_LINESTYLE || type == TC_RGB)
		    want_palette_but_not_colorbox = TRUE;
		    /* don't return yet -- decide later whether showing color box is desirable */
		else
		    /* TC_DEFAULT: splot x with line|lp|dot palette */
		    return;
	    }
#ifdef EAM_DATASTRINGS
	    if (this_3dplot->labels &&
		this_3dplot->labels->textcolor.type >= TC_CB)
		return;
#endif
	    this_3dplot = this_3dplot->next_sp;
	}
    }

    /* Any label with 'textcolor palette'? */
#define TC_USES_PALETTE(tctype) (tctype==TC_Z) || (tctype==TC_CB) || (tctype==TC_FRAC)
    for (; this_label != NULL; this_label = this_label->next) {
	if (TC_USES_PALETTE(this_label->textcolor.type))
	    return;
    }
    /* Any of title, xlabel, ylabel, zlabel, ... with 'textcolor palette'? */
    if (TC_USES_PALETTE(title.textcolor.type)) return;
    if (TC_USES_PALETTE(axis_array[FIRST_X_AXIS].label.textcolor.type)) return;
    if (TC_USES_PALETTE(axis_array[FIRST_Y_AXIS].label.textcolor.type)) return;
    if (TC_USES_PALETTE(axis_array[SECOND_X_AXIS].label.textcolor.type)) return;
    if (TC_USES_PALETTE(axis_array[SECOND_Y_AXIS].label.textcolor.type)) return;
    if (plot_mode == MODE_SPLOT)
	if (TC_USES_PALETTE(axis_array[FIRST_Z_AXIS].label.textcolor.type)) return;
    if (TC_USES_PALETTE(axis_array[COLOR_AXIS].label.textcolor.type)) return;
#undef TC_USES_PALETTE

    /* Palette with continuous colors is not used. */
    if (want_palette_but_not_colorbox == FALSE)
	plot_has_palette = FALSE; /* otherwise it stays TRUE */
    plot_wants_colorbox = FALSE;
}

TBOOLEAN
is_plot_with_palette()
{
    return plot_has_palette;
}

TBOOLEAN
is_plot_with_colorbox()
{
    return plot_wants_colorbox;
}

