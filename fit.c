#ifndef lint
static char *RCSid = "$Id: fit.c,v 1.58 1998/04/14 00:15:19 drd Exp $";
#endif

/*
 *	Nonlinear least squares fit according to the
 *	Marquardt-Levenberg-algorithm
 *
 *	added as Patch to Gnuplot (v3.2 and higher)
 *	by Carsten Grammes
 *	Experimental Physics, University of Saarbruecken, Germany
 *
 *	Internet address: cagr@rz.uni-sb.de
 *
 *	Copyright of this module:  1993, 1998  Carsten Grammes
 *
 *	Permission to use, copy, and distribute this software and its
 *	documentation for any purpose with or without fee is hereby granted,
 *	provided that the above copyright notice appear in all copies and
 *	that both that copyright notice and this permission notice appear
 *	in supporting documentation.
 *
 *	This software is provided "as is" without express or implied warranty.
 *
 *	930726:     Recoding of the Unix-like raw console I/O routines by:
 *		    Michele Marziani (marziani@ferrara.infn.it)
 * drd: start unitialised variables at 1 rather than NEARLY_ZERO
 *  (fit is more likely to converge if started from 1 than 1e-30 ?)
 *
 * HBB (Broeker@physik.rwth-aachen.de) : fit didn't calculate the errors
 * in the 'physically correct' (:-) way, if a third data column containing
 * the errors (or 'uncertainties') of the input data was given. I think
 * I've fixed that, but I'm not sure I really understood the M-L-algo well
 * enough to get it right. I deduced my change from the final steps of the
 * equivalent algorithm for the linear case, which is much easier to
 * understand. (I also made some minor, mostly cosmetic changes)
 *
 * HBB (again): added error checking for negative covar[i][i] values and
 * for too many parameters being specified.
 *
 * drd: allow 3d fitting. Data value is now called fit_z internally,
 * ie a 2d fit is z vs x, and a 3d fit is z vs x and y.
 *
 * Lars Hecking : review update command, for VMS in particular, where
 * it is not necessary to rename the old file.
 *
 * HBB, 971023: lifted fixed limit on number of datapoints, and number
 * of parameters.
 */


#define FIT_MAIN

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <signal.h>

#include "plot.h"
#include "type.h"               /* own types */
#include "matrix.h"
#include "fit.h"
#include "setshow.h"   /* for load_range */
#include "alloc.h"

#if defined(MSDOS) || defined(DOS386)	     /* non-blocking IO stuff */
# include <io.h>
# if !defined(_Windows)  /* WIN16 does define MSDOS .... */
#  include <conio.h>
# endif
# include <dos.h>
#else
# ifndef VMS
#  include <fcntl.h>
# endif	/* VMS */
#endif	/* DOS */

#if defined(ATARI) || defined(MTOS)
# include <osbind.h>
# include <time.h>
# define getchx() Crawcin()
int kbhit(void);
#endif

#define STANDARD    stderr	/* compatible with gnuplot philosophy */

#define BACKUP_SUFFIX ".old"


/* access external global variables  (ought to make a globals.h someday) */

extern struct udft_entry *dummy_func;
extern char dummy_var[MAX_NUM_VAR][MAX_ID_LEN+1];
extern char c_dummy_var[MAX_NUM_VAR][MAX_ID_LEN+1];
extern int c_token;
extern int df_datum, df_line_number;
   

enum marq_res { OK, ERROR, BETTER, WORSE };
typedef enum marq_res marq_res_t;

#ifdef INFINITY
#undef INFINITY
#endif
#define INFINITY    1e30
#define NEARLY_ZERO 1e-30
#define INITIAL_VALUE 1.0  /* create new variables with this value (was NEARLY_ZERO) */
#define DELTA	    0.001					/* Relative change for derivatives */
#define MAX_DATA    2048
#define MAX_PARAMS  32
#define MAX_LAMBDA  1e20
#define MIN_LAMBDA  1e-20
#define LAMBDA_UP_FACTOR 10
#define LAMBDA_DOWN_FACTOR 10
/*#define MAX_VALUES_PER_LINE	32*/  /* HBB 971023: turned out to be unused! */
/*#define MAX_VARLEN  32*/          /* HBB 971023: use MAX_ID_LEN instead! */
#if defined(MSDOS) || defined(OS2) || defined(DOS386)
#define PLUSMINUS   "\xF1"                      /* plusminus sign */
#else
#define PLUSMINUS   "+/-"
#endif

/* HBB 971023: new, allow for dynamic adjustment of these: */
static int max_data;
static int max_params;

static double epsilon	   = 1e-5;		/* convergence limit */
static int maxiter         = 0;	    /* HBB 970304: maxiter patch */

static char fit_script[127];
static char logfile[128]    = "fit.log";
static char *FIXED	    = "# FIXED";
static char *GNUFITLOG	    = "FIT_LOG";
static char *FITLIMIT	    = "FIT_LIMIT";
static char *FITSTARTLAMBDA = "FIT_START_LAMBDA";
static char *FITLAMBDAFACTOR = "FIT_LAMBDA_FACTOR";
static char *FITMAXITER     = "FIT_MAXITER"; /* HBB 970304: maxiter patch */
static char *FITSCRIPT	    = "FIT_SCRIPT";
static char *DEFAULT_CMD    = "replot";         /* if no fitscript spec. */


static FILE	    *log_f = NULL;

static int	    num_data,
		    num_params;
static int	    columns; 
static double	    *fit_x=0, 
		    *fit_y=0,
		    *fit_z=0,
		    *err_data = 0,
		    *a = 0;
static TBOOLEAN	    ctrlc_flag = FALSE;

static struct udft_entry func;

typedef char fixstr[MAX_ID_LEN+1];

static fixstr	    *par_name;

static double       startup_lambda = 0,
                    lambda_down_factor = LAMBDA_DOWN_FACTOR,
                    lambda_up_factor = LAMBDA_UP_FACTOR;

/*****************************************************************
			 internal Prototypes
*****************************************************************/

static RETSIGTYPE ctrlc_handle __PROTO((int an_int));
static void ctrlc_setup __PROTO((void));
static marq_res_t marquardt __PROTO((double a[], double **alpha, double *chisq,
			        double *lambda));
static TBOOLEAN analyze __PROTO((double a[], double **alpha, double beta[],
			    double *chisq));
static void calculate __PROTO((double *zfunc, double **dzda, double a[]));
static void call_gnuplot __PROTO((double *par, double *data));
static void show_fit __PROTO((int i, double chisq, double last_chisq, double *a,
		          double lambda, FILE *device));
static TBOOLEAN regress __PROTO((double a[]));
static TBOOLEAN is_variable __PROTO((char *s));
static void copy_max __PROTO((char *d, char *s, unsigned int n)); 
static double getdvar __PROTO((char *varname));
static double createdvar __PROTO((char *varname, double value));
static void splitpath __PROTO((char *s, char *p, char *f));
static void backup_file __PROTO((char *, const char *));
#if defined(MSDOS) || defined(DOS386)
static void subst __PROTO((char *s, char from, char to));
#endif
static TBOOLEAN fit_interrupt __PROTO((void));
static TBOOLEAN is_empty __PROTO((char *s));

/*****************************************************************
    Useful macros
    We avoid any use of varargs/stdargs (not good style but portable)
*****************************************************************/
#define Dblf(a) 	{fprintf (STANDARD,a); fprintf (log_f,a);}
#define Dblf2(a,b)	{fprintf (STANDARD,a,b); fprintf (log_f,a,b);}
#define Dblf3(a,b,c)    {fprintf (STANDARD,a,b,c); fprintf (log_f,a,b,c);}
#define Dblf5(a,b,c,d,e) \
		{fprintf (STANDARD,a,b,c,d,e); fprintf (log_f,a,b,c,d,e);}
#define Dblf6(a,b,c,d,e,f) \
                {fprintf (STANDARD,a,b,c,d,e,f); fprintf (log_f,a,b,c,d,e,f);}

/*****************************************************************
    This is called when a SIGINT occurs during fit
*****************************************************************/

static RETSIGTYPE ctrlc_handle (an_int)
int an_int;
{
#ifdef OS2
    (void) signal(an_int, SIG_ACK);
#else
    /* reinstall signal handler (necessary on SysV) */
    (void) signal(SIGINT, (sigfunc)ctrlc_handle);
#endif
    ctrlc_flag = TRUE;
}


/*****************************************************************
    setup the ctrl_c signal handler
*****************************************************************/
static void ctrlc_setup()
{
/*
 *  MSDOS defines signal(SIGINT) but doesn't handle it through
 *  real interrupts. So there remain cases in which a ctrl-c may
 *  be uncaught by signal. We must use kbhit() instead that really
 *  serves the keyboard interrupt (or write an own interrupt func
 *  which also generates #ifdefs)
 *
 *  I hope that other OSes do it better, if not... add #ifdefs :-(
 */
#if (defined(__EMX__) || !defined(MSDOS) && !defined(DOS386)) && !defined(ATARI) && !defined(MTOS)
	(void) signal(SIGINT, (sigfunc)ctrlc_handle);
#endif
}


/*****************************************************************
    getch that handles also function keys etc.
*****************************************************************/
#if defined(MSDOS) || defined(DOS386)

/* HBB 980317: added a prototype... */
int getchx __PROTO((void));

int getchx ()
{
    register int c = getch();
    if ( !c  ||  c == 0xE0 ) {
	c <<= 8;
	c |= getch ();
    }
    return c;
}
#endif

/*****************************************************************
    in case of fatal errors
*****************************************************************/
void error_ex ()
{
    char *sp;

    strncpy (fitbuf, "         ", 9);         /* start after GNUPLOT> */
    sp = strchr (fitbuf, '\0');
    while ( *--sp == '\n' )
	;
    strcpy (sp+1, "\n\n");              /* terminate with exactly 2 newlines */
    fprintf (STANDARD, fitbuf);
      if ( log_f ) {
	fprintf (log_f, "BREAK: %s", fitbuf);
  	(void) fclose (log_f); 
  	log_f = NULL;
      }
    if ( func.at ) {
	free (func.at); 		    /* release perm. action table */
	func.at = (struct at_type *) NULL;
    }

    /* restore original SIGINT function */
#if !defined(ATARI) && !defined(MTOS)
    interrupt_setup ();
#endif

    bail_to_command_line();
}


/*****************************************************************
    New utility routine: print a matrix (for debugging the alg.)
*****************************************************************/
static void printmatrix (double **C, int m, int n) 
{
  int i, j;
  
  for (i=0; i<m; i++) {
    for (j=0; j<n-1; j++) 
      Dblf2("%.8g |", C[i][j]);
    Dblf2("%.8g\n", C[i][j]);
  }
  Dblf("\n");
}

/**************************************************************************
    Yet another debugging aid: print matrix, with diff. and residue vector
**************************************************************************/
static void print_matrix_and_vectors (double **C, double *d, double *r,
                                      int m, int n) 
{
  int i, j;
  
  for (i=0; i<m; i++) {
    for (j=0; j<n; j++) 
      Dblf2("%8g ", C[i][j]);
    Dblf3("| %8g | %8g\n", d[i], r[i]);
  }
  Dblf("\n");
}


/*****************************************************************
    Marquardt's nonlinear least squares fit
*****************************************************************/
static marq_res_t marquardt (a, C, chisq, lambda)
double a[];
double **C;
double *chisq;
double *lambda;
{
    int 	    i, j;
    static double   *da=0,	    /* delta-step of the parameter */ 
		    *temp_a=0,	    /* temptative new params set   */
		    *d=0,
		    *tmp_d=0,
		    **tmp_C=0, 
		    *residues=0;
    double          tmp_chisq;		    

    /* Initialization when lambda == -1 */

    if ( *lambda == -1 ) {	/* Get first chi-square check */
	TBOOLEAN	    analyze_ret;		    

	temp_a	    = vec (num_params);
	d	    = vec (num_data+num_params);
	tmp_d       = vec (num_data+num_params);
	da	    = vec (num_params);
	residues    = vec (num_data+num_params);
	tmp_C       = matr (num_data+num_params, num_params); 

	analyze_ret = analyze (a, C, d, chisq);

	/* Calculate a useful startup value for lambda, as given by Schwarz */
	/* FIXME: this is doesn't turn out to be much better, really... */
	if ( startup_lambda != 0 )
	    *lambda = startup_lambda;
	else {
	    *lambda = 0;
	    for (i=0; i<num_data; i++)
		for (j=0; j<num_params; j++)
		    *lambda += C[i][j]*C[i][j];
	    *lambda = sqrt(*lambda/num_data/num_params);
    }

 	/* Fill in the lower square part of C (the diagonal is filled in on
 	   each iteration, see below) */
 	for (i=0; i<num_params; i++) 
	    for (j=0; j<i;  j++) 
	     	C[num_data+i][j] = 0, C[num_data+j][i] = 0;
        /*printmatrix(C, num_data+num_params, num_params); */
	return analyze_ret ? OK : ERROR;
    }

    /* once converged, free dynamic allocated vars */

    if ( *lambda == -2 ) { 
	free (d);
	free (tmp_d);
	free (da);
	free (temp_a);
	free (residues);
	free_matr (tmp_C); 
        return OK;
    }

    /* Givens calculates in-place, so make working copies of C and d */

    for ( j=0 ; j<num_data+num_params ; j++ ) 
	memcpy (tmp_C[j], C[j], num_params*sizeof(double));
    memcpy (tmp_d, d, num_data*sizeof(double));

    /* fill in additional parts of tmp_C, tmp_d */

    for ( i=0; i<num_params ; i++) {	
    	tmp_C[num_data+i][i] = *lambda; /* fill in low diag. of tmp_C ... */ 
    	tmp_d[num_data+i] = 0;          /* ... and low part of tmp_d */
    }
    /*printmatrix(tmp_C, num_data+num_params, num_params);*/

    /* FIXME: residues[] isn't used at all. Why? Should it be used? */

    Givens ( tmp_C, tmp_d, da, residues, num_params+num_data, num_params, 1);
    /*print_matrix_and_vectors (tmp_C, tmp_d, residues,
                                num_params+num_data, num_params);*/
    
    /* check if trial did ameliorate sum of squares */

    for ( j=0 ; j<num_params ; j++ ) 
	temp_a[j] = a[j] + da[j];

    if ( !analyze (temp_a, tmp_C, tmp_d, &tmp_chisq) )
	return ERROR;  /* FIXME: will never be reached: always returns TRUE */

    if ( tmp_chisq < *chisq ) {     /* Success, accept new solution */
        if ( *lambda > MIN_LAMBDA ) {
            (void)putc('/',stderr);
	    *lambda /= lambda_down_factor;
        }
	*chisq = tmp_chisq;
	for ( j=0 ; j<num_data ; j++ ) {
	    memcpy (C[j], tmp_C[j], num_params*sizeof(double));
	    d[j] = tmp_d[j];
	}
	for ( j=0 ; j<num_params ; j++ ) 
	    a[j] = temp_a[j];
	return BETTER;
    }
    else {			    /* failure, increase lambda and return */
      	(void)putc('*',stderr); 
	*lambda *= lambda_up_factor;
	return WORSE;
    }
}


/* FIXME: in the new code, this function doesn't really do enough to be
 * useful. Maybe it ought to be deleted, i.e. integrated with
 * calculate() ?
 */
/*****************************************************************
    compute chi-square and numeric derivations
*****************************************************************/
static TBOOLEAN analyze (a, C, d, chisq)
double a[];
double **C;
double d[];
double *chisq;
{
/*
 *  used by marquardt to evaluate the linearized fitting matrix C
 *  and vector d, fills in only the top part of C and d
 *  I don't use a temporary array zfunc[] any more. Just use
 *  d[] instead.
 */
    int    i, j;

    *chisq = 0;
    calculate (d, C, a);

    for (i=0; i<num_data; i++) {
        d[i] = (d[i] - fit_z[i]) / err_data[i];         /* note: order reversed, as used by Schwarz */
    	*chisq += d[i]*d[i];
        for (j=0; j<num_params; j++)
	  C[i][j] /= err_data[i];
    }

    return TRUE;     /* FIXME: why return a value that is always TRUE ? */
}


/* To use the more exact, but slower two-side formula, activate the
   following line: */
/*#define TWO_SIDE_DIFFERENTIATION*/
/*****************************************************************
    compute function values and partial derivatives of chi-square
*****************************************************************/
static void calculate (zfunc, dzda, a)
double *zfunc;
double **dzda;
double a[];
{
    int    k, p;
    double  tmp_a;
    double  *tmp_high,
	    *tmp_pars;
#ifdef TWO_SIDE_DIFFERENTIATION
    double  *tmp_low;
#endif

    tmp_high = vec (num_data);	      /* numeric derivations */
#ifdef TWO_SIDE_DIFFERENTIATION
    tmp_low  = vec (num_data);
#endif
    tmp_pars = vec (num_params);

    /* first function values */

    call_gnuplot (a, zfunc);

    /* then derivatives */

    for ( p=0 ; p<num_params ; p++ )
	tmp_pars[p] = a[p];
    for ( p=0 ; p<num_params ; p++ ) {
	tmp_a = fabs(a[p]) < NEARLY_ZERO ? NEARLY_ZERO : a[p];
	tmp_pars[p] = tmp_a * (1+DELTA);
	call_gnuplot (tmp_pars, tmp_high);
#ifdef TWO_SIDE_DIFFERENTIATION
	tmp_pars[p] = tmp_a * (1-DELTA);
	call_gnuplot (tmp_pars, tmp_low);  
#endif
	for ( k=0 ; k<num_data ; k++ )
#ifdef TWO_SIDE_DIFFERENTIATION
	    dzda[k][p] = (tmp_high[k] - tmp_low[k])/(2*tmp_a*DELTA); 
#else
	    dzda[k][p] = (tmp_high[k] - zfunc[k])/(tmp_a*DELTA);
#endif
        tmp_pars[p] = a[p];
    }

#ifdef TWO_SIDE_DIFFERENTIATION
    free (tmp_low);
#endif
    free (tmp_high);
    free (tmp_pars);
}


/*****************************************************************
    call internal gnuplot functions
*****************************************************************/
static void call_gnuplot (par, data)
double *par;
double *data;
{
    int    i;
    struct value v;

    /* set parameters first */
    for ( i=0 ; i<num_params ; i++ ) {
	(void) Gcomplex (&v, par[i], 0.0); 
	setvar (par_name[i], v);
    }

    for (i = 0; i < num_data; i++) {
	/* calculate fit-function value */
	(void) Gcomplex (&func.dummy_values[0], fit_x[i], 0.0);
	(void) Gcomplex (&func.dummy_values[1], fit_y[i], 0.0);
	evaluate_at (func.at,&v);
	if ( undefined )
	    Eex ("Undefined value during function evaluation")
	data[i] = real(&v);
    }
}


/*****************************************************************
    handle user interrupts during fit
*****************************************************************/
static TBOOLEAN fit_interrupt ()
{
    while ( TRUE ) {
	fprintf (STANDARD, "\n\n(S)top fit, (C)ontinue, (E)xecute:  ");
	switch (getc(stdin)) {

	case EOF :
	case 's' :
	case 'S' :  fprintf (STANDARD, "Stop.");
		    return FALSE;

	case 'c' :
	case 'C' :  fprintf (STANDARD, "Continue.");
		    return TRUE;

	case 'e' :
	case 'E' :  {
			int i;
			struct value v;
			char *tmp;

			tmp = (fit_script !=0 && *fit_script ) ? fit_script : DEFAULT_CMD;
			fprintf (STANDARD, "executing: %s", tmp);
			/* set parameters visible to gnuplot */
			for ( i=0 ; i<num_params ; i++ ) {
			    (void)Gcomplex (&v, a[i], 0.0); 
			    setvar (par_name[i], v);
			}
			sprintf (input_line, tmp);
			(void) do_line ();
		    }
	}
    }
    return TRUE; 
}


/*****************************************************************
    frame routine for the marquardt-fit
*****************************************************************/
static TBOOLEAN regress (a)
double a[];
{
    double	**covar,
		*dpar,
		**C,
		chisq,
		last_chisq,
		lambda;
    int 	iter, i, j;
    marq_res_t	res;

    chisq   = last_chisq = INFINITY;
    C	    = matr (num_data+num_params, num_params);
    lambda  = -1;				/* use sign as flag */
    iter = 0;					/* iteration counter  */

    /* ctrlc now serves as Hotkey */
    ctrlc_setup ();

    /* Initialize internal variables and 1st chi-square check */
    if ( (res = marquardt (a, C, &chisq, &lambda)) == ERROR )
	Eex ("FIT: error occured during fit")
    res = BETTER;

    Dblf ("\nInitial set of free parameters:\n")
    show_fit (iter, chisq, chisq, a, lambda, STANDARD);
    show_fit (iter, chisq, chisq, a, lambda, log_f);

    /* MAIN FIT LOOP: do the regression iteration */

    do {
/*
 *  MSDOS defines signal(SIGINT) but doesn't handle it through
 *  real interrupts. So there remain cases in which a ctrl-c may
 *  be uncaught by signal. We must use kbhit() instead that really
 *  serves the keyboard interrupt (or write an own interrupt func
 *  which also generates #ifdefs)
 *
 *  I hope that other OSes do it better, if not... add #ifdefs :-(
 *  EMX does not have kbhit.
 * 
 *  HBB: I think this can be enabled for DJGPP V2. SIGINT is actually
 *  handled there, AFAIK.
 */
#if ((defined(MSDOS) || defined(DOS386)) && !defined(__EMX__)) || defined(ATARI) || defined(MTOS)
 	if ( kbhit () ) {
	    do {getchx ();} while ( kbhit() );
	    ctrlc_flag = TRUE;
	}
#endif

	if ( ctrlc_flag ) {
	    show_fit (iter, chisq, last_chisq, a, lambda, STANDARD);
	    ctrlc_flag = FALSE;
	    if ( !fit_interrupt () )	       /* handle keys */
		break;
	}

	if ( res == BETTER ) {
            iter++;
            last_chisq = chisq;
	}
        if ( (res = marquardt (a, C, &chisq, &lambda)) == BETTER )
	    show_fit (iter, chisq, last_chisq, a, lambda, STANDARD);
    } while ( (res != ERROR)
	      && (lambda < MAX_LAMBDA)
	      && ((maxiter == 0) || (iter <=maxiter))
	      && (res == WORSE
		  || ( (chisq > NEARLY_ZERO)
		       ? ((last_chisq-chisq)/chisq)
		       : (last_chisq-chisq)) > epsilon
		  ) 
	      );

    /* fit done */

#if !defined(ATARI) && !defined(MTOS)
    /* restore original SIGINT function */
    interrupt_setup ();
#endif

    /* HBB 970304: the maxiter patch: */
    if ((maxiter>0)&&(iter>maxiter)) {
      Dblf2 ("\nMaximum iteration count (%d) reached. Fit stopped.\n", maxiter)
    } else {
      Dblf2 ("\nAfter %d iterations the fit converged.\n", iter)
    }

    Dblf2 ("final sum of squares residuals    : %g\n", chisq)
    
    if (chisq > NEARLY_ZERO) {
	Dblf2 ("rel. change during last iteration : %g\n\n", (chisq-last_chisq)/chisq);
    } else {
	Dblf2 ("abs. change during last iteration : %g\n\n", (chisq-last_chisq))
    }

    if ( res == ERROR )
	Eex ("FIT: error occured during fit")

    /* compute errors in the parameters */

    if (num_data==num_params) {
      int i;
    	
      Dblf("\nExactly as many data points as there are parameters.\n");
      Dblf("In this degenerate case, all errors are zero by definition.\n\n");
      Dblf ("Final set of parameters \n");
      Dblf ("======================= \n\n");
      for ( i=0 ; i<num_params; i++ ) 
        Dblf3 ("%-15.15s = %-15g\n", par_name[i], a[i]);
    } else if (chisq < NEARLY_ZERO) { 
      int i;
        
      Dblf("\nHmmmm.... Sum of squared residuals is zero. Can't compute errors.\n\n");
      Dblf ("Final set of parameters \n");
      Dblf ("======================= \n\n");
      for ( i=0 ; i<num_params; i++ ) 
        Dblf3 ("%-15.15s = %-15g\n", par_name[i], a[i]);
    } else {

    Dblf2 ("chisquared / # degrees of freedom : %g\n", chisq/(num_data-num_params))
	 
      /* get covariance-, Korrelations- and Kurvature-Matrix */
      /* and errors in the parameters			  */

    /* compute covar[][] directly from C */
    Givens ( C, 0, 0, 0, num_data, num_params, 0) ;
    /*printmatrix(C, num_params, num_params);*/
    covar   =  C + num_data;     /* Use lower square of C for covar */
    Invert_RtR ( C, covar, num_params );
    /*printmatrix(covar, num_params, num_params);*/

    /* calculate unscaled parameter errors in dpar[]: */
      dpar = vec (num_params);
    for (i=0; i<num_params; i++) {
      /* FIXME: can this still happen ? */
      if (covar[i][i]<=0.0) /* HBB: prevent floating point exception later on */
	Eex("Calculation error: non-positive diagonal element in covar. matrix");
      dpar[i]=sqrt(covar[i][i]);
    }

    /* transform covariances into correlations */
    for (i=0; i<num_params; i++) 
      for (j=0; j<=i; j++)   /* only lower triangle needs to be handled */
	covar[i][j] /= dpar[i]*dpar[j];

    /* scale parameter errors based on chisq */
    if (columns == 2)
      /* FIXME: maybe it should be num_data - num_params here as well?  */
      chisq = sqrt ( chisq / (num_data)); 
    else
      chisq = sqrt ( chisq / (num_data - num_params));
#if 1  /* HBB: For some I don't really understand yet, this seems to
        * be incorrect, at least for one demo case I know of
        * (thanks to clegelan@physik.uni-bielefeld.de)
        * OTOH, this makes most of the demo's results simply
        * ridiculous... :-( */
    for (i=0; i<num_params; i++)
      dpar[i] *= chisq;
#endif

      Dblf ("Final set of parameters            68.3%% confidence interval (one at a time)\n")
      Dblf ("=======================            =========================================\n\n")
      for ( i=0 ; i<num_params; i++ ) {
        double temp =
            (fabs(a[i]) < NEARLY_ZERO)? 0.0 : fabs(100.0*dpar[i]/a[i]);
	Dblf6 ("%-15.15s = %-15g  %-3.3s %-15g (%g%%)\n", par_name[i], a[i],
               PLUSMINUS, dpar[i], temp)
      }

      Dblf ("\n\ncorrelation matrix of the fit parameters:\n\n")
      Dblf ("               ")
      for ( j=0 ; j<num_params ; j++ )
	Dblf2 ("%-6.6s ", par_name[j])
      Dblf ("\n")

      for ( i=0 ; i<num_params; i++ ) {
	Dblf2 ("%-15.15s", par_name[i]) ;
	for ( j=0 ; j<=i; j++ )   /* Only print lower triangle of symmetric matrix */
	  Dblf2 ("%6.3f ", covar[i][j] ); 
	Dblf ("\n")
      }
      
      free (dpar);
    } 
      
#if 0 /* FIXME: what is this for? Well, let's see what happens without it... */
    /* restore last parameter's value (not done by calculate) */
    {
      struct value val;
      Gcomplex (&val, a[num_params-1], 0.0);
      setvar (par_name[num_params-1], val);
    }
#endif

    /* call destructor for allocated vars */
    lambda = -2;   /* flag value, meaning 'destruct!' */
    (void)marquardt (a, C, &chisq, &lambda);
    
    free_matr (C);
    return TRUE;
}


/*****************************************************************
    display actual state of the fit
*****************************************************************/
static void show_fit (i, chisq, last_chisq, a, lambda, device)
int i;
double chisq;
double last_chisq;
double *a;
double lambda;
FILE *device;
{
    int k;

    fprintf (device, "\n\n\
Iteration %d\n\
chisquare : %-15g   relative deltachi2 : %g\n\
deltachi2 : %-15g   limit for stopping : %g\n\
lambda	  : %g\n\nactual set of parameters\n\n",
i, chisq, chisq > NEARLY_ZERO ? (chisq - last_chisq)/chisq : 0.0, chisq - last_chisq, epsilon, lambda);
    for ( k=0 ; k<num_params ; k++ )
	fprintf (device, "%-15.15s = %g\n", par_name[k], a[k]);
}



/*****************************************************************
    is_empty: check for valid string entries
*****************************************************************/
static TBOOLEAN is_empty (s)
char *s;
{
    while ( *s == ' '  ||  *s == '\t'  ||  *s == '\n' )
	s++;
    return (TBOOLEAN)( *s == '#'  ||  *s == '\0' );
}


/*****************************************************************
    get next word of a multi-word string, advance pointer
*****************************************************************/
char *get_next_word (s, subst)
char **s;
char *subst;
{
    char *tmp = *s;

    while ( *tmp==' ' || *tmp=='\t' || *tmp=='=' )
	tmp++;
    if ( *tmp=='\n' || *tmp=='\0' )                    /* not found */
	return NULL;
    if ( (*s = strpbrk (tmp, " =\t\n")) == NULL )
	*s = tmp + strlen(tmp);
    *subst = **s;
    *(*s)++ = '\0';
    return tmp;
}


/*****************************************************************
    check for variable identifiers
*****************************************************************/
static TBOOLEAN is_variable (s)
char *s;
{
    while ( *s != '\0' ) { 
	if ( !isalnum(*s) && *s!='_' )
	    return FALSE;
	s++;
    }
    return TRUE;
}


/*****************************************************************
    strcpy but max n chars
*****************************************************************/
static void copy_max (d, s, n)
char *d;
char *s;
unsigned int n;
{
    strncpy (d, s, (size_t)n);
    if ( strlen(s) >= (size_t)n )
	d[n-1] = '\0';
}


/*****************************************************************
    first time settings
*****************************************************************/
void init_fit ()
{
    func.at = (struct at_type *) NULL;      /* need to parse 1 time */
}


/*****************************************************************
	    Set a GNUPLOT user-defined variable
******************************************************************/

void setvar (varname, data)
char *varname;
struct value data;
{
    register struct udvt_entry *udv_ptr = first_udv,
			       *last = first_udv;

    /* check if it's already in the table... */

    while (udv_ptr) {
	    last = udv_ptr;
	    if ( !strcmp (varname, udv_ptr->udv_name))
		break;
	    udv_ptr = udv_ptr->next_udv;
    }

    if ( !udv_ptr ) {			    /* generate new entry */
	last->next_udv = udv_ptr = (struct udvt_entry *)
	  gp_alloc ((unsigned int)sizeof(struct udvt_entry), "fit setvar");
	udv_ptr->next_udv = NULL;
    }
    copy_max (udv_ptr->udv_name, varname, (int)sizeof(udv_ptr->udv_name));
    udv_ptr->udv_value = data;
    udv_ptr->udv_undef = FALSE;
}



/*****************************************************************
    Read INTGR Variable value, return 0 if undefined or wrong type
*****************************************************************/
int getivar (varname)
char *varname;
{
    register struct udvt_entry *udv_ptr = first_udv;

    while (udv_ptr) {
	    if ( !strcmp (varname, udv_ptr->udv_name))
		return udv_ptr->udv_value.type == INTGR
		       ? udv_ptr->udv_value.v.int_val	/* valid */
		       : 0;				/* wrong type */
            udv_ptr = udv_ptr->next_udv;
    }
    return 0;						/* not in table */
}


/*****************************************************************
    Read DOUBLE Variable value, return 0 if undefined or wrong type
   I dont think it's a problem that it's an integer - div
*****************************************************************/
static double getdvar (varname)
char *varname;
{
    register struct udvt_entry *udv_ptr = first_udv;

    for ( ; udv_ptr ; udv_ptr=udv_ptr->next_udv)
	    if ( strcmp (varname, udv_ptr->udv_name)==0)
	      return real(&(udv_ptr->udv_value));

    /* get here => not found */
    return 0;
}

/* like getdvar, but
 * - convert it from integer to real if necessary
 * - create it with value INITIAL_VALUE if not found or undefined
 */
static double createdvar(varname,value)
char *varname;
double value;
{
    register struct udvt_entry *udv_ptr = first_udv;

    for ( ; udv_ptr ; udv_ptr=udv_ptr->next_udv)
	    if ( strcmp (varname, udv_ptr->udv_name)==0) {
	    	if (udv_ptr->udv_undef) {
	    		udv_ptr->udv_undef=0;
	    		(void)Gcomplex(&udv_ptr->udv_value, value, 0.0); 
	    	} else if (udv_ptr->udv_value.type==INTGR) {
	    		(void)Gcomplex(&udv_ptr->udv_value, (double)udv_ptr->udv_value.v.int_val, 0.0); 
	    	}
		   return real(&(udv_ptr->udv_value));
		 }

    /* get here => not found */

    {
        struct value tempval; 
        (void)Gcomplex(&tempval, value, 0.0);
        setvar(varname, tempval); 
    }
    
    return value;
}
	

/*****************************************************************
    Split Identifier into path and filename
*****************************************************************/
static void splitpath (s, p, f)
char *s;
char *p;
char *f;
{
    register char *tmp;
    tmp = s + strlen(s) - 1;
    while ( *tmp != '\\'  &&  *tmp != '/'  &&  *tmp != ':'  &&  tmp-s >= 0 )
	tmp--;
    strcpy (f, tmp+1);
    strncpy (p, s, (size_t) (tmp-s+1)); 
    p[tmp-s+1] = '\0';
}


#if defined(MSDOS) || defined(DOS386)
/*****************************************************************
    Character substitution
*****************************************************************/
#ifdef PROTOTYPES
static void subst (char *s, char from, char to)
#else
static void subst (s, from, to)
char *s;
char from;
char to;
#endif
{
    while ( *s ) {
	if ( *s == from )
	    *s = to;
	s++;
    }
}
#endif


/*****************************************************************
    write the actual parameters to start parameter file
*****************************************************************/
void update (pfile, npfile)
char *pfile, *npfile;
{
    char	fnam[256],
		path[256],
		sstr[256],
		pname[64],
		tail[127],
		*s = sstr,
		*tmp,
		c;
    char        ifilename[256], *ofilename;
    FILE        *of,
		*nf;
    double	pval;


    /* update pfile npfile:
       if npfile is a valid file name,
       take pfile as input file and
       npfile as output file
     */
	if (VALID_FILENAME(npfile)) {
		strncpy (ifilename, pfile, sizeof ifilename);
		ofilename = npfile;
	} else {
#ifdef BACKUP_FILESYSTEM
		/* filesystem will keep original as previous version */
		strncpy (ifilename, pfile, sizeof ifilename);
#else
		backup_file (ifilename, pfile); /* will Eex if it fails */
#endif
		ofilename = pfile;
	}

	/* split into path and filename */
	splitpath (ifilename, path, fnam);
	if ( !(of = fopen (ifilename, "r")) )
		Eex2 ("parameter file %s could not be read", ifilename)

	if ( !(nf = fopen (ofilename, "w")) ) 
		Eex2 ("new parameter file %s could not be created", ofilename)

	while ( fgets (s = sstr, sizeof(sstr), of) != NULL ) {

		if ( is_empty(s) ) {
			fprintf (nf, s);				/* preserve comments */
            continue;
        }
        if ( (tmp = strchr (s, '#')) != NULL ) {
	    copy_max (tail, tmp, (int) sizeof(tail)); 
	    *tmp = '\0';
	}
	else
	    strcpy (tail, "\n");

	tmp = get_next_word (&s, &c);
	if ( !is_variable (tmp)  ||  strlen(tmp) > MAX_ID_LEN ) {
	    (void)fclose (nf);
	    (void)fclose (of);
	    Eex2 ("syntax error in parameter file %s", fnam)
	}
	copy_max (pname, tmp, (int)sizeof(pname)); 
	/* next must be '=' */
	if ( c != '=' ) {
	    tmp = strchr (s, '=');
	    if ( tmp == NULL ) {
				(void)fclose (nf);
				(void)fclose (of);
		Eex2 ("syntax error in parameter file %s", fnam)
	    }
	    s = tmp+1;
	}
	tmp = get_next_word (&s, &c);
	if ( !sscanf (tmp, "%lf", &pval) ) {
	    (void)fclose (nf);
	    (void)fclose (of);
	    Eex2 ("syntax error in parameter file %s", fnam)
	}
	if ( (tmp = get_next_word (&s, &c)) != NULL ) {
	    (void)fclose (nf);
	    (void)fclose (of);
	    Eex2 ("syntax error in parameter file %s", fnam)
	}

        /* now modify */

	if ( (pval = getdvar (pname)) == 0 )
	    pval = (double) getivar (pname);

	sprintf (sstr, "%g", pval);
	if ( !strchr (sstr, '.')  &&  !strchr (sstr, 'e') )
	    strcat (sstr, ".0");                /* assure CMPLX-type */
	fprintf (nf, "%-15.15s = %-15.15s   %s", pname, sstr, tail);
    }
    if ( fclose (nf) || fclose (of) )
	Eex ("I/O error during update")

}


/*****************************************************************
    Backup a file by renaming it to something useful. Return
    the new name in tofile
*****************************************************************/
static void
backup_file (tofile, fromfile)
char *tofile;
const char *fromfile;
/* tofile must point to a char array[] or allocated data. See update() */
{
#if defined (WIN32) || defined(MSDOS) || defined(VMS)
    	char *tmpn;
#endif

    	/* win32 needs to have two attempts at the rename, since it may
    	 * be running on win32s with msdos 8.3 names
    	 */

/* first attempt, for all o/s other than MSDOS */

#ifndef MSDOS

    strcpy(tofile, fromfile);

#ifdef VMS
	/* replace all dots with _, since we will be adding the only
	 * dot allowed in VMS names
	 */
    while ( (tmpn = strchr (tofile, '.') ) != NULL)
        *tmpn = '_';
#endif /*VMS*/

    strcat (tofile, BACKUP_SUFFIX);

	if (rename(fromfile, tofile) == 0)
		return; /* hurrah */
#endif


#if defined (WIN32) || defined(MSDOS)

	/* first attempt for msdos. Second attempt for win32s */
		
	/* beware : strncpy is very dangerous since it does not guarantee
	 * to terminate the string. Copy up to 8 characters. If exactly
	 * chars were copied, the string is not terminated. If the
	 * source string was shorter than 8 chars, no harm is done
	 * (here) by writing to offset 8.
	 */
	strncpy(tofile, fromfile, 8);
	tofile[8] = 0;
	
   while ( (tmpn = strchr (tofile, '.') ) != NULL)
        *tmpn = '_';

    strcat (tofile, BACKUP_SUFFIX);

    if (rename(fromfile, tofile) == 0)
    	return;  /* success */

#endif /* win32 || msdos */

	/* get here => rename failed. */
	Eex3("Could not rename file %s to %s", fromfile, tofile);
}


/*****************************************************************
    Interface to the classic gnuplot-software
*****************************************************************/

void do_fit ()
{
	TBOOLEAN autorange_x=3, autorange_y=3;  /* yes */ 
    double min_x, max_x;  /* range to fit */
    double min_y, max_y;  /* range to fit */
    int dummy_x=-1, dummy_y=-1; /* eg  fit [u=...] [v=...] */

    int 	i;
    double	v[4];
    double tmpd;
    time_t	timer;
    int token1, token2, token3;
    char *tmp;

    /* first look for a restricted x fit range... */

    if (equals(c_token, "[")) {
	c_token++;
	if (isletter(c_token)) {
	    if (equals(c_token + 1, "=")) {
		dummy_x = c_token;
		c_token += 2;
	    }
			/* else parse it as a xmin expression */
	}
	autorange_x = load_range(FIRST_X_AXIS,&min_x, &max_x, autorange_x);
	if (!equals(c_token, "]"))
		int_error("']' expected", c_token);
	c_token++;
    }

    /* ... and y */

    if (equals(c_token, "[")) {
	c_token++;
	if (isletter(c_token)) {
	    if (equals(c_token + 1, "=")) {
		dummy_y = c_token;
		c_token += 2;
	    }
	    /* else parse it as a ymin expression */
	}
	autorange_y = load_range(FIRST_Y_AXIS,&min_y, &max_y, autorange_y);
	if (!equals(c_token, "]"))
		int_error("']' expected", c_token);
	c_token++;
    }


/* now compile the function */

    token1 = c_token;

    if (func.at) {
	free(func.at);
	func.at=NULL;  /* in case perm_at() does int_error */
    }

    dummy_func = &func;

    
    /* set the dummy variable names */
    
    if (dummy_x >= 0)
	copy_str(c_dummy_var[0], dummy_x, MAX_ID_LEN);
    else
	strcpy(c_dummy_var[0], dummy_var[0]);

    if (dummy_y >= 0)
	copy_str(c_dummy_var[0], dummy_y, MAX_ID_LEN);
    else
	strcpy(c_dummy_var[1], dummy_var[1]);
	
    func.at = perm_at();
    dummy_func = NULL;

    token2 = c_token;


    
/* use datafile module to parse the datafile and qualifiers */

    columns = df_open(4);  /* up to 4 using specs allowed */
    if (columns==1)
	int_error("Need 2 to 4 using specs", c_token);

	/* defer actually reading the data until we have parsed the rest of
     the line */
    
	token3 = c_token;

    tmpd = getdvar (FITLIMIT);		  /* get epsilon if given explicitly */
    if ( tmpd < 1.0  &&  tmpd > 0.0 )
        epsilon = tmpd;

    /* HBB 970304: maxiter patch */
    maxiter = getivar (FITMAXITER);

	tmpd = getdvar (FITSTARTLAMBDA); /* get startup value for lambda, if given */
	if ( tmpd > 0.0 ) {
		startup_lambda = tmpd;
		printf("Lambda Start value set: %g\n", startup_lambda);
	}

	tmpd = getdvar (FITLAMBDAFACTOR); /* get lambda up/down factor, if given */
	if ( tmpd > 0.0 ) {
		lambda_up_factor = lambda_down_factor = tmpd;
		printf("Lambda scaling factors reset:  %g\n", lambda_up_factor);
	}

    *fit_script = '\0';
    if ( (tmp = getenv (FITSCRIPT)) != NULL )
	strcpy (fit_script, tmp);

    tmp = getenv (GNUFITLOG);		  /* open logfile */
    if ( tmp != NULL ) {
        char *tmp2 = &tmp[strlen(tmp)-1];
        if ( *tmp2 == '/'  ||  *tmp2 == '\\' )
            sprintf (logfile, "%s%s", tmp, logfile);
        else
            strcpy (logfile, tmp);
    }
    if ( !log_f && /* div */ !(log_f = fopen (logfile, "a")) )
	Eex2 ("could not open log-file %s", logfile)
    fprintf (log_f, "\n\n*******************************************************************************\n");
	(void)time (&timer); 
    fprintf (log_f, "%s\n\n", ctime (&timer));
	{
		char *line = NULL;
      
		m_capture(&line, token2,token3-1);
			fprintf (log_f, "FIT:    data read from %s\n", line);
		free(line);
	}

	max_data = MAX_DATA;
	fit_x = vec (max_data);		 /* start with max. value */
	fit_y = vec (max_data);
	fit_z = vec (max_data);

/* first read in experimental data */

	err_data = vec (max_data);
    num_data = 0;

	while ( (i=df_readline(v, 4)) != EOF ) {
		if ( num_data>=max_data ) {
			max_data = (max_data * 3) / 2; /* increase max_data by factor of 1.5 */
			if ( 0
					 || redim_vec (&fit_x, max_data)
					 || redim_vec (&fit_y, max_data)
					 || redim_vec (&fit_z, max_data)
					 || redim_vec (&err_data, max_data)
					 ) {
				/* Some of the reallocations went bad: */
			df_close();
				Eex2 ("Out of memory in fit: too many datapoints (%d)?", max_data);
			} else {
				/* Just so we know that the routine is at work: */
				fprintf(STANDARD, "Max. number of data points scaled up to: %d\n",
								max_data);
			}
		}

		switch (i)
		{
			case DF_UNDEFINED:
			case DF_FIRST_BLANK:
			case DF_SECOND_BLANK:
				continue;
			case 0:
				Eex2("bad data on line %d of datafile", df_line_number);
				break;
			case 1:  /* only z provided */
				v[2] = v[0];
				v[0] = (double)df_datum; 
				break;
			case 2: /* x and z */
				v[2] = v[1];
				break;

			/* only if the explicitly asked for 4 columns do we
			 * do a 3d fit. (We can get here if they didn't
			 * specify a using spec, and the file has 4 columns
			 */
			case 4:  /* x, y, z, error */
				if (columns == 4)
					break;
				/* else fall through */
			case 3: /* x, z, error */
				v[3] = v[2]; /* error */
				v[2] = v[1]; /* z */
				break;
				
		}

		/* skip this point if it is out of range */
		if ( !(autorange_x&1) && (v[0] < min_x))
			continue;
		if ( !(autorange_x&2) && (v[0] > max_x))
			continue;
		if ( !(autorange_y&1) && (v[1] < min_y))
			continue;
		if ( !(autorange_y&2) && (v[1] > max_y))
			continue;

		fit_x[num_data] = v[0];
		fit_y[num_data] = v[1];
		fit_z[num_data] = v[2];

		/* we only use error if _explicitly_ asked for by a
		 * using spec
		 */
		err_data[num_data++] = (columns > 2) ? v[3] : 1;
	}
	df_close();

	if (num_data <= 1)
		Eex("No data to fit ");
	
	/* now resize fields to actual length:*/
    redim_vec (&fit_x, num_data);
    redim_vec (&fit_y, num_data);
    redim_vec (&fit_z, num_data);
    redim_vec (&err_data, num_data);
    
    fprintf (log_f, "        #datapoints = %d\n", num_data);

    if ( columns < 3 )
	fprintf (log_f, "        y-errors assumed equally distributed\n\n");

	{
		char *line=NULL;
		
		m_capture(&line, token1, token2-1);		
		fprintf (log_f, "function used for fitting: %s\n", line);
		free(line);
	}

    /* read in parameters */

	max_params = MAX_PARAMS;			/* HBB 971023: make this resizeable */

	if (!equals(c_token, "via"))
		int_error("Need via and either parameter list or file", c_token);

	a = vec (max_params);
	par_name = (fixstr *) gp_alloc ((max_params+1)*sizeof(fixstr), "fit param");
    num_params = 0;

	if (isstring(++c_token)) {
		TBOOLEAN fixed;
		double	tmp_par;
		char c, *s;
		char sstr[MAX_LINE_LEN];
		FILE *f;

		quote_str(sstr, c_token++, MAX_LINE_LEN);

		/* get parameters and values out of file and ignore fixed ones */

		fprintf (log_f, "take parameters from file: %s\n\n", sstr);
		if ( !(f = fopen (sstr, "r")) )
			Eex2 ("could not read parameter-file %s", sstr);
		while ( TRUE ) {
			if ( !fgets (s = sstr, (int)sizeof(sstr), f) )		/* EOF found */ 
				break;
			if ( (tmp = strstr (s, FIXED)) != NULL ) {	      /* ignore fixed params */
				*tmp = '\0';
				fprintf (log_f, "FIXED:  %s\n", s);
				fixed = TRUE;
			}
			else
				fixed = FALSE;
			if ( (tmp = strchr (s, '#')) != NULL )
				*tmp = '\0';
			if ( is_empty(s) )
				continue;
			tmp = get_next_word (&s, &c);
			if ( !is_variable (tmp)  ||  strlen(tmp) > MAX_ID_LEN ) {
				(void)fclose (f); 
				Eex ("syntax error in parameter file");
			}
			
			copy_max (par_name[num_params], tmp, (int)sizeof(fixstr)); 
			/* next must be '=' */
			if ( c != '=' ) {
				tmp = strchr (s, '=');
				if ( tmp == NULL ) {
					(void)fclose (f); 
					Eex ("syntax error in parameter file");
				}
				s = tmp+1;
			}
			tmp = get_next_word (&s, &c);
			if ( sscanf (tmp, "%lf", &tmp_par) != 1 ) {
				(void)fclose (f);
				Eex ("syntax error in parameter file");
			}
			
			/* make fixed params visible to GNUPLOT */
			if ( fixed ) {
				struct value tempval; 
				(void)Gcomplex (&tempval, tmp_par, 0.0); 
				setvar (par_name[num_params], tempval);   /* use parname as temp */ 
			}
			else {
				if (num_params>=max_params) {
					max_params = (max_params * 3) / 2;
					if (0
							|| !redim_vec(&a, max_params)
							|| !(par_name = gp_realloc (par_name, (max_params+1)*sizeof(fixstr), "fit param resize"))
							) {
/* HBB: maybe it would be better to realloc a and v instead ? */
						(void)fclose(f); 
						Eex("Out of memory in fit: too many parameters?");
					}
				}
				a[num_params++] = tmp_par;
			}
			
			if ( (tmp = get_next_word (&s, &c)) != NULL ) {
				(void)fclose (f); 
				Eex ("syntax error in parameter file");
			}
		}
		(void)fclose (f); 
	} else { /* not a string after via */
		fprintf (log_f, "use actual parameter values\n\n");
		do {
			if (!isletter(c_token))
				Eex ("no parameter specified");
			capture(par_name[num_params], c_token, c_token, (int)sizeof(par_name[0])); 
			if (num_params >= max_params) {
				max_params = (max_params * 3) / 2;
				if (0
						|| !redim_vec(&a, max_params)
						|| !(par_name = gp_realloc (par_name, (max_params+1)*sizeof(fixstr), "fit param resize"))
						) {
					Eex("Out of memory in fit: too many parameters?");
				}
			}
			/* create variable if it doesn't exist */
			a[num_params] = createdvar (par_name[num_params], INITIAL_VALUE);
			++num_params;
		}	while (equals(++c_token, ",") && ++c_token);
	}
    redim_vec (&a, num_params);
    par_name = (fixstr *) gp_realloc (par_name, (num_params+1)*sizeof(fixstr), "fit param");

    if (num_data < num_params)
	Eex("Number of data points smaller than number of parameters");
	
    /* avoid parameters being equal to zero */
    for ( i=0 ; i<num_params ; i++ )
	if ( a[i] == 0 )
	    a[i] = NEARLY_ZERO;

	(void)regress (a);  

	(void)fclose (log_f);
    log_f = NULL;
    free (fit_x);
    free (fit_y);
    free (fit_z);
    free (err_data);
    free (a);
    free (par_name);
    if ( func.at ) {
        free (func.at);                     /* release perm. action table */
        func.at = (struct at_type *) NULL;
    }

}

#if defined(ATARI) || defined(MTOS)
int kbhit()
{
	fd_set rfds;
	struct timeval timeout;
	
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	rfds = (fd_set)(1L << fileno(stdin));
	return((select(0, &rfds, NULL, NULL, &timeout) > 0) ? 1 : 0);
}
#endif
