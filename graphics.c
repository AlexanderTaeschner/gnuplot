#ifndef lint
static char *RCSid = "$Id: graphics.c,v 1.137 1998/04/14 00:15:32 drd Exp $";
#endif

/* GNUPLOT - graphics.c */

/*[
 * Copyright 1986 - 1993, 1998   Thomas Williams, Colin Kelley
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


#include "plot.h"
#include "setshow.h"

/* key placement is calculated in boundary, so we need file-wide variables
 * To simplify adjustments to the key, we set all these once [depends on
 * key_reverse] and use them throughout.
 */

/*{{{  local and global variables*/
static int key_sample_width;   /* width of line sample */
static int key_sample_left;   /* offset from x for left of line sample */
static int key_sample_right;  /* offset from x for right of line sample */
static int key_point_offset;  /* offset from x for point sample */
static int key_text_left;     /* offset from x for left-justified text */
static int key_text_right;    /* offset from x for right-justified text */
static int key_size_left;     /* size of left bit of key (text or sample, depends on key_reverse) */
static int key_size_right;    /* size of right part of key (including padding) */

/* I think the following should also be static ?? */

static int key_xl, key_xr, key_yt, key_yb;  /* boundarys for key field */
static int max_ptitl_len = 0;	/* max length of plot-titles (keys) */
static int ktitl_lines = 0;	/* no lines in key_title (key header) */
static int ptitl_cnt;	/* count keys with len > 0  */
static int key_cols; /* no cols of keys */
static int key_rows, key_col_wth, yl_ref;


/* penalty for doing tics by callback in gen_tics is need for
 * global variables to communicate with the tic routines
 * Dont need to be arrays for this
 */
static int tic_start, tic_direction, tic_text, rotate_tics, tic_hjust, tic_vjust, tic_mirror;

/* set by tic_callback - how large to draw polar radii */
static double largest_polar_circle;

/* either xformat etc or invented time format
 * index with FIRST_X_AXIS etc
 * global because used in gen_tics, which graph3d also uses
 */
char ticfmt[8][25];
int timelevel[8];
double ticstep[8];

static int xlablin, x2lablin, ylablin, y2lablin, titlelin, xticlin, x2ticlin;
static unsigned int x2ticdelta; /* 0 for tic_in or no x2tics, ticscale*v_tic otherwise */
static unsigned int xticdelta;

static int key_entry_height;  /* bigger of t->v_size, pointsize*t->v_tick */
static int p_width, p_height;  /* pointsize * { t->h_tic | t->v_tic } */


/* there are several things on right of plot - key, y2tics and y2label
 * when working out boundary, save posn of y2label for later...
 * Same goes for x2label.
 * key posn is also stored in key_xl, and tics go at xright
 */
static int ylabel_x, y2label_x, xlabel_y, x2label_y, title_y;
/*}}}*/

/*{{{  static fns and local macros*/
static void plot_impulses __PROTO((struct curve_points *plot, int yaxis_x, int xaxis_y));
static void plot_lines __PROTO((struct curve_points *plot));
static void plot_points __PROTO((struct curve_points *plot));
static void plot_dots __PROTO((struct curve_points *plot));
static void plot_bars __PROTO((struct curve_points *plot));
static void plot_boxes __PROTO((struct curve_points *plot, int xaxis_y));
static void plot_vectors __PROTO((struct curve_points *plot));
static void plot_f_bars __PROTO((struct curve_points *plot));
static void plot_c_bars __PROTO((struct curve_points *plot));

static void edge_intersect __PROTO((struct coordinate GPHUGE *points, int i, double *ex, double *ey));
static int two_edge_intersect __PROTO((struct coordinate GPHUGE *points, int i, double *lx, double *ly));
static TBOOLEAN two_edge_intersect_steps __PROTO((struct coordinate GPHUGE *points, int i, double *lx, double *ly));

static void plot_steps __PROTO((struct curve_points *plot));	/* JG */
static void plot_fsteps __PROTO((struct curve_points *plot));	/* HOE */
static void plot_histeps __PROTO((struct curve_points *plot));	/* CAC */
static void histeps_horizontal __PROTO((int *xl, int *yl, double x1, double x2, double y));	/* CAC */
static void histeps_vertical  __PROTO((int *xl, int *yl, double x, double y1, double y2));	/* CAC */
static void edge_intersect_steps __PROTO((struct coordinate GPHUGE *points, int i, double *ex, double *ey));    	/* JG */
static void edge_intersect_fsteps __PROTO((struct coordinate GPHUGE *points, int i, double *ex, double *ey));     	/* HOE */
static TBOOLEAN two_edge_intersect_steps __PROTO((struct coordinate GPHUGE *points, int i, double *lx, double *ly));	/* JG */
static TBOOLEAN two_edge_intersect_fsteps __PROTO((struct coordinate GPHUGE *points, int i, double *lx, double *ly));

static double LogScale __PROTO((double coord, int is_log, double log_base_log, char *what, char *axis));
static double dbl_raise __PROTO((double x, int y));
static void boundary __PROTO((int scaling, struct curve_points *plots, int count));
static double make_tics __PROTO((int axis, int guide));
static int widest_tic;  /* widest2d_callback keeps longest so far in here */
static void widest2d_callback __PROTO((int axis, double place, char *text, struct lp_style_type grid));
static void ytick2d_callback __PROTO((int axis, double place, char *text, struct lp_style_type grid));
static void xtick2d_callback __PROTO((int axis, double place, char *text, struct lp_style_type grid));
static void map_position __PROTO((struct position *pos, unsigned int *x, unsigned int *y, char *what));
static void mant_exp __PROTO((double log_base, double x, int scientific, double *m, int *p));
static void gprintf __PROTO((char *dest, char *format, double log_base, double x));

#if defined(sun386) || defined(AMIGA_SC_6_1)
static double CheckLog __PROTO((TBOOLEAN is_log, double base_log, double x));
#endif

/* for plotting error bars */
#define ERRORBARTIC (t->h_tic/2) /* half the width of error bar tic mark */

/*
 * The Amiga SAS/C 6.2 compiler moans about macro envocations causing
 * multiple calls to functions. I converted these macros to inline
 * functions coping with the problem without loosing speed.
 * If your compiler supports __inline, you should add it to the
 * #ifdef directive
 * (MGR, 1993)
 */

#ifdef AMIGA_SC_6_1
GP_INLINE static TBOOLEAN i_inrange(int z,int min,int max)
{
  return((min<max) ? ((z>=min)&&(z<=max)) : ((z>=max)&&(z<=min)));
}

GP_INLINE static double f_max(double a,double b)
{
  return(GPMAX(a,b));
}

GP_INLINE static double f_min(double a,double b)
{
  return(GPMIN(a,b));
}

#else
#define f_max(a,b) GPMAX((a),(b))
#define f_min(a,b) GPMIN((a),(b))
#define i_inrange(z,a,b) inrange((z),(a),(b))
#endif

#define inrange(z,min,max) ((min<max) ? ((z>=min)&&(z<=max)) : ((z>=max)&&(z<=min)) )

/* True if a and b have the same sign or zero (positive or negative) */
#define samesign(a,b) ((a) * (b) >= 0)
/*}}}*/

/*{{{  more variables*/
/* Define the boundary of the plot
 * These are computed at each call to do_plot, and are constant over
 * the period of one do_plot. They actually only change when the term
 * type changes and when the 'set size' factors change.
 * - no longer true, for 'set key out' or 'set key under'. also depend
 * on tic marks and multi-line labels.
 * They are shared with graph3d.c since we want to use its draw_clip_line()
 */
int xleft, xright, ybot, ytop;


/* we make a local copy of the 'key' variable so that if something
 * goes wrong, we can switch it off temporarily
 */

static int lkey;

/* First attempt at double axes...
 * x_min etc are now accessed from a global array min_array[], max_array[]
 * put the scale factors into a similar array
 * for convenience in this first attack on double axes, just define x_min etc
 * since code already uses x_min, etc  Eventually it will be done properly
 */


extern double min_array[], max_array[];
extern int auto_array[];

extern int log_array[];
extern double base_array[], log_base_array[];

static int x_axis=FIRST_X_AXIS, y_axis=FIRST_Y_AXIS;  /* current axes */

static double scale[AXIS_ARRAY_SIZE];  /* scale factors for mapping for each axis */

/* BODGES BEFORE I FIX IT UP */
#define x_min min_array[x_axis]
#define x_max max_array[x_axis]
#define y_min min_array[y_axis]
#define y_max max_array[y_axis]

/* And the functions to map from user to terminal coordinates */
#define map_x(x) (int)(xleft+(x-min_array[x_axis])*scale[x_axis]+0.5) /* maps floating point x to screen */ 
#define map_y(y) (int)(ybot +(y-min_array[y_axis])*scale[y_axis]+0.5)	/* same for y */

/* (DFK) Watch for cancellation error near zero on axes labels */
#define SIGNIF (0.01)		/* less than one hundredth of a tic mark */
#define CheckZero(x,tic) (fabs(x) < ((tic) * SIGNIF) ? 0.0 : (x))
#define NearlyEqual(x,y,tic) (fabs((x)-(y)) < ((tic) * SIGNIF))
/*}}}*/

/*{{{  CheckLog()*/
/* (DFK) For some reason, the Sun386i compiler screws up with the CheckLog 
 * macro, so I write it as a function on that machine.
 *
 * Amiga SAS/C 6.2 thinks it will do too much work calling functions in
 * macro arguments twice, thus I inline theese functions. (MGR, 1993)
 * If your compiler doesn't handle those macros correctly, you should
 * also subscribe here. Even without inlining you gain speed with log plots
 */
#if defined(sun386) || defined(AMIGA_SC_6_1)
GP_INLINE static double
CheckLog(is_log, base_log, x)
     TBOOLEAN is_log;
     double base_log;
     double x;
{
  if (is_log)
    return(pow(base_log, x));
  else
    return(x);
}
#else
/* (DFK) Use 10^x if logscale is in effect, else x */
#define CheckLog(is_log, base_log, x) ((is_log) ? pow(base_log, (x)) : (x))
#endif /* sun386 || SAS/C */
/*}}}*/

/*{{{  LogScale()*/
static double
LogScale(coord, is_log, log_base_log, what, axis)
	double coord;			/* the value */
	TBOOLEAN is_log;			/* is this axis in logscale? */
        double log_base_log;		/* if so, the log of its base */
	char *what;			/* what is the coord for? */
	char *axis;			/* which axis is this for ("x" or "y")? */
{
    if (is_log) {
	   if (coord <= 0.0) {
		  char errbuf[100];		/* place to write error message */
		(void) sprintf(errbuf,"%s has %s coord of %g; must be above 0 for log scale!",
				what, axis, coord);
		  graph_error(errbuf);
	   } else
		return(log(coord)/log_base_log);
    }
    return(coord);
}
/*}}}*/

/*{{{  graph_error()*/
/* handle errors during graph-plot in a consistent way */
void graph_error(text)
char *text;
{
	multiplot = FALSE;
	term_end_plot();
	int_error(text, NO_CARET);
}
/*}}}*/

/*{{{  widest2d_callback()*/
/* we determine widest tick label by getting gen_ticks to call this
 * routine with every label
 */

static void widest2d_callback(axis, place, text, grid)
int axis;
double place;
char *text;
struct lp_style_type grid;
{
	int len=label_width(text, NULL);
	if (len>widest_tic)
		widest_tic = len;
}
/*}}}*/


/*{{{  boundary()*/
/* borders of plotting area
 * computed once on every call to do_plot
 *
 * The order in which things is done is getting pretty critical:
 *  ytop depends on title, x2label, ylabels (if no rotated text)
 *  ybot depends on key, if TUNDER
 *  once we have these, we can setup the y1 and y2 tics and the 
 *  only then can we calculate xleft and xright
 *  xright depends also on key TRIGHT
 *  then we can do x and x2 tics
 *
 * For set size ratio ..., everything depends on everything else...
 * not really a lot we can do about that, so we lose if the plot has to
 * be reduced vertically. But the chances are the
 * change will not be very big, so the number of tics will not
 * change dramatically.
 */
 
static void boundary(scaling,plots,count)
	TBOOLEAN scaling;	/* TRUE if terminal is doing the scaling */
	struct curve_points *plots;
	int count;
{
    int ytlen;
    int xtic_textheight, x2tic_textheight;
    int top_margin;
    register struct termentry *t = term;
    int key_h, key_w;
    int can_rotate=(*t->text_angle)(1);

    int timelabel_horizontal = !can_rotate || !timelabel_rotate;

    int yticlin=0, y2ticlin=0, timelin=0;

    lkey = key; /* but we may have to disable it later */

   xticlin = ylablin = y2lablin = xlablin = x2lablin = titlelin = top_margin = 0;

   /*{{{  count lines in labels and tics*/
   if (*title.text) label_width(title.text, &titlelin);
   if (*xlabel.text) label_width(xlabel.text, &xlablin);
   if (*x2label.text) label_width(x2label.text, &x2lablin);
   if (*ylabel.text) label_width(ylabel.text, &ylablin);
   if (*y2label.text) label_width(y2label.text, &y2lablin);
   if (xtics) label_width(xformat, &xticlin);
   if (x2tics) label_width(x2format, &x2ticlin);
   if (ytics) label_width(yformat, &yticlin);
   if (y2tics) label_width(y2format, &y2ticlin);
   if (*timelabel.text) label_width(timelabel.text, &timelin);
   /*}}}*/

	/*{{{  ytop*/
		
	top_margin = t->v_char;
        x2tic_textheight = x2ticdelta = 0;
        if(titlelin) top_margin += (titlelin + 0.5)*(t->v_char);
        if(x2lablin) top_margin += (x2lablin + 0.5)*(t->v_char);
	
        if (*ylabel.text && !can_rotate && (ylablin+1.5)*(t->v_char)>top_margin)
		top_margin = (ylablin+1.5)*(t->v_char);
		
	if (*y2label.text && !can_rotate && (y2lablin+1.5)*(t->v_char)>top_margin)
		top_margin = (y2lablin+1.5)*(t->v_char);
 
        if (x2tics&TICS_ON_BORDER) {
	        /* ought to consider tics on axes if axis near border */
                if(can_rotate && rotate_x2tics) {
                        /* guess at tic length, since we don't know it yet */
                        x2tic_textheight = (t->h_char)*5;
                } else
                        x2tic_textheight = (t->v_char)*(int)(x2ticlin+0.5);
        }
	
	if (tmargin < 0 && !tic_in && (x2tics&TICS_ON_BORDER || xtics&TICS_MIRROR))
		x2ticdelta = (int)((t->v_tic)*ticscale);
 
        top_margin += x2ticdelta + x2tic_textheight;
 
	if (!timelabel_bottom && timelabel_horizontal && *timelabel.text)
		if(top_margin < (int)(t->v_char)*(timelin+1.5))
		        top_margin = (int)(t->v_char)*(timelin+1.5);
		
	if (!tic_in && (xtics&TICS_ON_BORDER || x2tics&TICS_MIRROR)) 
		; /* what's this - was there meant to be an action here ? */
	/* I assume it was supposed to adjust the margin slightly */

	if (tmargin >= 0)
		top_margin = (t->v_char)*tmargin;
 
	ytop = (int)((ysize+yoffset) * (t->ymax)) - top_margin;
	/*}}}*/

	/*{{{  tentative xleft, needed for TUNDER*/
	xleft = (int)(xoffset * (t->xmax) + (lmargin >= 0 ? (t->h_char)*lmargin : t->h_char));
	/*}}}*/
	
	/*{{{  tentative xright, needed for TUNDER*/

	/* note that this is a working xright : if rmargin is
	 * set, we will reconsider xright later
	 */
	xright = (int)((xsize+xoffset) * (t->xmax) - (t->h_char));
	/*}}}*/
	
	/*{{{  ybot*/
 
        xtic_textheight = xticdelta = 0;
	ybot = (int)(t->ymax * yoffset) + t->v_char;
 
        if (xtics&TICS_ON_BORDER) {
	        /* ought to consider tics on axes if axis near border */
                if(can_rotate && rotate_xtics) {
                        /* guess at tic length, since we don't know it yet */
                        xtic_textheight = (t->h_char)*5;
                } else
                        xtic_textheight = (int)((t->v_char)*(xticlin+0.5));
        }
	
	if (*xlabel.text || (timelabel_bottom && timelabel_horizontal && *timelabel.text) )
		ybot += (int)(t->v_char)*(xlablin+0.5);
		
	if (timelabel_bottom && timelabel_horizontal && *timelabel.text)
		ybot += (int)(t->v_char)*((timelin > xlablin) ? timelin - xlablin : 0);
		
	if (!tic_in && (xtics&TICS_ON_BORDER || x2tics&TICS_MIRROR)) 
		xticdelta = (int)((t->v_tic)*ticscale);
 
        ybot += xtic_textheight + xticdelta;
	
	if (bmargin >= 0)
	        ybot = (int)(t->ymax * yoffset + bmargin * (t->v_char));
 
	/*}}}*/

#define KEY_PANIC(x) if (x) { lkey=0; goto key_escape; }

	if (lkey) {
		/*{{{  essential key features*/
		p_width  = pointsize * t->h_tic;
		p_height = pointsize * t->v_tic;
		
		if (key_swidth >= 0 )
			key_sample_width= key_swidth*(t->h_char) + p_width;
		else
			key_sample_width= 0;
  
		key_entry_height = p_height * 1.25 * key_vert_factor;
		if (key_entry_height < (t->v_char) )
			key_entry_height = (t->v_char)*key_vert_factor;
		
		/* count max_len key and number keys with len > 0 */
		max_ptitl_len = find_maxl_keys(plots,count,&ptitl_cnt);
		if ( (ytlen = label_width(key_title,&ktitl_lines)) > max_ptitl_len ) 
			max_ptitl_len = ytlen;
		
		if (key_reverse) {
			key_sample_left= -key_sample_width;
			key_sample_right= 0;
			/* if key width is being used, adjust right-justified text */
			key_text_left=t->h_char;
			key_text_right=(t->h_char)*(max_ptitl_len+1+key_width_fix);
			key_size_left=t->h_char - key_sample_left;  /* sample left is -ve */
			key_size_right=key_text_right;
		} else {
			key_sample_left= 0;
  			key_sample_right= key_sample_width;
			/* if key width is being used, adjust left-justified text */
			key_text_left = -(int)((t->h_char)*(max_ptitl_len+1+key_width_fix));
			key_text_right = -(int)(t->h_char);
			key_size_left= - key_text_left;
			key_size_right=key_sample_right + t->h_char;
		}
		key_point_offset = (key_sample_left + key_sample_right)/2;
		
		/* advance width for cols */
		key_col_wth = key_size_left + key_size_right;
		
		key_rows = ptitl_cnt;
		key_cols = 1;
		
		/* calculate rows and cols for key - if something goes wrong,
		 * the tidiest way out is to  set lkey=0, and a goto
		 */
		 
		if (lkey == -1) {
			if (key_vpos == TUNDER ) {
				/* maximise no cols, limited by label-length */
				key_cols = (int)(xright - xleft)/key_col_wth;
				KEY_PANIC(key_cols == 0);
				key_rows = (int)(ptitl_cnt+key_cols-1)/key_cols;
				KEY_PANIC(key_rows==0);
				/* now calculate actual no cols depending on no rows */
				key_cols = (int)(ptitl_cnt+key_rows-1)/key_rows;
				KEY_PANIC(key_cols==0);
				key_col_wth = (int)(xright - xleft)/key_cols;
		 		/* we divide into columns, then centre in column by considering ratio
		 		 * of key_left_size to key_right_size
				 * key_size_left/(key_size_left+key_size_right) * (xright-xleft)/key_cols
				 * do one integer division to maximise accuracy (hope we dont overflow !)
				 */
				key_xl = xleft - key_size_left + ((xright-xleft)*key_size_left) / (key_cols*(key_size_left+key_size_right));
				key_xr = key_xl + key_col_wth * (key_cols-1) + key_size_left + key_size_right;
				key_yb = t->ymax * yoffset;
				key_yt = key_yb + key_rows * key_entry_height + ktitl_lines * t->v_char;
				ybot += key_entry_height*key_rows + t->v_char*(ktitl_lines+1);
			} else {
				/* maximise no rows, limited by ytop-ybot */
				int i = (int)(ytop-ybot-(ktitl_lines+1)*(t->v_char))/key_entry_height;
				KEY_PANIC(i==0);
				if (ptitl_cnt > i) {
					key_cols = (int)(ptitl_cnt+i-1)/i;
					/* now calculate actual no rows depending on no cols */
					KEY_PANIC(key_cols==0);
					key_rows = (int)(ptitl_cnt+key_cols-1)/key_cols;
				}
			}
			/* come here if we detect a division by zero in key calculations */
			key_escape:
				; /* ansi requires this */
		}
		/*}}}*/
	}


	/*{{{  set up y and y2 tics*/
	{
		/* setup_tics allows max number of tics to be specified
		 * but users dont like it to change with size and font,
		 * so we use value of 20, which is 3.5 behaviour.
		 * Note also that if format is '', yticlin=0, so this gives
		 * division by zero. 
		 * int guide = (ytop-ybot)/term->v_char;
		 */
		if (ytics) setup_tics(FIRST_Y_AXIS, &yticdef, yformat, 20 /*(int) (guide/yticlin)*/);
		if (y2tics) setup_tics(SECOND_Y_AXIS, &y2ticdef, y2format, 20 /*(int) (guide/y2ticlin)*/);
	}
	/*}}}*/
	
	/*{{{  fix up xleft based on widest ytic, ylabels, etc*/
	if ( (lmargin == -1)) {
		if (ytics&TICS_ON_BORDER) {
                        if (can_rotate && rotate_ytics)
                                xleft += (int)((t->v_char)*(yticlin+1.5));
                        else {
			        widest_tic=0; /* reset the global variable ... */
			        /* get gen_tics to call widest2d_callback with all labels
			         * the latter sets widest_tic to the length of the widest one
			         * ought to consider tics on axis if axis near border...
			         */
			        gen_tics(FIRST_Y_AXIS, &yticdef, 0, 0, 0.0, widest2d_callback);
	
			        xleft += (t->h_char)*(widest_tic+3);
                        }
		}
	
		if (*ylabel.text || (can_rotate && timelabel_rotate && *timelabel.text)) {
			xleft += (t->v_char)*(can_rotate && timelin>ylablin ? timelin : ylablin);
		}
	
			if (!tic_in && (ytics&TICS_ON_BORDER || y2tics&TICS_MIRROR))
				xleft += (t->h_tic)*ticscale;
	}
 
        if(*ylabel.text)
	        ylabel_x=xoffset*(t->xmax);
	/*}}}*/
	
	/*{{{  fix up xright based on widest y2tic. y2labels, key TOUT*/
	if ( lkey == -1 && key_hpos == TOUT ) {
		xright -= key_col_wth*key_cols; /* fix later if rmargin fixed */
		key_xl = xright  + (t->h_tic);
	}
	
	/* fix up xright some more if y2label
	 * also, store y2label posn, since it is not trivial to
	 * work out again
	 */
	
	if ( *y2label.text) {
		y2label_x = xright; /* right or top justified to here */

		if (can_rotate)
			xright -= (t->v_char)*y2lablin;
	}


	if (rmargin >= 0)
	{
		/* recalculate xright properly for fixed margin */
		xright = (int)((xsize+xoffset) * (t->xmax) - (t->h_char)*rmargin);
	}
	else
	{
		/* fix up xright yet more if second tics */
		if (y2tics&TICS_ON_BORDER) {
                        if (can_rotate && rotate_y2tics)
                                xright -= (int)((t->v_char)*(y2ticlin+0.5));
                        else {
			        /* dont use map_x/map_y, so dont need to set x_axis/y_axis */
			        widest_tic=0; /* reset the global variable */
			        gen_tics(SECOND_Y_AXIS, &y2ticdef, 0, 0, 0.0, widest2d_callback);
			        xright -= (widest_tic+1) * (t->h_char);
                        }
		}
	
		if (!tic_in && (y2tics&TICS_ON_BORDER || ytics&TICS_MIRROR))
			xright -= (t->h_tic)*ticscale;
	 }
	
	/*}}}*/

	if (aspect_ratio != 0.0)
	{
		double current_aspect_ratio;

		if (aspect_ratio < 0 && (max_array[x_axis] - min_array[x_axis]) != 0.0)
		{
			current_aspect_ratio = - aspect_ratio * (max_array[y_axis]-min_array[y_axis]) / \
			  (max_array[x_axis]-min_array[x_axis]);
		}
		else
			current_aspect_ratio = aspect_ratio;
		
		/*{{{  set aspect ratio if valid and sensible*/
		if (current_aspect_ratio >= 0.01 && current_aspect_ratio <= 100.0) {
			double current = ( (double) (ytop-ybot)) / ( (double) (xright-xleft) );
			double required= ( current_aspect_ratio * (double) t->v_tic) / ( (double) t->h_tic);
		
			if (current > required) {
				/* too tall */
				ytop = ybot + required * (xright-xleft);
			} else {
		 		int y2 = y2label_x - xright;
				xright = xleft + (ytop-ybot)/required;
				y2label_x = xright + y2;
			}
		}
		/*}}}*/
	}

	/*{{{  set up x and x2 tics*/
	/* we should base the guide on the width of the xtics, but we cannot
	 * use widest_tics until tics are set up. Bit of a downer - let us
	 * assume tics are 5 characters wide
	 */
	
	 {
		/* see equivalent code for ytics above
	 	 * int guide = (xright - xleft) / (5*t->h_char);
		 */
	
		if (xtics) setup_tics(FIRST_X_AXIS, &xticdef, xformat, 20 /*guide*/);
		if (x2tics) setup_tics(SECOND_X_AXIS, &x2ticdef, x2format, 20 /*guide*/);
	 }
	/*}}}*/

	/*{{{  adjust top and bottom margins for rotation */
	/* ...and compute y coordinates for xlabel, x2label, and title     */
	
	if (x2tics&TICS_ON_BORDER && can_rotate && rotate_x2tics) {
		widest_tic=0; /* reset the global variable ... */
		gen_tics(SECOND_X_AXIS, &x2ticdef, 0, 0, 0.0, widest2d_callback);
	               x2tic_textheight += (t->h_char)*(widest_tic-5);
	               if(tmargin < 0)
	                       ytop -= (t->h_char)*(widest_tic-5);
	}
	x2label_y = ytop + x2ticdelta + (int)((t->v_char)*(x2lablin+.5)) + x2tic_textheight;
	
	/* we could just draw the title here too, with JUST_BOT, but since
	 * we know the lines in the ylabel, we take this into account now
	 */
	title_y = x2label_y + titlelin * (t->v_char);
	
	if (bmargin <= 0 && xtics&TICS_ON_BORDER && can_rotate && rotate_xtics) {
		widest_tic=0; /* reset the global variable ... */
		gen_tics(FIRST_X_AXIS, &xticdef, 0, 0, 0.0, widest2d_callback);
	               xtic_textheight += (t->h_char)*(widest_tic-5);
	               ybot += (t->h_char)*(widest_tic-5);
	}
	xlabel_y = ybot - xticdelta - xtic_textheight - (int)(t->v_char);
	/*}}}*/

	/* restore text to horizontal [we tested rotation above] */
	(void)(*t->text_angle)(0);

	/* needed for map_position() below */
	
	scale[FIRST_Y_AXIS] = (ytop - ybot)/(max_array[FIRST_Y_AXIS] - min_array[FIRST_Y_AXIS]);
	scale[FIRST_X_AXIS] = (xright - xleft)/(max_array[FIRST_X_AXIS] - min_array[FIRST_X_AXIS]);
	scale[SECOND_Y_AXIS] = (ytop - ybot)/(max_array[SECOND_Y_AXIS] - min_array[SECOND_Y_AXIS]);
	scale[SECOND_X_AXIS] = (xright - xleft)/(max_array[SECOND_X_AXIS] - min_array[SECOND_X_AXIS]);

   /*{{{  calculate the window in the grid for the key*/
   if (lkey==1 || (lkey==-1 && key_vpos != TUNDER)) {
   	/* calculate space for keys to prevent grid overwrite the keys */
   	/* do it even if there is no grid, as do_plot will use these to position key */
   	key_w = key_col_wth*key_cols;
   	key_h = (ktitl_lines) * t->v_char + key_rows * key_entry_height;
   	if (lkey == -1) {
   		if ( key_vpos == TTOP ) {
   			key_yt = (int) ytop - (t->v_tic);
   			key_yb = key_yt - key_h;
   		} else {
   			key_yb = ybot + (t->v_tic);
   			key_yt = key_yb + key_h;
   		}
   		if ( key_hpos == TLEFT ) {
   			key_xl = xleft + (t->h_char); /* for Left just */
   			key_xr = key_xl + key_w;
   		} else if (key_hpos == TRIGHT) {
   			key_xr = xright - (t->h_char);  /* for Right just */
   			key_xl = key_xr - key_w;
   		} else /* TOUT */ {
   			/* do this here for do_plot() */
   			/* align right first since rmargin may be manual */
   			key_xr = (xsize+xoffset) * (t->xmax) - (t->h_char);
   			key_xl = key_xr - key_w;
   		}
   	} else {
   		unsigned int x,y;
   		map_position(&key_user_pos, &x, &y, "key");
   		key_xl = x - key_size_left;
   		key_xr = key_xl + key_w;
   		key_yt = y + (ktitl_lines ? t->v_char : key_entry_height)/2;
   		key_yb = key_yt - key_h;
   	}	
   }
   /*}}}*/

}
/*}}}*/

/*{{{  dbl_raise()*/
static double dbl_raise(x,y)
double x;
int y;
{
register int i=abs(y);
double val=1.0;

	while (--i >= 0)
		val *= x;

	if (y < 0 ) return (1.0/val);
	return(val);
}
/*}}}*/

/*{{{  timetic_fmt()*/
void
timetic_format(axis,amin,amax)
int axis;
double amin, amax;
{
	struct tm tmin, tmax;

	*ticfmt[axis]=0;  /* make sure we strcat to empty string */

	ggmtime(&tmin,(double)time_tic_just(timelevel[axis],amin));
	ggmtime(&tmax,(double)time_tic_just(timelevel[axis],amax));
	/*
	if ( tmax.tm_year == tmin.tm_year && 
		tmax.tm_mon == tmin.tm_mon && 
		tmax.tm_mday == tmin.tm_mday ) {
	*/
	if ( tmax.tm_year == tmin.tm_year && 
		tmax.tm_yday == tmin.tm_yday ) {
		/* same day, skip date */
		if ( tmax.tm_hour != tmin.tm_hour ) {
			strcpy(ticfmt[axis],"%H");
		}
		if ( timelevel[axis] < 3 ) {
			if (strlen(ticfmt[axis]))
				strcat(ticfmt[axis],":");
			strcat(ticfmt[axis],"%M");
		}
		if ( timelevel[axis] < 2 ) {
			strcat(ticfmt[axis],":%S");
		}
	} else {
		if ( tmax.tm_year != tmin.tm_year ) {
			/* different years, include year in ticlabel */
			/* check convention, day/month or month/day */
			if (strchr(timefmt,'m') < strchr(timefmt,'d')) {
				strcpy(ticfmt[axis],"%m/%d/%");
			} else {
				strcpy(ticfmt[axis],"%d/%m/%");
			}
			if ( ((int)(tmax.tm_year/100)) != ((int)(tmin.tm_year/100)) ) {
				strcat(ticfmt[axis],"Y");
			} else {
				strcat(ticfmt[axis],"y");
			}
						 
		} else {
			if (strchr(timefmt,'m') < strchr(timefmt,'d')) {
				strcpy(ticfmt[axis],"%m/%d");
			} else {
				strcpy(ticfmt[axis],"%d/%m");
			}
		}
		if ( timelevel[axis] < 4 ) {
			strcat(ticfmt[axis],"\n%H:%M");
		}
	}
}
/*}}}*/

/*{{{  set_tic()*/
/* the guide parameter was intended to allow the number of tics
 * to depend on the relative sizes of the plot and the font.
 * It is the approximate upper limit on number of tics allowed.
 * But it did not go down well with the users.
 * A value of 20 gives the same behaviour as 3.5, so that is
 * hardwired into the calls to here. Maybe we will restore it
 * to the automatic calculation one day
 */

double
set_tic(l10,guide)
double l10;
int guide;
{
	double xnorm, tics, posns;

	int fl=(int)floor(l10);
	xnorm = pow(10.0,l10-fl); /* approx number of decades */

	posns = guide / xnorm;  /* approx number of tic posns per decade */
	
	if (posns > 40)
		tics = 0.05;  /* eg 0, .05, .10, ... */
	else if (posns > 20)
		tics = 0.1;  /* eg 0, .1, .2, ... */
	else if (posns > 10)
		tics = 0.2;  /* eg 0,0.2,0.4,... */
	else if (posns > 4)
		tics = 0.5; /* 0,0.5,1, */
	else if (posns > 1)
		tics = 1; /* 0,1,2,.... */
	else if (posns > 0.5)
		tics = 2; /* 0, 2, 4, 6 */
	else
		/* getting desperate... the ceil is to make sure we
		 * go over rather than under - eg plot [-10:10] x*x
		 * gives a range of about 99.999 - tics=xnorm gives
		 * tics at 0, 99.99 and 109.98  - BAD !
		 * This way, inaccuracy the other way will round
		 * up (eg 0->100.0001 => tics at 0 and 101
		 * I think latter is better than former
		 */
		tics = ceil(xnorm);

	return (tics * dbl_raise(10.0,fl));
}
/*}}}*/

/*{{{  make_tics()*/
static double make_tics(axis, guide)
int axis,guide;
{
	register double xr,tic,l10;

	xr = fabs(min_array[axis]-max_array[axis]);

	l10 = log10(xr);
	tic = set_tic(l10, guide);
	if (log_array[axis] && tic<1.0)
		tic=1.0;
	if (datatype[axis] == TIME) {
		struct tm ftm, etm;
		/* this is not fun */
		ggmtime(&ftm,(double)min_array[axis]);
		ggmtime(&etm,(double)max_array[axis]);
	
		timelevel[axis] = 0; /* seconds */
		if ( tic > 20 ) {
			/* turn tic into units of minutes */
			tic = set_tic(log10(xr/60.0),guide)*60;
			timelevel[axis] = 1; /* minutes */
		}
		if ( tic > 20*60 ) {
			/* turn tic into units of hours */
			tic = set_tic(log10(xr/3600.0),guide)*3600;
			timelevel[axis] = 2; /* hours */
		}
		if ( tic > 2*3600 ) {
			/* need some tickling */
			tic = set_tic(log10(xr/(3*3600.0)),guide)*3*3600;
		}
		if ( tic > 6*3600 ) {
			/* turn tic into units of days */
			tic = set_tic(log10(xr/DAY_SEC),guide)*DAY_SEC;
			timelevel[axis] = 3; /* days */
		}
		if ( tic > 3*DAY_SEC ) {
			/* turn tic into units of weeks */
			tic = set_tic(log10(xr/WEEK_SEC),guide)*WEEK_SEC;
			if ( tic < WEEK_SEC ) { /* force */
				tic = WEEK_SEC;
			}
			timelevel[axis] = 4; /* weeks */
		}
		if ( tic > 3*WEEK_SEC ) {
			/* turn tic into units of month */
			tic = set_tic(log10(xr/MON_SEC),guide)*MON_SEC;
			if ( tic < MON_SEC ) { /* force */
				tic = MON_SEC;
			}
			timelevel[axis] = 5; /* month */
		}
		if ( tic > 2*MON_SEC ) {
			/* turn tic into units of month */
			tic = set_tic(log10(xr/(3*MON_SEC)),guide)*3*MON_SEC;
		}
		if ( tic > 6*MON_SEC ) {
			/* turn tic into units of years */
			tic = set_tic(log10(xr/YEAR_SEC),guide)*YEAR_SEC;
			if ( tic < (YEAR_SEC/2) ) {
				tic = YEAR_SEC / 2;
			}
			timelevel[axis] = 6; /* year */
		}
	}
	return(tic);
}
/*}}}*/


void do_plot(plots, pcount)
struct curve_points *plots;
int pcount;			/* count of plots in linked list */
{

/* BODGES BEFORE I FIX IT UP */
#define ytic ticstep[y_axis]
#define xtic ticstep[x_axis]

register struct termentry *t = term;
register int curve;
int axis_zero[AXIS_ARRAY_SIZE]; /* axes in terminal coords for FIRST_X_AXIS, etc */
register struct curve_points *this_plot = NULL;
register int xl=0, yl=0; /* avoid gcc -Wall warning */
register int key_count=0;
			/* only a Pyramid would have this many registers! */
struct text_label *this_label;
struct arrow_def *this_arrow;
TBOOLEAN scaling;
char ss[MAX_LINE_LEN+1], *s, *e;

    /* so that macros for x_min etc pick up correct values
     * until this is done properly
     */
     
    x_axis=FIRST_X_AXIS; y_axis=FIRST_Y_AXIS;

/*	Apply the desired viewport offsets. */
     if (y_min < y_max) {
	    y_min -= boff;
	    y_max += toff;
	} else {
	    y_max -= boff;
	    y_min += toff;
	}
     if (x_min < x_max) {
	    x_min -= loff;
	    x_max += roff;
	} else {
	    x_max -= loff;
	    x_min += roff;
	}

/*	This used be x_max == x_min, but that caused an infinite loop once. */
	if (fabs(x_max - x_min) < zero)
		int_error("x_min should not equal x_max!",NO_CARET);
	if (fabs(y_max - y_min) < zero)
		int_error("y_min should not equal y_max!",NO_CARET);

	term_init(); /* may set xmax/ymax */
		
	 /* compute boundary for plot (xleft, xright, ytop, ybot)
	  * also calculates tics, since xtics depend on xleft
	  * but xleft depends on ytics. Boundary calculations
	  * depend on term->v_char etc, so terminal must be
	  * initialised.
     */

	scaling = (*t->scale)(xsize, ysize);
 
	boundary(scaling,plots,pcount);

	screen_ok = FALSE;

	term_start_plot();
		
/* DRAW TICS AND GRID */
        term_apply_lp_properties(&border_lp); /* border linetype */
	largest_polar_circle=0;

	/* select first mapping */
	x_axis=FIRST_X_AXIS; y_axis=FIRST_Y_AXIS;

	/* label first y axis tics */
	if (ytics) {
		int axis=map_x(ZERO);
		/* set the globals ytick2d_callback() needs */
                rotate_tics=rotate_ytics && (*t->text_angle)(1);

                if(rotate_tics) {
                        tic_hjust=CENTRE;
                        tic_vjust=JUST_BOT;
                } else {
                        tic_hjust=RIGHT;
                        tic_vjust=JUST_CENTRE;
                }

		if (ytics&TICS_MIRROR)
			tic_mirror=xright;
		else
			tic_mirror= -1; /* no thank you */

		if ( (ytics&TICS_ON_AXIS) && !log_array[FIRST_X_AXIS] && inrange(axis, xleft, xright)) {
			tic_start = axis;
			tic_direction=-1;
			/* put text at boundary if axis is close to boundary */
			tic_text = ( ( (tic_start-xleft) > (3*t->h_char) ) ? tic_start : xleft) - t->h_char;
		} else {
			tic_start=xleft;
			tic_direction=tic_in ? 1 : -1;
                        if(rotate_tics)
			        tic_text=xleft-(t->v_char);
                        else
			        tic_text=xleft-(t->h_char);
			if (!tic_in) tic_text -= ticscale*(t->h_tic);
		}
		/* go for it */
		gen_tics(FIRST_Y_AXIS, &yticdef,
		   work_grid.l_type&(GRID_Y|GRID_MY),
		   mytics, mytfreq, ytick2d_callback);
                (*t->text_angle)(0);    /* reset rotation angle */

	}

	/* label first x axis tics */
	if (xtics) {
		int axis=map_y(ZERO);
		/* set the globals xtick2d_callback() needs */
                rotate_tics=rotate_xtics && (*t->text_angle)(1);
                if(rotate_tics) {
                        tic_hjust=RIGHT;
                        tic_vjust=JUST_CENTRE;
                } else {
                        tic_hjust=CENTRE;
                        tic_vjust=JUST_TOP;
                }

		if (xtics&TICS_MIRROR)
			tic_mirror=ytop;
		else
			tic_mirror= -1; /* no thank you */
		if ( (xtics&TICS_ON_AXIS) && !log_array[FIRST_Y_AXIS] && inrange(axis, ybot, ytop)) {
			tic_start=axis;
			tic_direction=-1;
			/* put text at boundary if axis is close to boundary */
			if (tic_start - ybot > 2*t->v_char)
				tic_text = tic_start - ticscale*t->v_tic - t->v_char;
			else
				tic_text =  ybot - t->v_char;
		} else {
			tic_start=ybot;
			tic_direction=tic_in ? 1 : -1;
                        if(rotate_tics)
			        tic_text=ybot-(t->h_char);
                        else
			        tic_text=ybot-(t->v_char);
			if (!tic_in)
				tic_text -= ticscale*(t->v_tic);
		}
		/* go for it */
		gen_tics(FIRST_X_AXIS, &xticdef,
		   work_grid.l_type&(GRID_X|GRID_MX),
		   mxtics, mxtfreq, xtick2d_callback);
                (*t->text_angle)(0);    /* reset rotation angle */
	}

	/* select second mapping */
	x_axis=SECOND_X_AXIS; y_axis=SECOND_Y_AXIS;

	/* label second y axis tics */
	if (y2tics) {
		/* set the globalss ytick2d_callback() needs */
		int axis=map_x(ZERO);
                rotate_tics=rotate_y2tics && (*t->text_angle)(1);
                if(rotate_tics) {
                        tic_hjust=CENTRE;
                        tic_vjust=JUST_TOP;
                } else {
                        tic_hjust=LEFT;
                        tic_vjust=JUST_CENTRE;
                }

		if (y2tics&TICS_MIRROR)
			tic_mirror=xleft;
		else
			tic_mirror= -1; /* no thank you */
		if ((y2tics&TICS_ON_AXIS) && !log_array[FIRST_X_AXIS] && inrange(axis,xleft,xright)) {
			tic_start = axis;
			tic_direction=1;
			/* put text at boundary if axis is close to boundary */
			tic_text = ( ( (xright-tic_start) > (3*t->h_char) ) ? tic_start : xright) + t->h_char;
		} else {
			tic_start=xright;
			tic_direction=tic_in ? -1 : 1;
                        if(rotate_tics)
			        tic_text=xright+(t->v_char);
                        else
			        tic_text=xright+(t->h_char);
			if (!tic_in)
				tic_text += ticscale*(t->h_tic);
		}
		/* go for it */
		gen_tics(SECOND_Y_AXIS, &y2ticdef,
		  work_grid.l_type&(GRID_Y2|GRID_MY2),
		  my2tics, my2tfreq, ytick2d_callback);
                (*t->text_angle)(0);    /* reset rotation angle */
	}

	/* label second x axis tics */
	if (x2tics) {
		int axis=map_y(ZERO);
		/* set the globals xtick2d_callback() needs */
                rotate_tics=rotate_x2tics && (*t->text_angle)(1);
                if(rotate_tics) {
                        tic_hjust=LEFT;
                        tic_vjust=JUST_CENTRE;
                } else {
                        tic_hjust=CENTRE;
                        tic_vjust=JUST_BOT;
                }

		if (x2tics&TICS_MIRROR)
			tic_mirror=ybot;
		else
			tic_mirror= -1; /* no thank you */
		if ( (x2tics&TICS_ON_AXIS) && !log_array[SECOND_Y_AXIS] && inrange(axis,ybot,ytop)) {
			tic_start=axis;
			tic_direction=1;
			/* put text at boundary if axis is close to boundary */
			tic_text = ( ( (ytop - tic_start ) > (2*t->v_char) ) ? tic_start : ytop) + t->v_char;
		} else {
			tic_start=ytop;
			tic_direction=tic_in ? -1 : 1;
			tic_text=ytop + x2ticdelta;
                        if(rotate_tics)
			        tic_text += t->h_char;
                        else
			        tic_text += t->v_char;
		}
		/* go for it */
		gen_tics(SECOND_X_AXIS, &x2ticdef,
		  work_grid.l_type&(GRID_X2|GRID_MX2),
		  mx2tics, mx2tfreq, xtick2d_callback);
                (*t->text_angle)(0);    /* reset rotation angle */
	}

	/* select first mapping */
	x_axis=FIRST_X_AXIS; y_axis=FIRST_Y_AXIS;

/* RADIAL LINES FOR POLAR GRID */

	/* note that draw_clip_line takes unsigneds, but (fortunately)
	 * clip_line takes signeds
	 */
	if (polar_grid_angle)
	{
		double theta=0;
		int ox=map_x(0);
		int oy=map_y(0);
		term_apply_lp_properties(&grid_lp);
		for (theta=0; theta<6.29; theta += polar_grid_angle)
		{
			/* copy ox in case it gets moved (but it shouldn't) */
			int oox = ox;
			int ooy = oy;
			int x = map_x(largest_polar_circle*cos(theta));
			int y = map_y(largest_polar_circle*sin(theta));
			if (clip_line(&oox,&ooy,&x,&y))
			{
				(*t->move)( (unsigned int) oox, (unsigned int) ooy);
				(*t->vector)( (unsigned int) x, (unsigned int) y);
			}
		}
			draw_clip_line(ox,oy,
			  map_x(largest_polar_circle*cos(theta)),
			  map_y(largest_polar_circle*sin(theta)));
	}


/* DRAW AXES */

   /* after grid so that axes linetypes are on top */

	x_axis=FIRST_X_AXIS; y_axis=FIRST_Y_AXIS; /* chose scaling */
	axis_zero[FIRST_X_AXIS] = map_y(0.0);
	axis_zero[FIRST_Y_AXIS] = map_x(0.0); 

	if (axis_zero[FIRST_X_AXIS] < ybot || is_log_y)
		axis_zero[FIRST_X_AXIS] = ybot;				/* save for impulse plotting */
	else if (axis_zero[FIRST_X_AXIS] >= ytop)
		axis_zero[FIRST_X_AXIS] = ytop ;
	else if (xzeroaxis.l_type > -3) {
		term_apply_lp_properties(&xzeroaxis);
		(*t->move)(xleft,axis_zero[FIRST_X_AXIS]);
		(*t->vector)(xright,axis_zero[FIRST_X_AXIS]);
	}

	if ((yzeroaxis.l_type>-3) && !is_log_x && axis_zero[FIRST_Y_AXIS] >= xleft && axis_zero[FIRST_Y_AXIS] < xright ) {
		term_apply_lp_properties(&yzeroaxis);
		(*t->move)(axis_zero[FIRST_Y_AXIS],ybot);
		(*t->vector)(axis_zero[FIRST_Y_AXIS],ytop);
	}

	x_axis=SECOND_X_AXIS; y_axis=SECOND_Y_AXIS; /* chose scaling */
	axis_zero[SECOND_X_AXIS] = map_y(0.0);
	axis_zero[SECOND_Y_AXIS] = map_x(0.0); 

	if (axis_zero[SECOND_X_AXIS] < ybot || is_log_y2)
		axis_zero[SECOND_X_AXIS] = ybot;				/* save for impulse plotting */
	else if (axis_zero[SECOND_X_AXIS] >= ytop)
		axis_zero[SECOND_X_AXIS] = ytop ;
	else if (x2zeroaxis.l_type>-3) {
		term_apply_lp_properties(&x2zeroaxis);
		(*t->move)(xleft,axis_zero[SECOND_X_AXIS]);
		(*t->vector)(xright,axis_zero[SECOND_X_AXIS]);
	}

	if ((y2zeroaxis.l_type>-3) && !is_log_x2 && axis_zero[SECOND_Y_AXIS] >= xleft && axis_zero[SECOND_Y_AXIS] < xright ) {
		term_apply_lp_properties(&y2zeroaxis);
		(*t->move)(axis_zero[SECOND_Y_AXIS],ybot);
		(*t->vector)(axis_zero[SECOND_Y_AXIS],ytop);
	}


/* DRAW PLOT BORDER */
	term_apply_lp_properties(&border_lp); /* border linetype */
	if (draw_border) {
		(*t->move)(xleft,ybot);
		if(border_south){
			(*t->vector)(xright,ybot);
		} else {
			(*t->move)(xright,ybot);
		}
		if(border_east){
			(*t->vector)(xright,ytop);
		} else {
			(*t->move)(xright,ytop);
		}
		if(border_north){ 
			(*t->vector)(xleft,ytop);
		} else {
			(*t->move)(xleft,ytop);
		}
		if(border_west){ 
			(*t->vector)(xleft,ybot);
		} else {
			(*t->move)(xleft,ybot);
		}
	}

/* YLABEL */
    if (*ylabel.text) {
	strcpy(ss, ylabel.text);
	if ((*t->text_angle)(1)) {
		unsigned int x=ylabel_x+(ylabel.xoffset+1)*(t->v_char);
		unsigned int y=(ytop+ybot)/2 + ylabel.yoffset*(t->h_char);
		write_multiline(x,y,ss,CENTRE, JUST_TOP, 1, ylabel.font);
		(*t->text_angle)(0);
	} else {
		/* really bottom just, but we know number of lines */
		unsigned int x=(1+ylabel.xoffset)*(t->h_char) + xoffset*(t->xmax);
		unsigned int y=ytop+(ylablin+ylabel.yoffset)*(t->v_char);
		write_multiline(x,y,ss,LEFT,JUST_TOP, 0, ylabel.font);
	}
    }

/* Y2LABEL */
    if (*y2label.text) {
	strcpy(ss, y2label.text);
	if ((*t->text_angle)(1)) {
		/* we worked out posn in boundary() */
		unsigned int x=y2label_x + y2label.xoffset*(t->v_char);
		unsigned int y=(ytop+ybot)/2 + y2label.yoffset*(t->h_char);
		write_multiline(x,y,ss,CENTRE, JUST_BOT, 1, y2label.font);
		(*t->text_angle)(0);
	} else {
		/* really bottom just, but we know number of lines */
		unsigned int x= y2label_x + (y2label.xoffset)*(t->h_char);
		unsigned int y=ytop+(y2lablin+y2label.yoffset)*(t->v_char);
		write_multiline(x,y,ss,RIGHT,JUST_TOP, 0, y2label.font);
	}
    }

/* XLABEL */
    if (*xlabel.text) {
	unsigned int x=(xright+xleft)/2 + xlabel.xoffset*(t->h_char);
	unsigned int y=xlabel_y + xlabel.yoffset*(t->v_char);
	strcpy(ss, xlabel.text);
	write_multiline(x,y,ss,CENTRE,JUST_TOP, 0, xlabel.font);
    }
	
/* PLACE TITLE */
    if (*title.text) {
		unsigned int x = (xleft+xright)/2 + title.xoffset * t->h_char;
		unsigned int y = title_y + title.yoffset*(t->v_char);
		strcpy(ss, title.text);
		write_multiline(x,y,ss,CENTRE, JUST_TOP, 0, title.font);
    }

/* X2LABEL */
	if (*x2label.text) {
		unsigned int x=(xright+xleft)/2 + x2label.xoffset*(t->h_char);
		unsigned int y=  x2label_y + x2label.yoffset*(t->v_char);
		strcpy(ss, x2label.text);
		write_multiline(x,y,ss,CENTRE,JUST_TOP, 0, x2label.font);
	}
		

/* PLACE TIMEDATE */
	if (*timelabel.text) {
		char str[MAX_LINE_LEN+1];
		time_t now;
		unsigned int x = (timelabel.xoffset+1)*t->h_char;
		unsigned int y = timelabel.yoffset*(t->v_char) +
                	( timelabel_bottom ? xlabel_y : title_y );
		time(&now);
		strftime(str, MAX_LINE_LEN, timelabel.text, localtime(&now));

		if (timelabel_rotate && (*t->text_angle)(1)) {
                        if(timelabel_bottom)
				write_multiline(x,y,str,LEFT, JUST_TOP, 1, timelabel.font);
                        else
				write_multiline(x,y,str,RIGHT, JUST_TOP, 1, timelabel.font);
			(*t->text_angle)(0);
		} else {
                        if(timelabel_bottom)
				write_multiline(x,y,str,LEFT, JUST_BOT, 0, timelabel.font);
			else
				write_multiline(x,y,str,LEFT, JUST_TOP, 0, timelabel.font);
  		}
  	}

/* PLACE LABELS */
    for (this_label = first_label; this_label!=NULL;
			this_label=this_label->next ) {
		unsigned int x,y;
		map_position(&this_label->place, &x, &y, "label");
		strcpy(ss, this_label->text);
                if(this_label->rotate && (*t->text_angle)(1)) {
		        write_multiline(x, y, ss, this_label->pos, JUST_TOP, 1, this_label->font);
                        (*t->text_angle)(0);
                } else {
		        write_multiline(x, y, ss, this_label->pos, JUST_TOP, 0, this_label->font);
                }
 	 }

/* PLACE ARROWS */
    for (this_arrow = first_arrow; this_arrow!=NULL;
	    this_arrow = this_arrow->next ) {
	    	unsigned int sx,sy,ex,ey;
	    	map_position(&this_arrow->start, &sx, &sy, "arrow");
	    	map_position(&this_arrow->end, &ex, &ey, "arrow");

                term_apply_lp_properties(&(this_arrow->lp_properties));
		   (*t->arrow)(sx, sy, ex, ey, this_arrow->head);
    }

/* WORK OUT KEY SETTINGS AND DO KEY TITLE / BOX */


	if (lkey) { /* may have been cancelled if something went wrong */
		/* just use key_xl etc worked out in boundary() */
		xl = key_xl + key_size_left;
		yl = key_yt;

 		if (*key_title) {
 			sprintf(ss,"%s\n",key_title);
 			s = ss;
 			yl -= t->v_char/2;
 			while( (e=(char *)strchr(s,'\n')) != NULL ) {
 				*e = '\0';
 				if ( key_just == JLEFT) {
 					(*t->justify_text)(LEFT);
 					(*t->put_text)(xl+key_text_left,yl,s);
  				} else {
 					if ((*t->justify_text)(RIGHT)) {
 						(*t->put_text)(xl+key_text_right,yl,s);
 					} else {
 						int x=xl+key_text_right-(t->h_char)*strlen(s);
 						if (key_hpos == TOUT || inrange(x, xleft, xright))
 							(*t->put_text)(x, yl,s);
 					}
 				}
 				s = ++e;
 				yl -= t->v_char;
 			}
 			yl += t->v_char/2;
 		}
 		yl_ref = yl -= key_entry_height/2;  /* centralise the keys */
 		key_count = 0;
 	
 		if (key_box.l_type>-3) {
 			term_apply_lp_properties(&key_box);
 			(*t->move)(key_xl,key_yb);
 			(*t->vector)(key_xl,key_yt);
 			(*t->vector)(key_xr,key_yt);
 			(*t->vector)(key_xr,key_yb);
 			(*t->vector)(key_xl,key_yb);
                    /* draw a horizontal line between key title
                       and first entry                          */             /* JFi */
		        (*t->move)  (key_xl,key_yt - (ktitl_lines) * t->v_char); /* JFi */
                      (*t->vector)(key_xr,key_yt - (ktitl_lines) * t->v_char); /* JFi */
 		}
 	} /* lkey */

/* DRAW CURVES */

	this_plot = plots;
 	for (curve = 0; curve < pcount; this_plot = this_plot->next_cp, curve++) {
  		int localkey = lkey; /* a local copy */

		/* set scaling for this plot's axes */
		x_axis=this_plot->x_axis;
		y_axis=this_plot->y_axis;
		
		term_apply_lp_properties(&(this_plot->lp_properties));
		
		if (this_plot->title && !*this_plot->title) {
		    localkey = 0;
		} else {
			if (localkey != 0 && this_plot->title) {
				key_count++;
				if ( key_just == JLEFT) {
					(*t->justify_text)(LEFT);
					(*t->put_text)(xl+key_text_left,
							 yl,this_plot->title);
				} else {
					if ((*t->justify_text)(RIGHT)) {
						(*t->put_text)(xl+key_text_right,
							yl,this_plot->title);
					} else {
						int x=xl+key_text_right-(t->h_char)*strlen(this_plot->title);
						if (key_hpos == TOUT ||
						     i_inrange(x, xleft, xright))
				 		(*t->put_text)(x, yl,this_plot->title);
					}
				}

				/* draw sample depending on bits set in plot_style */
				if ((this_plot->plot_style & 1) ||
					 ( (this_plot->plot_style & 4) && this_plot->plot_type==DATA))
				{ /* errors for data plots only */
					(*t->move)(xl+key_sample_left,yl);
					(*t->vector)(xl+key_sample_right,yl);
				}

				/* oops - doing the point sample now breaks postscript
				 * terminal for example, which changes current line style
				 * when drawing a point, but does not restore it.
				 * We simply draw the point sample after plotting
				 */

				if ( this_plot->plot_type == DATA &&
				     (this_plot->plot_style & 4) &&
				     bar_size>0.0)
				{
					(*t->move)(xl+key_sample_left,yl+ERRORBARTIC);
					(*t->vector)(xl+key_sample_left,yl-ERRORBARTIC);
					(*t->move)(xl+key_sample_right,yl+ERRORBARTIC);
					(*t->vector)(xl+key_sample_right,yl-ERRORBARTIC);
				}
			}
		}

		/* and now the curves, plus any special key requirements */
		/* be sure to draw all lines before drawing any points */
		
		switch(this_plot->plot_style) {
			/*{{{  IMPULSE*/
			case IMPULSES:
				plot_impulses(this_plot, axis_zero[y_axis], axis_zero[x_axis]);
				break;
			/*}}}*/
			/*{{{  LINES*/
			case LINES:
				plot_lines(this_plot);
				break;
			/*}}}*/
			/*{{{  STEPS*/
			case STEPS:
				plot_steps(this_plot);
				break;
			/*}}}*/
			/*{{{  FSTEPS*/
			case FSTEPS:
				plot_fsteps(this_plot);
				break;
			/*}}}*/
			/*{{{  HISTEPS*/
			case HISTEPS:
				plot_histeps(this_plot);
				break;
			/*}}}*/
			/*{{{  POINTSTYLE*/
			case POINTSTYLE:
				plot_points(this_plot);
				break;
			/*}}}*/
			/*{{{  LINESPOINTS*/
			case LINESPOINTS:
				plot_lines(this_plot);
				plot_points(this_plot);
				break;
			/*}}}*/
			/*{{{  DOTS*/
			case DOTS:
				if (localkey != 0 && this_plot->title) {
					(*t->point)(xl+key_point_offset,yl, -1);
				}
				plot_dots(this_plot);
				break;
			/*}}}*/
			/*{{{  YERRORBARS*/
			case YERRORBARS: {
				plot_bars(this_plot);
				plot_points(this_plot);
				break;
			}
			/*}}}*/
			/*{{{  XERRORBARS*/
			case XERRORBARS: {
				plot_bars(this_plot);
				plot_points(this_plot);
			}
			/*}}}*/
			/*{{{  XYERRORBARS*/
			case XYERRORBARS:
				plot_bars(this_plot);
				plot_points(this_plot);
				break;
			
			/*}}}*/
			/*{{{  BOXXYERROR*/
			case BOXXYERROR:
				plot_boxes(this_plot,axis_zero[x_axis]);
				break;
			/*}}}*/
			/*{{{  BOXERROR (falls through to)*/
			case BOXERROR:
				plot_bars(this_plot);
				/* no break */
					    
			/*}}}*/
			/*{{{  BOXES*/
			case BOXES:
				plot_boxes(this_plot,axis_zero[x_axis]);
				break;
			/*}}}*/
			/*{{{  VECTOR*/
			case VECTOR:
				plot_vectors(this_plot);
				break;
					    
			/*}}}*/
			/*{{{  FINANCEBARS*/
			case FINANCEBARS:
				plot_f_bars(this_plot);
				break;
			/*}}}*/
			/*{{{  CANDLESTICKS*/
			case CANDLESTICKS:
				plot_c_bars(this_plot);
				break;
			/*}}}*/
		}

		
		if (localkey && this_plot->title) {
		    /* we deferred point sample until now */
		    if (this_plot->plot_style & 2)
			(*t->point)(xl+key_point_offset,yl,
    			   this_plot->lp_properties.p_type);
    			   
		    if (key_count >= key_rows ) {
			yl = yl_ref;
			xl += key_col_wth;
			key_count = 0;
		    } else 
		    	yl = yl - key_entry_height;
		}
	}

	term_end_plot();
}


/* BODGES */
#undef ytic
#undef xtic

/* plot_impulses:
 * Plot the curves in IMPULSES style
 */

static void
plot_impulses(plot, yaxis_x, xaxis_y)
	struct curve_points *plot;
	int yaxis_x, xaxis_y;
{
    int i;
    int x,y;
    struct termentry *t = term;

    for (i = 0; i < plot->p_count; i++) {
	   switch (plot->points[i].type) {
		  case INRANGE: {
			 x = map_x(plot->points[i].x);
			 y = map_y(plot->points[i].y);
			 break;
		  }
		  case OUTRANGE: {
			 if (!inrange(plot->points[i].x, x_min,x_max))
			   continue;
			 x = map_x(plot->points[i].x);
			 if ((y_min < y_max 
				 && plot->points[i].y < y_min)
				|| (y_max < y_min 
				    && plot->points[i].y > y_min))
			   y = map_y(y_min);
			 else
			   y = map_y(y_max);
			
			 break;
		  }
		  default:		/* just a safety */
		  case UNDEFINED: {
			 continue;
		  }
	   }
				    
	   if (polar)
	      (*t->move)(yaxis_x,xaxis_y);
	   else
	      (*t->move)(x,xaxis_y);
	   (*t->vector)(x,y);
    }

}

/* plot_lines:
 * Plot the curves in LINES style
 */
static void
plot_lines(plot)
	struct curve_points *plot;
{
    int i;				/* point index */
    int x,y;				/* point in terminal coordinates */
    struct termentry *t = term;
    enum coord_type prev = UNDEFINED; /* type of previous point */
    double ex, ey;			/* an edge point */
    double lx[2], ly[2];		/* two edge points */

    for (i = 0; i < plot->p_count; i++) {
	   switch (plot->points[i].type) {
		  case INRANGE: {
			 x = map_x(plot->points[i].x);
			 y = map_y(plot->points[i].y);

			 if (prev == INRANGE) {
				(*t->vector)(x,y);
			 } else if (prev == OUTRANGE) {
				/* from outrange to inrange */
				if (!clip_lines1) {
				    (*t->move)(x,y);
				} else {
				    edge_intersect(plot->points, i, &ex, &ey);
				    (*t->move)(map_x(ex), map_y(ey));
				    (*t->vector)(x,y);
				}
			 } else {		/* prev == UNDEFINED */
				(*t->move)(x,y);
				(*t->vector)(x,y);
			 }
				    
			 break;
		  }
		  case OUTRANGE: {
			 if (prev == INRANGE) {
				/* from inrange to outrange */
				if (clip_lines1) {
				    edge_intersect(plot->points, i, &ex, &ey);
				    (*t->vector)(map_x(ex), map_y(ey));
				}
			 } else if (prev == OUTRANGE) {
				/* from outrange to outrange */
				if (clip_lines2) {
				    if (two_edge_intersect(plot->points, i, lx, ly)) {
					   (*t->move)(map_x(lx[0]), map_y(ly[0]));
					   (*t->vector)(map_x(lx[1]), map_y(ly[1]));
				    }
				}
			 }
			 break;
		  }
		  default:		/* just a safety */
		  case UNDEFINED: {
			 break;
		  }
	   }
	   prev = plot->points[i].type;
    }
}

/* XXX - JG  */
/* plot_steps:				
 * Plot the curves in STEPS style
 */
static void
plot_steps(plot)
struct curve_points *plot;
{
    int i;				/* point index */
    int x,y;				/* point in terminal coordinates */
    struct termentry *t = term;
    enum coord_type prev = UNDEFINED;	/* type of previous point */
    double ex, ey;			/* an edge point */
    double lx[2], ly[2];		/* two edge points */
    int yprev=0;                 /* previous point coordinates */

    for (i = 0; i < plot->p_count; i++) {
	   switch (plot->points[i].type) {
		  case INRANGE: {
			 x = map_x(plot->points[i].x);
			 y = map_y(plot->points[i].y);

			 if (prev == INRANGE) {
				(*t->vector)(x,yprev);
				(*t->vector)(x,y);
			 } else if (prev == OUTRANGE) {
				/* from outrange to inrange */
				if (!clip_lines1) {
				    (*t->move)(x,y);
				} else {		/* find edge intersection */
				    edge_intersect_steps(plot->points, i, &ex, &ey);
				    (*t->move)(map_x(ex), map_y(ey));
				    (*t->vector)(x,map_y(ey));
				    (*t->vector)(x,y);
				}
			 } else {		/* prev == UNDEFINED */
				(*t->move)(x,y);
				(*t->vector)(x,y);
			 }
	   	 yprev = y;
			 break;
		  }
		  case OUTRANGE: {
			 if (prev == INRANGE) {
				/* from inrange to outrange */
				if (clip_lines1) {
				    edge_intersect_steps(plot->points, i, &ex, &ey);
				    (*t->vector)(map_x(ex), yprev);
				    (*t->vector)(map_x(ex), map_y(ey));
				}
			 } else if (prev == OUTRANGE) {
				/* from outrange to outrange */
				if (clip_lines2) {
				    if (two_edge_intersect_steps(plot->points, i, lx, ly)) {
					   (*t->move)(map_x(lx[0]), map_y(ly[0]));
					   (*t->vector)(map_x(lx[1]), map_y(ly[0]));
					   (*t->vector)(map_x(lx[1]), map_y(ly[1]));
				    }
				}
			 }
			 break;
		  }
		  default:		/* just a safety */
		  case UNDEFINED: {
			 break;
		  }
	   }
	   prev  = plot->points[i].type;
    }
}
/* XXX - HOE  */
/* plot_fsteps:				
 * Plot the curves in STEPS style by step on forward yvalue
 */
static void
plot_fsteps(plot)
struct curve_points *plot;
{
    int i;				/* point index */
    int x,y;				/* point in terminal coordinates */
    struct termentry *t = term;
    enum coord_type prev = UNDEFINED;	/* type of previous point */
    double ex, ey;			/* an edge point */
    double lx[2], ly[2];		/* two edge points */
    int xprev = 0;		/* previous point coordinates */

    for (i = 0; i < plot->p_count; i++) {
	   switch (plot->points[i].type) {
		  case INRANGE: {
			 x = map_x(plot->points[i].x);
			 y = map_y(plot->points[i].y);

			 if (prev == INRANGE) {
				(*t->vector)(xprev,y);
				(*t->vector)(x,y);
			 } else if (prev == OUTRANGE) {
				/* from outrange to inrange */
				if (!clip_lines1) {
				    (*t->move)(x,y);
				} else {		/* find edge intersection */
				    edge_intersect_fsteps(plot->points, i, &ex, &ey);
				    (*t->move)(map_x(ex), map_y(ey));
				    (*t->vector)(map_x(ex),y);
				    (*t->vector)(x,y);
				}
			 } else {		/* prev == UNDEFINED */
				(*t->move)(x,y);
				(*t->vector)(x,y);
			 }
			 xprev = x;
			 break;
		  }
		  case OUTRANGE: {
			 if (prev == INRANGE) {
				/* from inrange to outrange */
				if (clip_lines1) {
				    edge_intersect_fsteps(plot->points, i, &ex, &ey);
				    (*t->vector)(xprev,map_y(ey));
				    (*t->vector)(map_x(ex), map_y(ey));
				}
			 } else if (prev == OUTRANGE) {
				/* from outrange to outrange */
				if (clip_lines2) {
				    if (two_edge_intersect_fsteps(plot->points, i, lx, ly)) {
					   (*t->move)(map_x(lx[0]), map_y(ly[0]));
					   (*t->vector)(map_x(lx[0]), map_y(ly[1]));
					   (*t->vector)(map_x(lx[1]), map_y(ly[1]));
				    }
				}
			 }
			 break;
		  }
		  default:		/* just a safety */
		  case UNDEFINED: {
			 break;
		  }
	   }
	   prev  = plot->points[i].type;
    }
}

/* CAC  */
/* plot_histeps:				
 * Plot the curves in HISTEPS style
 */
static void
plot_histeps(plot)
struct curve_points *plot;
{
    int i,		/* point index */
      hold, bigi;       /* indices for sorting */
    int xl,yl;		  /* cursor position in terminal coordinates */
    struct termentry *t = term;
    double x,y, xn, yn;                 /* point position */
    int *gl, goodcount;       /* array to hold list of valid points */

/* preliminary count of points inside array */ 
    goodcount = 0;
    for(i=0; i < plot->p_count; i++)
      if ( plot->points[i].type == INRANGE ||
	   plot->points[i].type == OUTRANGE)
	++goodcount;
    if (goodcount < 2) return;  /* cannot plot less than 2 points */

    gl = (int *) gp_alloc ( goodcount * sizeof(int), "histeps valid point mapping" );
    if (gl == NULL) return;

/* fill gl array with indexes of valid (non-undefined) points.  */
    goodcount = 0;
    for(i=0; i < plot->p_count; i++)
      if ( plot->points[i].type == INRANGE ||
	   plot->points[i].type == OUTRANGE) {
	gl[goodcount] = i;
	++goodcount;
      }

/* sort the data */
    for(bigi = i = 1; i < goodcount; ) {
      if(plot->points[gl[i]].x < plot->points[gl[i-1]].x) {
	hold = gl[i];
	gl[i] = gl[i-1];
	gl[i-1] = hold;
	if(i>1) {
	  i--;
	  continue;
	}
      }
      i = ++bigi;
    }

    x = (3.0*plot->points[gl[0]].x - plot->points[gl[1]].x) / 2.0;
    y = 0.0;

    xl = map_x(x); yl = map_y(y);
    (*t->move)(xl, yl);

    for (i = 0; i < goodcount - 1; i++) { /* loop over all points except last  */

	yn = plot->points[gl[i]].y;
	xn = (plot->points[gl[i]].x + plot->points[gl[i+1]].x) / 2.0;
	histeps_vertical(&xl, &yl, x, y, yn);
	histeps_horizontal(&xl, &yl, x, xn, yn);

	x = xn; y = yn;
    }

    yn = plot->points[gl[i]].y;
    xn = (3.0*plot->points[gl[i]].x - plot->points[gl[i-1]].x) / 2.0;
    histeps_vertical(&xl, &yl, x, y, yn);
    histeps_horizontal(&xl, &yl, x, xn, yn);
    histeps_vertical(&xl, &yl, xn, yn, 0.0);

    free(gl);
}
/* CAC 
 * Draw vertical line for the histeps routine.
 * Performs clipping.
 */
static void
histeps_vertical(xl, yl, x, y1, y2)
     int *xl, *yl;                     /* keeps track of "cursor" position */
     double x, y1, y2;                 /* coordinates of vertical line */
{
    struct termentry *t = term;
    /* global x_min, x_max, y_min, y_max */
    int xm, y1m, y2m;

    if ( (y1 < y_min && y2 < y_min) ||
        (y1 > y_max && y2 > y_max) ||
        x < x_min ||
        x > x_max )
      return;

    if (y1 < y_min) y1 = y_min;
    if (y1 > y_max) y1 = y_max;
    if (y2 < y_min) y2 = y_min;
    if (y2 > y_max) y2 = y_max;

    xm = map_x(x);
    y1m = map_y(y1);
    y2m = map_y(y2);

    if(y1m != *yl || xm != *xl)
      (*t->move)(xm, y1m);
    (*t->vector)(xm, y2m);
    *xl = xm;
    *yl = y2m;

    return;
}
/* CAC 
 * Draw horizontal line for the histeps routine.
 * Performs clipping.
 */
static void
histeps_horizontal(xl, yl, x1, x2, y)
     int *xl, *yl;                     /* keeps track of "cursor" position */
     double x1, x2, y;                 /* coordinates of vertical line */
{
    struct termentry *t = term;
    /* global x_min, x_max, y_min, y_max */
    int x1m, x2m, ym;

    if ( (x1 < x_min && x2 < x_min) ||
        (x1 > x_max && x2 > x_max) ||
        y < y_min ||
        y > y_max )
      return;

    if (x1 < x_min) x1 = x_min;
    if (x1 > x_max) x1 = x_max;
    if (x2 < x_min) x2 = x_min;
    if (x2 > x_max) x2 = x_max;

    ym = map_y(y);
    x1m = map_x(x1);
    x2m = map_x(x2);

    if(x1m != *xl || ym != *yl)
      (*t->move)(x1m, ym);
    (*t->vector)(x2m, ym);
    *xl = x2m;
    *yl = ym;

    return;
}


/* plot_bars:
 * Plot the curves in ERRORBARS style
 *  we just plot the bars; the points are plotted in plot_points
 */
static void
plot_bars(plot)
	struct curve_points *plot;
{
    int i;				/* point index */
    struct termentry *t = term;
    double x, y;                        /* position of the bar */
    double ylow, yhigh;		/* the ends of the bars */
    double xlow, xhigh;
    unsigned int xM, ylowM, yhighM; /* the mapped version of above */
    unsigned int yM, xlowM, xhighM;
    TBOOLEAN low_inrange, high_inrange;
    int tic = ERRORBARTIC;
    
/* Limitation: no boxes with x errorbars */

    if ((plot->plot_style == YERRORBARS) || (plot->plot_style == XYERRORBARS) ||
         (plot->plot_style == BOXERROR)){
/* Draw the vertical part of the bar */
    for (i = 0; i < plot->p_count; i++) {
	   /* undefined points don't count */
	   if (plot->points[i].type == UNDEFINED)
		continue;

	   /* check to see if in xrange */
	   x = plot->points[i].x;
	   if (! inrange(x, x_min, x_max))
		continue;
	   xM = map_x(x);

	   /* find low and high points of bar, and check yrange */
	   yhigh = plot->points[i].yhigh;
	   ylow = plot->points[i].ylow;

	   high_inrange = inrange(yhigh, y_min,y_max);
	   low_inrange = inrange(ylow, y_min,y_max);

	   /* compute the plot position of yhigh */
	   if (high_inrange)
		yhighM = map_y(yhigh);
	   else if (samesign(yhigh-y_max, y_max-y_min))
		yhighM = map_y(y_max);
	   else
		yhighM = map_y(y_min);
	   
	   /* compute the plot position of ylow */
	   if (low_inrange)
		ylowM = map_y(ylow);
	   else if (samesign(ylow-y_max, y_max-y_min))
		ylowM = map_y(y_max);
	   else
		ylowM = map_y(y_min);

	   if (!high_inrange && !low_inrange && ylowM == yhighM)
		/* both out of range on the same side */
		  continue;

	   /* by here everything has been mapped */
	   (*t->move)(xM, ylowM);
	   (*t->vector)(xM, yhighM); /* draw the main bar */
           if(bar_size > 0.0){
	   (*t->move)((unsigned int)(xM-bar_size*tic), ylowM); /* draw the bottom tic */
	   (*t->vector)((unsigned int)(xM+bar_size*tic), ylowM);
	   (*t->move)((unsigned int)(xM-bar_size*tic), yhighM); /* draw the top tic */
	   (*t->vector)((unsigned int)(xM+bar_size*tic), yhighM);
    }
    } /* for loop */
    } /* if yerrorbars OR xyerrorbars */

    if ((plot->plot_style == XERRORBARS) || (plot->plot_style == XYERRORBARS)){
/* Draw the horizontal part of the bar */
    for (i = 0; i < plot->p_count; i++) {
           /* undefined points don't count */
           if (plot->points[i].type == UNDEFINED)
                continue;

           /* check to see if in yrange */
           y = plot->points[i].y;
           if (! inrange(y, y_min, y_max))
                continue;
           yM = map_y(y);

           /* find low and high points of bar, and check xrange */
           xhigh = plot->points[i].xhigh;
           xlow = plot->points[i].xlow;

           high_inrange = inrange(xhigh, x_min,x_max);
           low_inrange = inrange(xlow, x_min,x_max);

           /* compute the plot position of xhigh */
           if (high_inrange)
                xhighM = map_x(xhigh);
           else if (samesign(xhigh-x_max, x_max-x_min))
                xhighM = map_x(x_max);
           else
                xhighM = map_x(x_min);

           /* compute the plot position of xlow */
           if (low_inrange)
                xlowM = map_x(xlow);
           else if (samesign(xlow-x_max, x_max-x_min))
                xlowM = map_x(x_max);
           else
                xlowM = map_x(x_min);

           if (!high_inrange && !low_inrange && xlowM == xhighM)
                /* both out of range on the same side */
                  continue;

           /* by here everything has been mapped */
           (*t->move)(xlowM, yM);
           (*t->vector)(xhighM, yM); /* draw the main bar */
           if(bar_size > 0.0){
              (*t->move)(xlowM, (unsigned int)(yM-bar_size*tic)); /* draw the left tic */
              (*t->vector)(xlowM, (unsigned int)(yM+bar_size*tic));
              (*t->move)(xhighM, (unsigned int)(yM-bar_size*tic)); /* draw the right tic */
              (*t->vector)(xhighM, (unsigned int)(yM+bar_size*tic));
           }
    } /* for loop */
    } /* if xerrorbars OR xyerrorbars */
}

/* plot_boxes:
 * Plot the curves in BOXES style
 */
static void
plot_boxes(plot,xaxis_y)
	struct curve_points *plot;
	int xaxis_y;
{
    int i;				/* point index */
    int xl,xr,yt;			/* point in terminal coordinates */
	double dxl,dxr,dyt;
    struct termentry *t = term;
    enum coord_type prev = UNDEFINED; /* type of previous point */
    TBOOLEAN boxxy = (plot->plot_style == BOXXYERROR);

    for (i = 0; i < plot->p_count; i++) {
    	
	   switch (plot->points[i].type) {
		  case OUTRANGE:
		  case INRANGE: {
			if (plot->points[i].z<0.0) {
				/* need to auto-calc width */
				/* ASSERT(boxwidth <= 0.0); - else graphics.c provides width */
				
				/* calculate width */
				if (prev!=UNDEFINED)
					dxl = (plot->points[i-1].x - plot->points[i].x)/2.0;
				else
					dxl = 0.0;
					
				if (i < plot->p_count-1)
				{
					if (plot->points[i+1].type!=UNDEFINED)
						dxr = (plot->points[i+1].x - plot->points[i].x)/2.0;
					else
						dxr = -dxl;
				}
				else
				{
					dxr = -dxl;
				}
				
				if (prev==UNDEFINED)
					dxl = -dxr;

				dxl= plot->points[i].x+dxl;
				dxr= plot->points[i].x+dxr;				
			}
			else /* z>=0 */
			{
				dxr = plot->points[i].xhigh;
				dxl = plot->points[i].xlow;
			}

			if (boxxy) {
				dyt = plot->points[i].yhigh;
				xaxis_y = map_y(plot->points[i].ylow);
         } else {
				dyt= plot->points[i].y;
			}

			/* clip to border */
			if ((y_min < y_max  && dyt < y_min)
				|| (y_max < y_min  && dyt > y_min))
			   dyt = y_min;
			if ((y_min < y_max  && dyt > y_max)
				|| (y_max<y_min  && dyt < y_max))
			   dyt = y_max;
			if ((x_min < x_max  && dxr < x_min)
				|| (x_max < x_min  && dxr > x_min))
			   dxr = x_min;
			if ((x_min < x_max  && dxr > x_max)
				|| (x_max<x_min  && dxr < x_max))
			   dxr = x_max;
			if ((x_min < x_max  && dxl < x_min)
				|| (x_max < x_min  && dxl > x_min))
			   dxl = x_min;
			if ((x_min < x_max  && dxl > x_max)
				|| (x_max<x_min  && dxl < x_max))
			   dxl = x_max;

			xl= map_x(dxl);
			xr= map_x(dxr);
			yt = map_y(dyt);

			(*t->move)(xl,xaxis_y);
			(*t->vector)(xl,yt);
			(*t->vector)(xr,yt);
			(*t->vector)(xr,xaxis_y);
			(*t->vector)(xl,xaxis_y);
			break;
		 } /* case OUTRANGE, INRANGE */
		 
		  default:		/* just a safety */
		  case UNDEFINED: {
			 break;
		  }
		  
	   } /* switch point-type */
	   
	   prev = plot->points[i].type;
	   
    } /*loop*/
}



/* plot_points:
 * Plot the curves in POINTSTYLE style
 */
static void
plot_points(plot)
	struct curve_points *plot;
{
    int i;
    int x,y;
    struct termentry *t = term;

    for (i = 0; i < plot->p_count; i++) {
	   if (plot->points[i].type == INRANGE) {
		  x = map_x(plot->points[i].x);
		  y = map_y(plot->points[i].y);
		  /* do clipping if necessary */
		  if (!clip_points ||
			 (   x >= xleft + p_width  && y >= ybot + p_height 
			  && x <= xright - p_width && y <= ytop - p_height))
		    (*t->point)(x,y, plot->lp_properties.p_type);
	   }
    }
}

/* plot_dots:
 * Plot the curves in DOTS style
 */
static void
plot_dots(plot)
	struct curve_points *plot;
{
    int i;
    int x,y;
    struct termentry *t = term;

    for (i = 0; i < plot->p_count; i++) {
	   if (plot->points[i].type == INRANGE) {
		  x = map_x(plot->points[i].x);
		  y = map_y(plot->points[i].y);
		  /* point type -1 is a dot */
		  (*t->point)(x,y, -1);
	   }
    }
}

/* plot_vectors:
 * Plot the curves in VECTORS style
 */
static void
plot_vectors(plot)
	struct curve_points *plot;
{
    int i;
    int x1,y1,x2,y2;
    struct termentry *t = term;

    for (i = 0; i < plot->p_count; i++) {
	   if (plot->points[i].type == INRANGE) {
		  x1 = map_x(plot->points[i].xlow);
		  y1 = map_y(plot->points[i].ylow);
		  x2 = map_x(plot->points[i].xhigh);
		  y2 = map_y(plot->points[i].yhigh);
		  (*t->arrow)(x1,y1, x2,y2, TRUE);
	   }
    }
}


/* plot_f_bars() - finance bars */
static void
plot_f_bars(plot)
	struct curve_points *plot;
{
    int i;				/* point index */
    struct termentry *t = term;
    double x;				/* position of the bar */
    double ylow, yhigh, yclose, yopen;		/* the ends of the bars */
    unsigned int xM, ylowM, yhighM; /* the mapped version of above */
    TBOOLEAN low_inrange, high_inrange;
    int tic = ERRORBARTIC/2;
    
    for (i = 0; i < plot->p_count; i++) {
	   /* undefined points don't count */
	   if (plot->points[i].type == UNDEFINED)
		continue;

	   /* check to see if in xrange */
	   x = plot->points[i].x;
	   if (! inrange(x, x_min, x_max))
		continue;
	   xM = map_x(x);

	   /* find low and high points of bar, and check yrange */
	   yhigh = plot->points[i].yhigh;
	   ylow = plot->points[i].ylow;
	   yclose = plot->points[i].z;
	   yopen = plot->points[i].y;

	   high_inrange = inrange(yhigh, y_min,y_max);
	   low_inrange = inrange(ylow, y_min,y_max);

	   /* compute the plot position of yhigh */
	   if (high_inrange)
		yhighM = map_y(yhigh);
	   else if (samesign(yhigh-y_max, y_max-y_min))
		yhighM = map_y(y_max);
	   else
		yhighM = map_y(y_min);
	   
	   /* compute the plot position of ylow */
	   if (low_inrange)
		ylowM = map_y(ylow);
	   else if (samesign(ylow-y_max, y_max-y_min))
		ylowM = map_y(y_max);
	   else
		ylowM = map_y(y_min);

	   if (!high_inrange && !low_inrange && ylowM == yhighM)
		/* both out of range on the same side */
		  continue;

	   /* by here everything has been mapped */
	   (*t->move)(xM, ylowM);
	   (*t->vector)(xM, yhighM); /* draw the main bar */
           /* draw the open tic */
	   (*t->move)((unsigned int)(xM-bar_size*tic),map_y(yopen) );
	   (*t->vector)(xM, map_y(yopen));
           /* draw the close tic */
	   (*t->move)((unsigned int)(xM+bar_size*tic), map_y(yclose));
	   (*t->vector)(xM, map_y(yclose));
    }
}


/* plot_c_bars:
 * Plot the curves in CANDLESTICSK style
 *  we just plot the bars; the points are not plotted 
 */
static void
plot_c_bars(plot)
	struct curve_points *plot;
{
    int i;				/* point index */
    struct termentry *t = term;
    double x;				/* position of the bar */
    double ylow, yhigh, yclose, yopen;		/* the ends of the bars */
    unsigned int xM, ylowM, yhighM; /* the mapped version of above */
    TBOOLEAN low_inrange, high_inrange;
    int tic = ERRORBARTIC/2;
    
    for (i = 0; i < plot->p_count; i++) {
	   /* undefined points don't count */
	   if (plot->points[i].type == UNDEFINED)
		continue;

	   /* check to see if in xrange */
	   x = plot->points[i].x;
	   if (! inrange(x, x_min, x_max))
		continue;
	   xM = map_x(x);

	   /* find low and high points of bar, and check yrange */
	   yhigh = plot->points[i].yhigh;
	   ylow = plot->points[i].ylow;
	   yclose = plot->points[i].z;
	   yopen = plot->points[i].y;

	   high_inrange = inrange(yhigh, y_min,y_max);
	   low_inrange = inrange(ylow, y_min,y_max);

	   /* compute the plot position of yhigh */
	   if (high_inrange)
		yhighM = map_y(yhigh);
	   else if (samesign(yhigh-y_max, y_max-y_min))
		yhighM = map_y(y_max);
	   else
		yhighM = map_y(y_min);
	   
	   /* compute the plot position of ylow */
	   if (low_inrange)
		ylowM = map_y(ylow);
	   else if (samesign(ylow-y_max, y_max-y_min))
		ylowM = map_y(y_max);
	   else
		ylowM = map_y(y_min);

	   if (!high_inrange && !low_inrange && ylowM == yhighM)
		/* both out of range on the same side */
		  continue;

	   /* by here everything has been mapped */
	   if( yopen <= yclose){
	     (*t->move)(xM, ylowM);
	     (*t->vector)(xM, map_y(yopen)); /* draw the lower bar */
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM-bar_size*tic), map_y(yopen) );
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM+bar_size*tic), map_y(yopen) );
	     (*t->vector)((unsigned int)(xM+bar_size*tic), map_y(yclose));
	     (*t->vector)((unsigned int)(xM-bar_size*tic), map_y(yclose));
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM-bar_size*tic), map_y(yopen) );
	     (*t->move)  (xM    , map_y(yclose)); /* draw the close tic */
	     (*t->vector)(xM    , yhighM);
	   } else {
	     (*t->move)  (xM    , ylowM);
	     (*t->vector)(xM    , yhighM);
             /* draw the open tic */
	     (*t->move)  ((unsigned int)(xM-bar_size*tic), map_y(yopen) );
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM+bar_size*tic), map_y(yopen) );
	     (*t->vector)((unsigned int)(xM+bar_size*tic), map_y(yclose));
	     (*t->vector)((unsigned int)(xM-bar_size*tic), map_y(yclose));
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM-bar_size*tic), map_y(yopen) );
             /* draw the close tic */
	     (*t->move)  ((unsigned int)(xM-bar_size*tic/2), map_y(yclose));
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM-bar_size*tic/2), map_y(yopen) );
             /* draw the close tic */
	     (*t->move)  ((unsigned int)(xM+bar_size*tic/2), map_y(yclose));
             /* draw the open tic */
	     (*t->vector)((unsigned int)(xM+bar_size*tic/2), map_y(yopen) );
	   }

    }
}

/* single edge intersection algorithm */
/* Given two points, one inside and one outside the plot, return
 * the point where an edge of the plot intersects the line segment defined 
 * by the two points.
 */
static void
edge_intersect(points, i, ex, ey)
	struct coordinate GPHUGE *points; /* the points array */
	int i;				/* line segment from point i-1 to point i */
	double *ex, *ey;		/* the point where it crosses an edge */
{
    /* global x_min, x_max, y_min, x_max */
    double ix = points[i-1].x;
    double iy = points[i-1].y;
    double ox = points[i].x;
    double oy = points[i].y;
    double x, y;			/* possible intersection point */

    if(points[i].type == INRANGE)
      {
	/* swap points around so that ix/ix/iz are INRANGE and ox/oy/oz are OUTRANGE */
	x = ix;ix = ox;ox = x;
	y = iy;iy = oy;oy = y;
      }

    /* nasty degenerate cases, effectively drawing to an infinity point (?)
       cope with them here, so don't process them as a "real" OUTRANGE point 

       If more than one coord is -VERYLARGE, then can't ratio the "infinities"
       so drop out by returning the INRANGE point. 

       Obviously, only need to test the OUTRANGE point (coordinates) */
    if(ox == -VERYLARGE || oy == -VERYLARGE)
      {
	*ex = ix;
	*ey = iy;

	if(ox == -VERYLARGE)
	  {
	    /* can't get a direction to draw line, so simply return INRANGE point */
	    if(oy == -VERYLARGE)
	      return;

	    *ex = x_min;
	    return;
	  }

	/* obviously oy is -VERYLARGE and ox != -VERYLARGE */
	*ey = y_min;
	return;
      }

    /*
     * Can't have case (ix == ox && iy == oy) as one point
     * is INRANGE and one point is OUTRANGE.
     */    
    if (iy == oy) {
	   /* horizontal line */
	   /* assume inrange(iy, y_min, y_max) */
	   *ey = iy;		/* == oy */

	   if (inrange(x_max, ix, ox))
		*ex = x_max;
	   else if (inrange(x_min, ix, ox))
		*ex = x_min;
	   else {
		graph_error("error in edge_intersect");
	   }
	   return;
    } else if (ix == ox) {
	   /* vertical line */
	   /* assume inrange(ix, x_min, x_max) */
	   *ex = ix;		/* == ox */

	   if (inrange(y_max, iy, oy))
		*ey = y_max;
	   else if (inrange(y_min, iy, oy))
		*ey = y_min;
	   else {
		graph_error("error in edge_intersect");
	   }
	   return;
    }

    /* slanted line of some kind */

    /* does it intersect y_min edge */
    if (inrange(y_min, iy, oy) && y_min != iy && y_min != oy) {
	   x = ix + (y_min-iy) * ((ox-ix) / (oy-iy));
	   if (inrange(x, x_min, x_max)) {
		  *ex = x;
		  *ey = y_min;
		  return;			/* yes */
	   }
    }
    
    /* does it intersect y_max edge */
    if (inrange(y_max, iy, oy) && y_max != iy && y_max != oy) {
	   x = ix + (y_max-iy) * ((ox-ix) / (oy-iy));
	   if (inrange(x, x_min, x_max)) {
		  *ex = x;
		  *ey = y_max;
		  return;			/* yes */
	   }
    }

    /* does it intersect x_min edge */
    if (inrange(x_min, ix, ox) && x_min != ix && x_min != ox) {
	   y = iy + (x_min-ix) * ((oy-iy) / (ox-ix));
	   if (inrange(y, y_min, y_max)) {
		  *ex = x_min;
		  *ey = y;
		  return;
	   }
    }

    /* does it intersect x_max edge */
    if (inrange(x_max, ix, ox) && x_max != ix && x_max != ox) {
	   y = iy + (x_max-ix) * ((oy-iy) / (ox-ix));
	   if (inrange(y, y_min, y_max)) {
		  *ex = x_max;
		  *ey = y;
		  return;
	   }
    }

    /* If we reach here, the inrange point is on the edge, and
     * the line segment from the outrange point does not cross any 
     * other edges to get there. In this case, we return the inrange 
     * point as the 'edge' intersection point. This will basically draw
     * line.
     */
    *ex = ix; 
    *ey = iy;
    return;
}

/* XXX - JG  */
/* single edge intersection algorithm for "steps" curves */
/* 
 * Given two points, one inside and one outside the plot, return
 * the point where an edge of the plot intersects the line segments
 * forming the step between the two points. 
 *
 * Recall that if P1 = (x1,y1) and P2 = (x2,y2), the step from  
 * P1 to P2 is drawn as two line segments: (x1,y1)->(x2,y1) and 
 * (x2,y1)->(x2,y2). 
 */
static void
edge_intersect_steps(points, i, ex, ey)
	struct coordinate GPHUGE *points; /* the points array */
	int i;				/* line segment from point i-1 to point i */
	double *ex, *ey;		/* the point where it crosses an edge */
{
    /* global x_min, x_max, y_min, x_max */
    double ax = points[i-1].x;
    double ay = points[i-1].y;
    double bx = points[i].x;
    double by = points[i].y;

    if (points[i].type == INRANGE) {	/* from OUTRANGE to INRANG */
	    if (inrange(ay,y_min,y_max)) {
		*ey = ay;
		if (ax > x_max)
			*ex = x_max;
		else			/* x < x_min */
			*ex = x_min;
	    } else {
	    	*ex = bx;
		if (ay > y_max)     
			*ey = y_max;
		else			/* y < y_min */
			*ey = y_min;
	    }
    } else {				/* from INRANGE to OUTRANGE */
	    if (inrange(bx,x_min,x_max)) {
		*ex = bx;
		if (by > y_max)
			*ey = y_max;
		else			/* y < y_min */
			*ey = y_min;
	    } else {
	    	*ey = ay;
		if (bx > x_max)     
			*ex = x_max;
		else			/* x < x_min */
			*ex = x_min;
	    }
    }
    return;
}

/* XXX - HOE  */
/* single edge intersection algorithm for "fsteps" curves */
/* fsteps means step on forward y-value. 
 * Given two points, one inside and one outside the plot, return
 * the point where an edge of the plot intersects the line segments
 * forming the step between the two points. 
 *
 * Recall that if P1 = (x1,y1) and P2 = (x2,y2), the step from  
 * P1 to P2 is drawn as two line segments: (x1,y1)->(x1,y2) and 
 * (x1,y2)->(x2,y2). 
 */
static void
edge_intersect_fsteps(points, i, ex, ey)
	struct coordinate GPHUGE *points; /* the points array */
	int i;				/* line segment from point i-1 to point i */
	double *ex, *ey;		/* the point where it crosses an edge */
{
    /* global x_min, x_max, y_min, x_max */
    double ax = points[i-1].x;
    double ay = points[i-1].y;
    double bx = points[i].x;
    double by = points[i].y;

    if (points[i].type == INRANGE) {	/* from OUTRANGE to INRANG */
	    if (inrange(ax,x_min,x_max)) {
		*ex = ax;
		if (ay > y_max)
			*ey = y_max;
		else			/* y < y_min */
			*ey = y_min;
	    } else {
	    	*ey = by;
		if (bx > x_max)     
			*ex = x_max;
		else			/* x < x_min */
			*ex = x_min;
	    }
    } else {				/* from INRANGE to OUTRANGE */
	    if (inrange(by,y_min,y_max)) {
		*ey = by;
		if (bx > x_max)
			*ex = x_max;
		else			/* x < x_min */
			*ex = x_min;
	    } else {
	    	*ex = ax;
		if (by > y_max)     
			*ey = y_max;
		else			/* y < y_min */
			*ey = y_min;
	    }
    }
    return;
}

/* XXX - JG  */
/* double edge intersection algorithm for "steps" plot */
/* Given two points, both outside the plot, return the points where an 
 * edge of the plot intersects the line segments forming a step 
 * by the two points. There may be zero, one, two, or an infinite number
 * of intersection points. (One means an intersection at a corner, infinite
 * means overlaying the edge itself). We return FALSE when there is nothing
 * to draw (zero intersections), and TRUE when there is something to 
 * draw (the one-point case is a degenerate of the two-point case and we do 
 * not distinguish it - we draw it anyway).
 *
 * Recall that if P1 = (x1,y1) and P2 = (x2,y2), the step from  
 * P1 to P2 is drawn as two line segments: (x1,y1)->(x2,y1) and 
 * (x2,y1)->(x2,y2). 
 */
static TBOOLEAN				/* any intersection? */
two_edge_intersect_steps(points, i, lx, ly)
	struct coordinate GPHUGE *points; /* the points array */
	int i;				/* line segment from point i-1 to point i */
	double *lx, *ly;		/* lx[2], ly[2]: points where it crosses edges */
{
    /* global x_min, x_max, y_min, x_max */
    double ax = points[i-1].x;
    double ay = points[i-1].y;
    double bx = points[i].x;
    double by = points[i].y;

    if ( GPMAX(ax,bx) < x_min || GPMIN(ax,bx) > x_max || 
         GPMAX(ay,by) < y_min || GPMIN(ay,by) > y_max ||
         ( (ay  > y_max || ay < y_min)            &&
           (bx  > x_max || bx < x_min)  ) ) {
	return(FALSE);				
    } else if (inrange(ay,y_min,y_max) && inrange(bx,x_min,x_max)) {	/* corner of step inside plotspace */
    	*ly++ = ay;
	if (ax < x_min) 
		*lx++ = x_min;
	else 
		*lx++ = x_max;

	*lx++ = bx;
	if (by < y_min) 
		*ly++ = y_min;
	else 
		*ly++ = y_max;

	return(TRUE);
    } else if (inrange(ay,y_min,y_max)) {	/* cross plotspace in x-direction */
	*lx++ = x_min;
	*ly++ = ay;
	*lx++ = x_max;
	*ly++ = ay;
	return(TRUE);
    } else if (inrange(ax,x_min,x_max)) {	/* cross plotspace in y-direction */
	*lx++ = bx;
	*ly++ = y_min;
	*lx++ = bx;
	*ly++ = y_max;
	return(TRUE);
    } else
	return(FALSE);
}

/* XXX - HOE  */
/* double edge intersection algorithm for "fsteps" plot */
/* Given two points, both outside the plot, return the points where an 
 * edge of the plot intersects the line segments forming a step 
 * by the two points. There may be zero, one, two, or an infinite number
 * of intersection points. (One means an intersection at a corner, infinite
 * means overlaying the edge itself). We return FALSE when there is nothing
 * to draw (zero intersections), and TRUE when there is something to 
 * draw (the one-point case is a degenerate of the two-point case and we do 
 * not distinguish it - we draw it anyway).
 *
 * Recall that if P1 = (x1,y1) and P2 = (x2,y2), the step from  
 * P1 to P2 is drawn as two line segments: (x1,y1)->(x1,y2) and 
 * (x1,y2)->(x2,y2). 
 */
static TBOOLEAN				/* any intersection? */
two_edge_intersect_fsteps(points, i, lx, ly)
	struct coordinate GPHUGE *points; /* the points array */
	int i;		/* line segment from point i-1 to point i */
	double *lx, *ly; /* lx[2], ly[2]: points where it crosses edges */
{
    /* global x_min, x_max, y_min, x_max */
    double ax = points[i-1].x;
    double ay = points[i-1].y;
    double bx = points[i].x;
    double by = points[i].y;

    if ( GPMAX(ax,bx) < x_min || GPMIN(ax,bx) > x_max || 
         GPMAX(ay,by) < y_min || GPMIN(ay,by) > y_max ||
         ( (by  > y_max || by < y_min)            &&
           (ax  > x_max || ax < x_min)  ) ) {
	return(FALSE);				
    } else if (inrange(by,y_min,y_max) && inrange(ax,x_min,x_max)) {	/* corner of step inside plotspace */
    	*lx++ = ax;
	if (ay < y_min) 
		*ly++ = y_min;
	else 
		*ly++ = y_max;

	*ly = by;
	if (bx < x_min) 
		*lx = x_min;
	else 
		*lx = x_max;

	return(TRUE);
    } else if (inrange(by,y_min,y_max)) {	/* cross plotspace in x-direction */
	*lx++ = x_min;
	*ly++ = by;
	*lx = x_max;
	*ly = by;
	return(TRUE);
    } else if (inrange(ax,x_min,x_max)) {	/* cross plotspace in y-direction */
	*lx++ = ax;
	*ly++ = y_min;
	*lx = ax;
	*ly = y_max;
	return(TRUE);
    } else
	return(FALSE);
}

/* double edge intersection algorithm */
/* Given two points, both outside the plot, return
 * the points where an edge of the plot intersects the line segment defined 
 * by the two points. There may be zero, one, two, or an infinite number
 * of intersection points. (One means an intersection at a corner, infinite
 * means overlaying the edge itself). We return FALSE when there is nothing
 * to draw (zero intersections), and TRUE when there is something to 
 * draw (the one-point case is a degenerate of the two-point case and we do 
 * not distinguish it - we draw it anyway).
 */
static TBOOLEAN				/* any intersection? */
two_edge_intersect(points, i, lx, ly)
	struct coordinate GPHUGE *points; /* the points array */
	int i;				/* line segment from point i-1 to point i */
	double *lx, *ly;		/* lx[2], ly[2]: points where it crosses edges */
{
    /* global x_min, x_max, y_min, x_max */
    int count;
    double ix = points[i-1].x;
    double iy = points[i-1].y;
    double ox = points[i].x;
    double oy = points[i].y;
    double t[4];
    double swap;
    double t_min, t_max;
#if 0
    fprintf(stderr, "\ntwo_edge_intersect (%g, %g) and (%g, %g) : ",
	    points[i-1].x, points[i-1].y,
	    points[i].x, points[i].y);
#endif
    /* nasty degenerate cases, effectively drawing to an infinity point (?)
       cope with them here, so don't process them as a "real" OUTRANGE point 

       If more than one coord is -VERYLARGE, then can't ratio the "infinities"
       so drop out by returning FALSE */
    
    count = 0;
    if(ix == -VERYLARGE) count++;
    if(ox == -VERYLARGE) count++;
    if(iy == -VERYLARGE) count++;
    if(oy == -VERYLARGE) count++;

    /* either doesn't pass through graph area *or* 
       can't ratio infinities to get a direction to draw line, so simply return(FALSE) */
    if(count > 1){
#if 0
      fprintf(stderr, "\tA\n");
#endif
      return(FALSE);
    }

    if(ox == -VERYLARGE || ix == -VERYLARGE)
      {
	if(ix == -VERYLARGE)
	  {
	    /* swap points so ix/iy don't have a -VERYLARGE component */
	    swap = ix;ix = ox;ox = swap;
	    swap = iy;iy = oy;oy = swap;
	  }

	/* check actually passes through the graph area */
	if(ix > x_max && inrange(iy, y_min, y_max))
	  {
	    lx[0] = x_min;
	    ly[0] = iy;

	    lx[1] = x_max;
	    ly[1] = iy;
#if 0
    fprintf(stderr, "(%g %g) -> (%g %g)", 
	    lx[0], ly[0], lx[1], ly[1]);
#endif
	    return(TRUE);
	  }
	else {
#if 0
	  fprintf(stderr, "\tB\n");
#endif
	  return(FALSE);
	}
      }

    if(oy == -VERYLARGE || iy == -VERYLARGE)
      {
	if(iy == -VERYLARGE)
	  {
	    /* swap points so ix/iy don't have a -VERYLARGE component */
	    swap = ix;ix = ox;ox = swap;
	    swap = iy;iy = oy;oy = swap;
	  }

	/* check actually passes through the graph area */
	if(iy > y_max && inrange(ix, x_min, x_max))
	  {
	    lx[0] = ix;
	    ly[0] = y_min;

	    lx[1] = ix;
	    ly[1] = y_max;
#if 0
    fprintf(stderr, "(%g %g) -> (%g %g)", 
	    lx[0], ly[0], lx[1], ly[1]);
#endif
	    return(TRUE);
	  }
	else {
#if 0
	  fprintf(stderr, "\tC\n");
#endif
	  return(FALSE);
	}
      }

    /*
     * Special horizontal/vertical, etc. cases are checked and remaining
     * slant lines are checked separately.
     *
     * The slant line intersections are solved using the parametric form
     * of the equation for a line, since if we test x/y min/max planes explicitly
     * then e.g. a  line passing through a corner point (x_min,y_min) 
     * actually intersects 2 planes and hence further tests would be required 
     * to anticipate this and similar situations.
     */

    /*
     * Can have case (ix == ox && iy == oy) as both points OUTRANGE
     */
    if(ix == ox && iy == oy)
      {
	/* but as only define single outrange point, can't intersect graph area */
	return(FALSE);
      }

    if(ix == ox) {
	/* line parallel to y axis */
	
	/* x coord must be in range, and line must span both y_min and y_max */
	/* note that spanning y_min implies spanning y_max, as both points OUTRANGE */
	if(!inrange(ix, x_min, x_max))
	  {
	    return(FALSE);
	  }

	if (inrange(y_min, iy, oy)) {
	  lx[0] = ix;
	  ly[0] = y_min;

	  lx[1] = ix;
	  ly[1] = y_max;
#if 0
	  fprintf(stderr, "(%g %g) -> (%g %g)", 
		  lx[0], ly[0], lx[1], ly[1]);
#endif
	  return(TRUE);
	} else
	  return(FALSE);
      }

    if(iy == oy) {
      /* already checked case (ix == ox && iy == oy) */
      
      /* line parallel to x axis */
	/* y coord must be in range, and line must span both x_min and x_max */
	/* note that spanning x_min implies spanning x_max, as both points OUTRANGE */
	if(!inrange(iy, y_min, y_max))
	  {
	    return(FALSE);
	  }

	if (inrange(x_min, ix, ox)) {
	  lx[0] = x_min;
	  ly[0] = iy;

	  lx[1] = x_max;
	  ly[1] = iy;
#if 0
    fprintf(stderr, "(%g %g) -> (%g %g)", 
	    lx[0], ly[0], lx[1], ly[1]);
#endif
	  return(TRUE);
	} else
	  return(FALSE);
      }

    /* nasty 2D slanted line in an xy plane */

    /*
      Solve parametric equation
      
      (ix, iy) + t (diff_x, diff_y)

      where 0.0 <= t <= 1.0 and

      diff_x = (ox - ix);
      diff_y = (oy - iy);
      */

      t[0] = (x_min - ix)/(ox - ix);
      t[1] = (x_max - ix)/(ox - ix);
      
      if(t[0] > t[1]) {
	swap = t[0];t[0] = t[1];t[1] = swap;
      }

      t[2] = (y_min - iy)/(oy - iy);
      t[3] = (y_max - iy)/(oy - iy);
      
      if(t[2] > t[3]) {
	swap = t[2];t[2] = t[3];t[3] = swap;
      }

      t_min = GPMAX(GPMAX(t[0],t[2]),0.0);
      t_max = GPMIN(GPMIN(t[1],t[3]),1.0);
      
      if(t_min > t_max)
	return(FALSE);

      lx[0] = ix + t_min * (ox - ix);
      ly[0] = iy + t_min * (oy - iy);
      
      lx[1] = ix + t_max * (ox - ix);
      ly[1] = iy + t_max * (oy - iy);
      
      /*
       * Can only have 0 or 2 intersection points -- only need test one coord
       */
      if(inrange(lx[0], x_min, x_max) && 
	 inrange(ly[0], y_min, y_max))
	{
#if 0
	  fprintf(stderr, "(%g %g) -> (%g %g)", 
		  lx[0], ly[0], lx[1], ly[1]);
#endif
	  return(TRUE);
	}
      
    return(FALSE);
}

/* justify ticplace to a proper date-time value */
double
time_tic_just(level,ticplace)
int level;
double ticplace;
{
	struct tm tm;

	if (level <= 0) {
		return(ticplace);
	}
	ggmtime(&tm,(double)ticplace);
  	if (level > 0) { /* units of minutes */
  		if ( tm.tm_sec > 50 ) 
  			tm.tm_min ++;
  		tm.tm_sec = 0;
  	}
  	if (level > 1) { /* units of hours */
  		if ( tm.tm_min > 50 )
  			tm.tm_hour ++;
  		tm.tm_min = 0;
  	}
  	if (level > 2) { /* units of days */
		if ( tm.tm_hour > 14 ) {
			tm.tm_hour = 0;
			tm.tm_mday = 0;
  			tm.tm_yday ++;
 			ggmtime(&tm,(double)gtimegm(&tm));
  		} else if (tm.tm_hour > 7 ) {
  			tm.tm_hour = 12;
  		} else if (tm.tm_hour > 3 ) {
  			tm.tm_hour = 6;
  		} else {
  			tm.tm_hour = 0;
  		}
  	}
  	/* skip it, I have not bothered with weekday so far */
	if (level > 4) { /* units of month */
		if ( tm.tm_mday > 25 ) {
			tm.tm_mon ++;
			if (tm.tm_mon > 11) {
				tm.tm_year++;
				tm.tm_mon = 0;
			}
		}
		tm.tm_mday = 1;
	}
	if (level > 5) {
		if ( tm.tm_mon >= 7 )
			tm.tm_year ++;
		tm.tm_mon = 0;
	}
	ticplace = (double) gtimegm(&tm);
	ggmtime(&tm,(double)gtimegm(&tm));
	return(ticplace);
}

/* make smalltics for time-axis */
double
make_ltic(tlevel,incr)
int tlevel;
double incr;
{
	double tinc;
	tinc = 0;
	if ( tlevel < 0 ) tlevel = 0;
	switch ( tlevel ) {
		case 0: 
		case 1: {
			if ( incr >= 20 )
				tinc = 10;
			if ( incr >= 60 )
				tinc = 30;
			if ( incr >= 2*60 )
				tinc = 60;
			if ( incr >= 5*60 )
				tinc = 2*60;
			if ( incr >= 10*60 )
				tinc = 5*60;
			if ( incr >= 20*60 )
				tinc = 10*60;
			break;
			}
		case 2: {
			if ( incr >= 20*60 )
				tinc = 10*60;
			if ( incr >= 3600 )
				tinc = 30*60;
			if ( incr >= 2*3600 )
				tinc = 3600;
			if ( incr >= 5*3600 )
				tinc = 2*3600;
			if ( incr >= 10*3600 )
				tinc = 5*3600;
			if ( incr >= 20*3600 )
				tinc = 10*3600;
			break;
			}
		case 3: {
			if ( incr > 2*3600 )
				tinc = 3600;
			if ( incr > 4*3600 )
				tinc = 2*3600;
			if ( incr > 7*3600 )
				tinc = 3*3600;
			if ( incr > 13*3600 )
				tinc = 6*3600;
			if ( incr > DAY_SEC )
				tinc = 12*3600;
			if ( incr > 2*DAY_SEC )
				tinc = DAY_SEC;
			break;
			}
		case 4: { /* week: tic per day */
			if ( incr > 2*DAY_SEC )
				tinc = DAY_SEC;
			if ( incr > 7*DAY_SEC )
				tinc = 7*DAY_SEC;
			break;
			}
		case 5: { /* month */
			if ( incr > 2*DAY_SEC )
				tinc = DAY_SEC;
			if ( incr > 15*DAY_SEC )
				tinc = 10*DAY_SEC;
			if ( incr > 2*MON_SEC )
				tinc = MON_SEC;
			if ( incr > 6*MON_SEC )
				tinc = 3*MON_SEC;
			if ( incr > 2*YEAR_SEC )
				tinc = YEAR_SEC;
			break;
			}
		case 6: { /* year */
			if ( incr > 2*MON_SEC )
				tinc = MON_SEC;
			if ( incr > 6*MON_SEC )
				tinc = 3*MON_SEC;
			if ( incr > 2*YEAR_SEC )
				tinc = YEAR_SEC;
			if ( incr > 10*YEAR_SEC )
				tinc = 5*YEAR_SEC;
			if ( incr > 50*YEAR_SEC ) 
				tinc = 10*YEAR_SEC;
			if ( incr > 100*YEAR_SEC ) 
				tinc = 20*YEAR_SEC;
			if ( incr > 200*YEAR_SEC ) 
				tinc = 50*YEAR_SEC;
			if ( incr > 300*YEAR_SEC ) 
				tinc = 100*YEAR_SEC;
			break;
			}
	}
	return(tinc);
}


void write_multiline(x,y,text,hor,vert,angle, font)
unsigned int x,y;
char *text;
enum JUSTIFY hor; /* horizontal ... */
int vert; /* ... and vertical just - text in hor direction despite angle */
int angle; /* assume term has already been set for this */
char *font; /* NULL or "" means use default */
{
	/* assumes we are free to mangle the text */
    register struct termentry *t = term;
	char *p;
	if (vert != JUST_TOP) {
		/* count lines and adjust y */
		int lines=0; /* number of linefeeds - one fewer than lines */
		for (p=text; *p; ++p)
			if (*p=='\n') ++lines;
		if (angle)
			x -= (vert*lines*t->v_char)/2;
		else
			y += (vert*lines*t->v_char)/2;
	}

	if (font && *font)
		(*t->set_font)(font);
		

	for(;;) { /* we will explicitly break out */
	
		if ( (p=strchr(text, '\n')) != NULL )
			*p=0; /* terminate the string */

		if ((*t->justify_text)(hor)) {
			(*t->put_text)(x, y, text);
		} else {
			int fix=hor*(t->h_char)*strlen(text)/2;
			if (angle)
				(*t->put_text)(x, y-fix, text);
			else
				(*t->put_text)(x - fix, y, text);
		}
		if (angle)
			x += t->v_char;
		else
			y -= t->v_char;

		if (!p)
			break;

		text = p+1;
	} /* unconditional branch back to the for(;;) - just a goto ! */

	if (font && *font)
		(*t->set_font)(default_font);
}

/* display a x-axis ticmark - called by gen_ticks */
/* also uses global tic_start, tic_direction, tic_text and tic_just */
static void xtick2d_callback(axis, place, text, grid)
int axis;
double place;
char *text;
struct lp_style_type grid; /* linetype or -2 for no grid */
{
    register struct termentry *t = term;
    /* minitick if text is NULL - beware - h_tic is unsigned */
    int ticsize = tic_direction*(int)(t->v_tic)*(text ? ticscale : miniticscale);
    unsigned int x=map_x(place);

  if (grid.l_type > -2) {
    term_apply_lp_properties(&grid);
    if (polar_grid_angle) {
	double x=place, y=0, s=sin(0.1), c=cos(0.1);
	int i;
	if (place > largest_polar_circle) largest_polar_circle=place;
	else if (-place > largest_polar_circle) largest_polar_circle=-place;
	clip_move(map_x(x),map_y(0));
	for (i=1;i<=63 /* 2pi/0.1*/; ++i) {
		{
			/* cos(t+dt)=cos(t)cos(dt)-sin(t)cos(dt) */
			double tx=x*c-y*s;
			/* sin(t+dt)=sin(t)cos(dt)+cos(t)sin(dt) */
			y=y*c+x*s;
			x=tx;
		}
		clip_vector(map_x(x), map_y(y));
	}
    } else {
	    if (lkey && key_yt > ybot && x < key_xr && x > key_xl ) {
		if ( key_yb > ybot ) {
		     (*t->move)(x, ybot);
		     (*t->vector)(x, key_yb);
		}
		if ( key_yt < ytop ) {
		     (*t->move)(x,key_yt);
		     (*t->vector)(x, ytop);
		}
	    } else {
	        (*t->move)(x, ybot);
	        (*t->vector)(x, ytop);
	    }
    }
    term_apply_lp_properties(&border_lp); /* border linetype */
  }

    /* we precomputed tic posn and text posn in global vars */

    (*t->move)(x, tic_start);
    (*t->vector)(x,tic_start+ticsize);

    if (tic_mirror >= 0) {
	(*t->move)(x, tic_mirror);
	(*t->vector)(x,tic_mirror-ticsize);
    }
 	
    if (text)
    	write_multiline(x, tic_text, text, tic_hjust, tic_vjust, rotate_tics, NULL);
}

/* display a y-axis ticmark - called by gen_ticks */
/* also uses global tic_start, tic_direction, tic_text and tic_just */
static void ytick2d_callback(axis, place, text, grid)
int axis;
double place;
char *text;
struct lp_style_type grid; /* linetype or -2 */
{
    register struct termentry *t = term;
    /* minitick if text is NULL - v_tic is unsigned */
    int ticsize = tic_direction*(int)(t->h_tic)*(text ? ticscale : miniticscale);
    unsigned int y=map_y(place);
  if (grid.l_type > -2) {
    term_apply_lp_properties(&grid);
    if (polar_grid_angle) {
	double x=0, y=place, s=sin(0.1), c=cos(0.1);
	int i;
	if (place > largest_polar_circle) largest_polar_circle=place;
	else if (-place > largest_polar_circle) largest_polar_circle=-place;
	clip_move(map_x(x),map_y(y));
	for (i=1;i<=63 /* 2pi/0.1*/; ++i) {
		{
			/* cos(t+dt)=cos(t)cos(dt)-sin(t)cos(dt) */
			double tx=x*c-y*s;
			/* sin(t+dt)=sin(t)cos(dt)+cos(t)sin(dt) */
			y=y*c+x*s;
			x=tx;
		}
		clip_vector( map_x(x), map_y(y) );
	}
    } else {
	     if (lkey && y < key_yt && y > key_yb && key_xl < xright /* catch TOUT */ ) {
		if ( key_xl > xleft ) {
		     (*t->move)(xleft, y);
		     (*t->vector)(key_xl, y);
		}
		if ( key_xr < xright ) {
		     (*t->move)(key_xr, y);
		     (*t->vector)(xright, y);
		}
	     } else {
	        (*t->move)(xleft, y);
	        (*t->vector)(xright, y);
	     }
    }
    term_apply_lp_properties(&border_lp); /* border linetype */
  }
  
    /* we precomputed tic posn and text posn */

    (*t->move)(tic_start, y);
    (*t->vector)(tic_start+ticsize, y);

    if (tic_mirror >= 0) {
	(*t->move)(tic_mirror, y);
	(*t->vector)(tic_mirror-ticsize, y);
    }
    
    if (text)
    	write_multiline(tic_text, y, text, tic_hjust, tic_vjust, rotate_tics, NULL);
}

int 
label_width(str,lines)
char *str;
int *lines;
{
	char lab[MAX_LINE_LEN+1], *s, *e;
	int mlen, len, l;

	l = mlen = len = 0;
	sprintf(lab,"%s\n",str);
	s = lab;
	while( (e=(char *)strchr(s,'\n')) != NULL ) { /* HBB 980308: quiet BC-3.1 warning */
		*e = '\0';
		len = strlen(s); /* = e-s ? */
		if ( len > mlen ) mlen = len;
		if ( len || l ) l++;
		s = ++e;
	}
	/* lines=NULL => not interested - div */
	if (lines) *lines = l;
	return(mlen);
}


void setup_tics(axis,ticdef, format, max)
int axis;
struct ticdef *ticdef;
char *format;
int max; /* approx max number of slots available */
{
	double tic=0; /* HBB: shut up gcc -Wall */

	int fixmin=(auto_array[axis] & 1) != 0;
	int fixmax=(auto_array[axis] & 2) != 0;

	if (ticdef->type == TIC_SERIES) {
		ticstep[axis] = tic = ticdef->def.series.incr;
		fixmin &=  (ticdef->def.series.start == -VERYLARGE);
		fixmax &=  (ticdef->def.series.end   ==  VERYLARGE);
	} else if (ticdef->type == TIC_COMPUTED) {
		ticstep[axis] = tic = make_tics(axis, max);
	} else {
		fixmin = fixmax = 0; /* user-defined, day or month */
	}
	
	if (fixmin) {
		if (min_array[axis] < max_array[axis])
			min_array[axis] = tic * floor(min_array[axis]/tic);
		else
			min_array[axis] = tic * ceil(min_array[axis]/tic);
	}

	if (fixmax) {
		if (min_array[axis] < max_array[axis])
			max_array[axis] = tic * ceil(max_array[axis]/tic);
		else
			max_array[axis] = tic * floor(max_array[axis]/tic);
	}
			
	if (datatype[axis] == TIME && format_is_numeric[axis])
		/* invent one for them */
		timetic_format(axis, min_array[axis],max_array[axis]);
	else
		strcpy(ticfmt[axis], format);
}

/*{{{  mant_exp - split into mantissa and/or exponent*/
static void mant_exp(log_base, x, scientific, m,p)
double log_base, x;
int scientific; /* round to power of 3 */
double *m;
int *p; /* results */
{
	int sign=1;
	double l10;
	int power;
	/*{{{  check 0*/
	if (x==0) {
		if (m) *m=0;
		if (p) *p=0;
		return;
	}
	/*}}}*/
	/*{{{  check -ve*/
	if (x<0) {
		sign=(-1);
		x=(-x);
	}
	/*}}}*/

	l10=log10(x) / log_base;
	power = floor(l10);
	if (scientific) {
		power = 3*floor(power/3.0);
	}

	if (m) *m = sign * pow( 10.0, (l10-power)*log_base );
	if (p) *p = power;
}
/*}}}*/
	

/*{{{  gprintf*/
/* extended sprintf */
static void gprintf(dest, format, log_base, x)
char *dest, *format;
double log_base, x; /* we print one number in a number of different formats */
{
#define LOC_PI 3.14159265358979323846   /* local definition of PI */
	char temp[MAX_LINE_LEN];
	char *t;

	for (;;) {
		/*{{{  copy to dest until %*/
		while (*format != '%')
			if (!(*dest++ = *format++))
				return;  /* end of format */
		/*}}}*/

		/*{{{  check for %%*/
		if (format[1]=='%') {
			*dest++ = '%';
			format += 2;
			continue;
		}
		/*}}}*/

		/*{{{  copy format part to temp, excluding conversion character*/
		t=temp;
		*t++ = '%';
		/* dont put isdigit first since sideeffect in macro is bad */
		while (*++format=='.' || isdigit(*format) || *format=='-' || *format=='+' || *format==' ')
			*t++ = *format;
		/*}}}*/

		/*{{{  convert conversion character*/
		switch (*format) {
			/*{{{  x and o*/
			case 'x': case 'X':
			case 'o': case 'O':
				t[0]=*format++;
				t[1]=0;
				sprintf(dest, temp, (int) x);
				dest += strlen(dest);
				break;
			/*}}}*/
			/*{{{  e, f and g*/
			case 'e': case 'E':
			case 'f': case 'F':
			case 'g': case 'G':
				t[0]=*format++;
				t[1]=0;
				sprintf(dest, temp, x);
				dest += strlen(dest);
				break;
			/*}}}*/
			/*{{{  l*/
			case 'l':
				{
					double mantissa;
					mant_exp(log_base, x, 0, &mantissa, NULL);
					t[0]='f';
					t[1]=0;
					sprintf(dest, temp, mantissa);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  t*/
			case 't':
				{
					double mantissa;
					mant_exp(1.0, x, 0, &mantissa, NULL);
					t[0]='f';
					t[1]=0;
					sprintf(dest, temp, mantissa);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  s*/
			case 's':
				{
					double mantissa;
					mant_exp(1.0, x, 1, &mantissa, NULL);
					t[0]='f';
					t[1]=0;
					sprintf(dest, temp, mantissa);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  L*/
			case 'L':
				{
					int power;
					mant_exp(log_base, x, 0, NULL, &power);
					t[0]='d';
					t[1]=0;
					sprintf(dest, temp, power);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  T*/
			case 'T':
				{
					int power;
					mant_exp(1.0, x, 0, NULL, &power);
					t[0]='d';
					t[1]=0;
					sprintf(dest, temp, power);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  S*/
			case 'S':
				{
					int power;
					mant_exp(1.0, x, 1, NULL, &power);
					t[0]='d';
					t[1]=0;
					sprintf(dest, temp, power);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			/*{{{  c*/
			case 'c':
				{
					int power;
					mant_exp(1.0, x, 1, NULL, &power);
					t[0]='c';
					t[1]=0;
					power = power / 3 + 6; /* -18 -> 0, 0 -> 6, +18 -> 12, ... */
					if (power>=0 && power<=12)
							sprintf(dest, temp, "afpnum kMGTPE"[power]);
					else
					{
						/* please extend the range ! */
			                                     /* name  power   name  power
			                                        -------------------------
			                                        atto   -18    Exa    18
			                                        femto  -15    Peta   15
			                                        pico   -12    Tera   12
			                                        nano    -9    Giga    9
			                                        micro   -6    Mega    6
			                                        milli   -3    kilo    3   */
			
						/* for the moment, print e+21 for example */
						sprintf(dest, "e%+02d", (power-6)*3);
					}
					
					dest += strlen(dest);
					++format;
					break;
				}
			case 'P':
				{
					t[0]='f';
					t[1]=0;
					sprintf(dest, temp, x/LOC_PI);
					dest += strlen(dest);
					++format;
					break;
				}
			/*}}}*/
			default:
				int_error("Bad format character", NO_CARET);
		}
		/*}}}*/
	}
}
#undef LOC_PI   /* local definition of PI */
/*}}}*/

/*{{{  gen_tics*/
/* uses global arrays ticstep[], ticfmt[], min_array[], max_array[],
 * auto_array[], log_array[], log_base_array[]
 * we use any of GRID_X/Y/X2/Y2 and  _MX/_MX2/etc - caller is expected
 * to clear the irrelevent fields from global grid bitmask
 * note this is also called from graph3d, so we need GRID_Z too
 */

void gen_tics(axis, def, grid, minitics, minifreq, callback)
int axis; /* FIRST_X_AXIS, etc */
struct ticdef *def; /* tic defn */
int grid; /* GRID_X | GRID_MX etc */
int minitics; /* minitics - off/default/auto/explicit */
double minifreq; /* frequency */
tic_callback callback;  /* fn to call to actually do the work */
{
    /* seperate main-tic part of grid */
    struct lp_style_type lgrd, mgrd;

    memcpy(&lgrd, &grid_lp,sizeof(struct lp_style_type));
    memcpy(&mgrd,&mgrid_lp,sizeof(struct lp_style_type));
    lgrd.l_type= (grid&(GRID_X|GRID_Y|GRID_X2|GRID_Y2|GRID_Z)) ? grid_lp.l_type : -2;
    mgrd.l_type= (grid&(GRID_MX|GRID_MY|GRID_MX2|GRID_MY2|GRID_MZ)) ? mgrid_lp.l_type : -2;

    if (def->type==TIC_USER) {  /* special case */
	/*{{{  do user tics then return*/
	struct ticmark *mark = def->def.user;
	double uncertain=(max_array[axis]-min_array[axis])/10;
	double ticmin=min_array[axis]-SIGNIF*uncertain;
	double internal_max=max_array[axis]+SIGNIF*uncertain;
	double log_base = log_array[axis] ? log10(base_array[axis]) : 1.0;
	
	/* polar labels always +ve, and if rmin has been set, they are
	 * relative to rmin. position is as user specified, but must
	 * be translated. I dont think it will work at all for
	 * log scale, so I shan't worry about it !
	 */
	double polar_shift = (polar && !(autoscale_r & 1)) ? rmin : 0;
	
	for (mark=def->def.user; mark; mark=mark->next) {
		char label[64];
		double internal = log_array[axis] ? log(mark->position)/log_base_array[axis] : mark->position;
	
		internal -= polar_shift;
	
		if (!inrange(internal, ticmin, internal_max))
			continue;
 
		if  (datatype[axis] == TIME)
			gstrftime(label, 24, mark->label ? mark->label : ticfmt[axis], mark->position);
		else
			gprintf(label, mark->label ? mark->label : ticfmt[axis], log_base, mark->position);
 
		(*callback)(axis, internal, label, lgrd);
	}
	
	return; /* NO MINITICS FOR USER-DEF TICS */
	/*}}}*/
    }

    /* series-tics
     * need to distinguish user co-ords from internal co-ords.
     * - for logscale, internal=log(user), else internal=user
     *
     * The minitics are a bit of a drag - we need to distinuish
     * the cases step>1 from step==1.
     * If step=1, we are looking at 1,10,100,1000 for example, so
     * minitics are 2,5,8, ...  - done in user co-ordinates
     * If step>1, we are looking at 1,1e6,1e12 for example, so
     * minitics are 10,100,1000,... - done in internal co-ords
     */

    {
	double tic; /* loop counter */
	double internal; /* in internal co-ords */
	double user;     /* in user co-ords */
	double start,step,end;
	double lmin=min_array[axis], lmax=max_array[axis];
	double internal_min, internal_max;	/* to allow for rounding errors */
	double ministart=0, ministep=1, miniend=1; /* internal or user - depends on step */

	/* gprintf uses log10() of base - log_base_array is log() */
	double log_base = log_array[axis] ? log10(base_array[axis]) : 1.0;

	if (lmax < lmin) {
		/* hmm - they have set reversed range for some reason */
		double temp=lmin; lmin=lmax; lmax=temp;
	}
	
	/*{{{  choose start, step and end*/
	switch (def->type) {
		case TIC_SERIES:
			if (log_array[axis]) {
				/* we can tolerate start<=0 if step and end > 0 */
				if (def->def.series.end <= 0 ||
				    def->def.series.incr <= 0)
					return; /* just quietly ignore */
				step=log(def->def.series.incr)/log_base_array[axis];
				end=log(def->def.series.end)/log_base_array[axis];
				start=def->def.series.start > 0 ?
				      log(def->def.series.start) / log_base_array[axis] :
						step;
			} else {
				start=def->def.series.start;
				step=def->def.series.incr;
				end=def->def.series.end;
				if (start==-VERYLARGE)
					start=step*floor(lmin/step);
				if (end == VERYLARGE)
					end=step*ceil(lmax/step);
			}
			break;
		case TIC_COMPUTED:
			/* round to multiple of step */
			start=ticstep[axis]*floor(lmin/ticstep[axis]);
			step=ticstep[axis];
			end=ticstep[axis]*ceil(lmax/ticstep[axis]);
			break;
		case TIC_MONTH:
			start=floor(lmin);
			end=ceil(lmax);
			step=floor((end-start)/12);
			if (step<1) step=1;
			break;
		case TIC_DAY:
			start=floor(lmin);
			end=ceil(lmax);
			step=floor((end-start)/14);
			if (step<1) step=1;
			break;
		default:
			graph_error("Internal error : unknown tic type");
			return; /* avoid gcc -Wall warning about start */
	}
	/*}}}*/

	/*{{{  ensure ascending order*/
	if (end < start) {
		double temp;
		temp=end; end=start; start=temp;
	}
	step=fabs(step);
	/*}}}*/

	if (minitics) {
		/*{{{  figure out ministart, ministep, miniend*/
		if (minitics==MINI_USER) {
			/* they have said what they want */
			if (minifreq<=0)
				minitics=0;  /* not much else we can do */
			else if (log_array[axis]) {
				ministart = ministep=step/minifreq * base_array[axis];
				miniend = step * base_array[axis];
			}
			else {
				ministart = ministep=step/minifreq;
				miniend = step;
			}
		} else if (log_array[axis]) {
			if (step>1.5) { /* beware rounding errors */
				/*{{{  10,100,1000 case*/
				/* no more than five minitics */
				ministart = ministep = (int) (0.2*step);
				if (ministep < 1)
					ministart = ministep = 1;
				miniend = step;
				/*}}}*/
			} else {
				/*{{{  2,5,8 case*/
				miniend = base_array[axis];
				if (end-start >= 10)
					minitics=0; /* none */
				else if (end-start >= 5) {
					ministart=2;
					ministep=3;
				} else {
					ministart=2;
					ministep=1;
				}
				/*}}}*/
			}
		} else if (datatype[axis]==TIME) {
			ministart=ministep=make_ltic(timelevel[axis], step);
			miniend=step*0.9;
		} else if (minitics==MINI_AUTO) {
			ministart=ministep=0.1*step;
			miniend=step;
		} else
			minitics=0;
		
		if (ministep<=0)
			minitics=0; /* dont get stuck in infinite loop */
		/*}}}*/
	}
	
	/*{{{  a few tweaks and checks*/
	/* watch rounding errors */
	end += SIGNIF*step;
	internal_max=lmax + step*SIGNIF;
	internal_min=lmin - step*SIGNIF;
	
	if (step==0)
		return;  /* just quietly ignore them ! */
	/*}}}*/
	
	for (tic=start; tic<=end; tic+=step) {
		/*{{{  calc internal and user co-ords*/
		if (!log_array[axis]) {
			internal = datatype[axis]==TIME ? time_tic_just(timelevel[axis],tic): tic;
			user=CheckZero(internal,step);
		} else {
			/* log scale => dont need to worry about zero ? */
			internal=tic;
			user=pow(base_array[axis], internal);
		}
		/*}}}*/
		if (internal > internal_max)
			break; /* gone too far - end of series = VERYLARGE perhaps */
		if (internal >= internal_min) {
			/* continue; */ /* maybe minitics!!!. user series starts below min ? */

			/*{{{  draw tick via callback*/
			switch(def->type) {
				case TIC_DAY : {
					int d = (long)floor(user+0.5) % 7;
					if (d<0) d+=7;
					(*callback)(axis,internal, abbrev_day_names[d], lgrd);
					break;
				}
				case TIC_MONTH: {
					int m = (long)floor(user+0.5) % 12;
					if (m<0) m+=12;
					(*callback)(axis, internal, abbrev_month_names[m], lgrd);
					break;
				}
				default: { /* comp or series */
					char label[64];
					if ( datatype[axis] == TIME ) {
						/* If they are doing polar time plot, good luck to them */
						gstrftime(label,24,ticfmt[axis],(double)user);
					} else if (polar) {
						/* if rmin is set, we stored internally with r-rmin */
						double r = fabs(user) + (autoscale_r&1 ? 0 : rmin);
						gprintf(label, ticfmt[axis], log_base, r);
					} else {
						gprintf(label, ticfmt[axis], log_base, user);
					}
					(*callback)(axis, internal, label, lgrd);
				}
			}
			/*}}}*/

		}
		if (minitics) {
			/*{{{  process minitics*/
			double mplace, mtic;
			for (mplace=ministart; mplace < miniend; mplace += ministep) {
				if ( datatype[axis] == TIME )
					mtic = time_tic_just(timelevel[axis]-1,internal+mplace);
				else
					mtic=internal + (log_array[axis]&&step<=1.5 ? log(mplace)/log_base_array[axis] : mplace);
				if (inrange(mtic, internal_min, internal_max) &&
                inrange(mtic, start-step*SIGNIF, end+step*SIGNIF))
					 callback(axis, mtic, NULL, mgrd);
			}
			/*}}}*/
		} 
	}
    }
}
/*}}}*/

/*{{{  map_position*/
static void map_position(pos, x, y, what)
struct position *pos;
unsigned int *x, *y;
char *what;
{
	switch (pos->scalex) {
		case first_axes:
			{
				double xx=LogScale(pos->x, log_array[FIRST_X_AXIS], log_base_array[FIRST_X_AXIS], what, "x");
				*x=xleft + (xx-min_array[FIRST_X_AXIS])*scale[FIRST_X_AXIS]+0.5;
				break;
			}
		case second_axes:
			{
				double xx=LogScale(pos->x, log_array[SECOND_X_AXIS], log_base_array[SECOND_X_AXIS], what, "x");
				*x=xleft + (xx-min_array[SECOND_X_AXIS])*scale[SECOND_X_AXIS]+0.5;
				break;
			}
		case graph:
			{
				*x=xleft + pos->x*(xright-xleft) + 0.5;
				break;
			}
		case screen:
			{
				register struct termentry *t = term;
				*x = pos->x*(t->xmax) + 0.5;
				break;
			}
	}
	switch (pos->scaley) {
		case first_axes:
			{
				double yy=LogScale(pos->y, log_array[FIRST_Y_AXIS], log_base_array[FIRST_Y_AXIS], what, "y");
				*y=ybot  + (yy-min_array[FIRST_Y_AXIS])*scale[FIRST_Y_AXIS]+0.5;
				return;
			}
		case second_axes:
			{
				double yy=LogScale(pos->y, log_array[SECOND_Y_AXIS], log_base_array[SECOND_Y_AXIS], what, "y");
				*y=ybot  + (yy-min_array[SECOND_Y_AXIS])*scale[SECOND_Y_AXIS]+0.5;
				return;
			}
		case graph:
			{
				*y=ybot  + pos->y*(ytop-ybot) + 0.5;
				return;
			}
		case screen:
			{
				register struct termentry *t = term;
				*y = pos->y*(t->ymax) + 0.5;
				return;				
			}
	}
}
/*}}}*/
