#ifndef lint
static char *RCSid() { return RCSid("$Id: fit.c,v 1.54 2006/03/11 21:35:49 sfeam Exp $"); }
#endif

/*  NOTICE: Change of Copyright Status
 *
 *  The author of this module, Carsten Grammes, has expressed in
 *  personal email that he has no more interest in this code, and
 *  doesn't claim any copyright. He has agreed to put this module
 *  into the public domain.
 *
 *  Lars Hecking  15-02-1999
 */

/*
 *    Nonlinear least squares fit according to the
 *      Marquardt-Levenberg-algorithm
 *
 *      added as Patch to Gnuplot (v3.2 and higher)
 *      by Carsten Grammes
 *
 *      930726:     Recoding of the Unix-like raw console I/O routines by:
 *                  Michele Marziani (marziani@ferrara.infn.it)
 * drd: start unitialised variables at 1 rather than NEARLY_ZERO
 *  (fit is more likely to converge if started from 1 than 1e-30 ?)
 *
 * HBB (broeker@physik.rwth-aachen.de) : fit didn't calculate the errors
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

#include "fit.h"

#include <signal.h>

#include "alloc.h"
#include "axis.h"
#include "command.h"
#include "datafile.h"
#include "eval.h"
#include "gp_time.h"
#include "matrix.h"
#include "plot.h"
#include "misc.h"
#include "util.h"

/* Just temporary */
#if defined(VA_START) && defined(STDC_HEADERS)
static void Dblfn __PROTO((const char *fmt, ...));
#else
static void Dblfn __PROTO(());
#endif
#define Dblf  Dblfn
#define Dblf2 Dblfn
#define Dblf3 Dblfn
#define Dblf5 Dblfn
#define Dblf6 Dblfn

#if defined(MSDOS) || defined(DOS386)	/* non-blocking IO stuff */
# include <io.h>
# ifndef _Windows		/* WIN16 does define MSDOS .... */
#  include <conio.h>
# endif
# include <dos.h>
#else /* !(MSDOS || DOS386) */
# ifndef VMS
#  include <fcntl.h>
# endif				/* !VMS */
#endif /* !(MSDOS || DOS386) */

#if defined(ATARI) || defined(MTOS)
# define getchx() Crawcin()
static int kbhit(void);
#endif

enum marq_res {
    OK, ML_ERROR, BETTER, WORSE
};
typedef enum marq_res marq_res_t;

#ifdef INFINITY
# undef INFINITY
#endif

#define INFINITY    1e30
#define NEARLY_ZERO 1e-30

/* create new variables with this value (was NEARLY_ZERO) */
#define INITIAL_VALUE 1.0

/* Relative change for derivatives */
#define DELTA	    0.001

#define MAX_DATA    2048
#define MAX_PARAMS  32
#define MAX_LAMBDA  1e20
#define MIN_LAMBDA  1e-20
#define LAMBDA_UP_FACTOR 10
#define LAMBDA_DOWN_FACTOR 10

#if defined(MSDOS) || defined(OS2) || defined(DOS386)
# define PLUSMINUS   "\xF1"	/* plusminus sign */
#else
# define PLUSMINUS   "+/-"
#endif

#define LASTFITCMDLENGTH 511

/* externally visible variables: */

/* saved copy of last 'fit' command -- for output by "save" */
char fitbuf[256];

/* log-file for fit command */
char *fitlogfile = NULL;

#ifdef GP_FIT_ERRVARS
/* NEW 20030131: should we place parameter errors into user-defined
 * variables?  */
TBOOLEAN fit_errorvariables = FALSE;
#endif /* GP_FIT_ERRVARS */

/* private variables: */

static int max_data;
static int max_params;

static double epsilon = 1e-5;	/* convergence limit */
static int maxiter = 0;

static char fit_script[256];

/* HBB/H.Harders 20020927: log file name now changeable from inside
 * gnuplot */
static const char fitlogfile_default[] = "fit.log";
static const char GNUFITLOG[] = "FIT_LOG";

static const char *GP_FIXED = "# FIXED";
static const char *FITLIMIT = "FIT_LIMIT";
static const char *FITSTARTLAMBDA = "FIT_START_LAMBDA";
static const char *FITLAMBDAFACTOR = "FIT_LAMBDA_FACTOR";
static const char *FITMAXITER = "FIT_MAXITER";	/* HBB 970304: maxiter patch */
static const char *FITSCRIPT = "FIT_SCRIPT";
static const char *DEFAULT_CMD = "replot";	/* if no fitscript spec. */
static char last_fit_command[LASTFITCMDLENGTH+1] = "";

static FILE *log_f = NULL;

static int num_data, num_params;
static int columns;
static double *fit_x = 0, *fit_y = 0, *fit_z = 0, *err_data = 0, *a = 0;
static TBOOLEAN ctrlc_flag = FALSE;
static TBOOLEAN user_stop = FALSE;

static struct udft_entry func;

typedef char fixstr[MAX_ID_LEN+1];

static fixstr *par_name;

static double startup_lambda = 0, lambda_down_factor = LAMBDA_DOWN_FACTOR, lambda_up_factor = LAMBDA_UP_FACTOR;

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
static TBOOLEAN fit_interrupt __PROTO((void));
static TBOOLEAN regress __PROTO((double a[]));
static void show_fit __PROTO((int i, double chisq, double last_chisq, double *a,
			      double lambda, FILE * device));
static void log_axis_restriction __PROTO((FILE *log_f, AXIS_INDEX axis));
static TBOOLEAN is_empty __PROTO((char *s));
static TBOOLEAN is_variable __PROTO((char *s));
static double getdvar __PROTO((const char *varname));
static int getivar __PROTO((const char *varname));
static void setvar __PROTO((char *varname, struct value data));
static char *get_next_word __PROTO((char **s, char *subst));
static double createdvar __PROTO((char *varname, double value));
static void splitpath __PROTO((char *s, char *p, char *f));
static void backup_file __PROTO((char *, const char *));

#ifdef GP_FIT_ERRVARS
static void setvarerr __PROTO((char *varname, double value));
#endif

/*****************************************************************
    Small function to write the last fit command into a file
    Arg: Pointer to the file; if NULL, nothing is written,
         but only the size of the string is returned.
*****************************************************************/

size_t
wri_to_fil_last_fit_cmd(FILE *fp)
{
    if (fp == NULL)
	return strlen(last_fit_command);
    else
	return (size_t) fputs(last_fit_command, fp);
}


/*****************************************************************
    This is called when a SIGINT occurs during fit
*****************************************************************/

static RETSIGTYPE
ctrlc_handle(int an_int)
{
    (void) an_int;		/* avoid -Wunused warning */
    /* reinstall signal handler (necessary on SysV) */
    (void) signal(SIGINT, (sigfunc) ctrlc_handle);
    ctrlc_flag = TRUE;
}


/*****************************************************************
    setup the ctrl_c signal handler
*****************************************************************/
static void
ctrlc_setup()
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
    (void) signal(SIGINT, (sigfunc) ctrlc_handle);
#endif
}


/*****************************************************************
    getch that handles also function keys etc.
*****************************************************************/
#if defined(MSDOS) || defined(DOS386)

/* HBB 980317: added a prototype... */
int getchx __PROTO((void));

int
getchx()
{
    int c = getch();
    if (!c || c == 0xE0) {
	c <<= 8;
	c |= getch();
    }
    return c;
}
#endif

/*****************************************************************
    in case of fatal errors
*****************************************************************/
void
error_ex()
{
    char *sp;

    memcpy(fitbuf, "         ", 9);	/* start after GNUPLOT> */
    sp = strchr(fitbuf, NUL);
    while (*--sp == '\n');
    strcpy(sp + 1, "\n\n");	/* terminate with exactly 2 newlines */
    fputs(fitbuf, STANDARD);
    if (log_f) {
	fprintf(log_f, "BREAK: %s", fitbuf);
	(void) fclose(log_f);
	log_f = NULL;
    }
    if (func.at) {
	free_at(func.at);		/* release perm. action table */
	func.at = (struct at_type *) NULL;
    }
    /* restore original SIGINT function */
#if !defined(ATARI) && !defined(MTOS)
    interrupt_setup();
#endif

    bail_to_command_line();
}

/* HBB 990829: removed the debug print routines */
/*****************************************************************
    Marquardt's nonlinear least squares fit
*****************************************************************/
static marq_res_t
marquardt(double a[], double **C, double *chisq, double *lambda)
{
    int i, j;
    static double *da = 0,	/* delta-step of the parameter */
    *temp_a = 0,		/* temptative new params set   */
    *d = 0, *tmp_d = 0, **tmp_C = 0, *residues = 0;
    double tmp_chisq;

    /* Initialization when lambda == -1 */

    if (*lambda == -1) {	/* Get first chi-square check */
	TBOOLEAN analyze_ret;

	temp_a = vec(num_params);
	d = vec(num_data + num_params);
	tmp_d = vec(num_data + num_params);
	da = vec(num_params);
	residues = vec(num_data + num_params);
	tmp_C = matr(num_data + num_params, num_params);

	analyze_ret = analyze(a, C, d, chisq);

	/* Calculate a useful startup value for lambda, as given by Schwarz */
	/* FIXME: this is doesn't turn out to be much better, really... */
	if (startup_lambda != 0)
	    *lambda = startup_lambda;
	else {
	    *lambda = 0;
	    for (i = 0; i < num_data; i++)
		for (j = 0; j < num_params; j++)
		    *lambda += C[i][j] * C[i][j];
	    *lambda = sqrt(*lambda / num_data / num_params);
	}

	/* Fill in the lower square part of C (the diagonal is filled in on
	   each iteration, see below) */
	for (i = 0; i < num_params; i++)
	    for (j = 0; j < i; j++)
		C[num_data + i][j] = 0, C[num_data + j][i] = 0;
	return analyze_ret ? OK : ML_ERROR;
    }
    /* once converged, free dynamic allocated vars */

    if (*lambda == -2) {
	free(d);
	free(tmp_d);
	free(da);
	free(temp_a);
	free(residues);
	free_matr(tmp_C);
	return OK;
    }
    /* Givens calculates in-place, so make working copies of C and d */

    for (j = 0; j < num_data + num_params; j++)
	memcpy(tmp_C[j], C[j], num_params * sizeof(double));
    memcpy(tmp_d, d, num_data * sizeof(double));

    /* fill in additional parts of tmp_C, tmp_d */

    for (i = 0; i < num_params; i++) {
	/* fill in low diag. of tmp_C ... */
	tmp_C[num_data + i][i] = *lambda;
	/* ... and low part of tmp_d */
	tmp_d[num_data + i] = 0;
    }

    /* FIXME: residues[] isn't used at all. Why? Should it be used? */

    Givens(tmp_C, tmp_d, da, residues, num_params + num_data, num_params, 1);

    /* check if trial did ameliorate sum of squares */

    for (j = 0; j < num_params; j++)
	temp_a[j] = a[j] + da[j];

    if (!analyze(temp_a, tmp_C, tmp_d, &tmp_chisq)) {
	/* FIXME: will never be reached: always returns TRUE */
	return ML_ERROR;
    }
    if (tmp_chisq < *chisq) {	/* Success, accept new solution */
	if (*lambda > MIN_LAMBDA) {
	    (void) putc('/', stderr);
	    *lambda /= lambda_down_factor;
	}
	*chisq = tmp_chisq;
	for (j = 0; j < num_data; j++) {
	    memcpy(C[j], tmp_C[j], num_params * sizeof(double));
	    d[j] = tmp_d[j];
	}
	for (j = 0; j < num_params; j++)
	    a[j] = temp_a[j];
	return BETTER;
    } else {			/* failure, increase lambda and return */
	(void) putc('*', stderr);
	*lambda *= lambda_up_factor;
	return WORSE;
    }
}


/*****************************************************************
    compute chi-square and numeric derivations
*****************************************************************/
/* used by marquardt to evaluate the linearized fitting matrix C and
 * vector d, fills in only the top part of C and d I don't use a
 * temporary array zfunc[] any more. Just use d[] instead.  */
/* FIXME: in the new code, this function doesn't really do enough to
 * be useful. Maybe it ought to be deleted, i.e. integrated with
 * calculate() ? */
static TBOOLEAN
analyze(double a[], double **C, double d[], double *chisq)
{
    int i, j;

    *chisq = 0;
    calculate(d, C, a);

    for (i = 0; i < num_data; i++) {
	/* note: order reversed, as used by Schwarz */
	d[i] = (d[i] - fit_z[i]) / err_data[i];
	*chisq += d[i] * d[i];
	for (j = 0; j < num_params; j++)
	    C[i][j] /= err_data[i];
    }
    /* FIXME: why return a value that is always TRUE ? */
    return TRUE;
}


/* To use the more exact, but slower two-side formula, activate the
   following line: */
/*#define TWO_SIDE_DIFFERENTIATION */
/*****************************************************************
    compute function values and partial derivatives of chi-square
*****************************************************************/
static void
calculate(double *zfunc, double **dzda, double a[])
{
    int k, p;
    double tmp_a;
    double *tmp_high, *tmp_pars;
#ifdef TWO_SIDE_DIFFERENTIATION
    double *tmp_low;
#endif

    tmp_high = vec(num_data);	/* numeric derivations */
#ifdef TWO_SIDE_DIFFERENTIATION
    tmp_low = vec(num_data);
#endif
    tmp_pars = vec(num_params);

    /* first function values */

    call_gnuplot(a, zfunc);

    /* then derivatives */

    for (p = 0; p < num_params; p++)
	tmp_pars[p] = a[p];
    for (p = 0; p < num_params; p++) {
	tmp_a = fabs(a[p]) < NEARLY_ZERO ? NEARLY_ZERO : a[p];
	tmp_pars[p] = tmp_a * (1 + DELTA);
	call_gnuplot(tmp_pars, tmp_high);
#ifdef TWO_SIDE_DIFFERENTIATION
	tmp_pars[p] = tmp_a * (1 - DELTA);
	call_gnuplot(tmp_pars, tmp_low);
#endif
	for (k = 0; k < num_data; k++)
#ifdef TWO_SIDE_DIFFERENTIATION
	    dzda[k][p] = (tmp_high[k] - tmp_low[k]) / (2 * tmp_a * DELTA);
#else
	    dzda[k][p] = (tmp_high[k] - zfunc[k]) / (tmp_a * DELTA);
#endif
	tmp_pars[p] = a[p];
    }

#ifdef TWO_SIDE_DIFFERENTIATION
    free(tmp_low);
#endif
    free(tmp_high);
    free(tmp_pars);
}


/*****************************************************************
    call internal gnuplot functions
*****************************************************************/
static void
call_gnuplot(double *par, double *data)
{
    int i;
    struct value v;

    /* set parameters first */
    for (i = 0; i < num_params; i++) {
	(void) Gcomplex(&v, par[i], 0.0);
	setvar(par_name[i], v);
    }

    for (i = 0; i < num_data; i++) {
	/* calculate fit-function value */
	(void) Gcomplex(&func.dummy_values[0], fit_x[i], 0.0);
	(void) Gcomplex(&func.dummy_values[1], fit_y[i], 0.0);
	evaluate_at(func.at, &v);
	if (undefined)
	    Eex("Undefined value during function evaluation");
	data[i] = real(&v);
    }
}


/*****************************************************************
    handle user interrupts during fit
*****************************************************************/
static TBOOLEAN
fit_interrupt()
{
    while (TRUE) {
	fputs("\n\n(S)top fit, (C)ontinue, (E)xecute FIT_SCRIPT:  ", STANDARD);
	switch (getc(stdin)) {

	case EOF:
	case 's':
	case 'S':
	    fputs("Stop.", STANDARD);
	    user_stop = TRUE;
	    return FALSE;

	case 'c':
	case 'C':
	    fputs("Continue.", STANDARD);
	    return TRUE;

	case 'e':
	case 'E':{
		int i;
		struct value v;
		const char *tmp;

		tmp = (fit_script != 0 && *fit_script) ? fit_script : DEFAULT_CMD;
		fprintf(STANDARD, "executing: %s", tmp);
		/* set parameters visible to gnuplot */
		for (i = 0; i < num_params; i++) {
		    (void) Gcomplex(&v, a[i], 0.0);
		    setvar(par_name[i], v);
		}
		safe_strncpy(gp_input_line, tmp, gp_input_line_len);
		(void) do_line();
	    }
	}
    }
    return TRUE;
}


/*****************************************************************
    frame routine for the marquardt-fit
*****************************************************************/
static TBOOLEAN
regress(double a[])
{
    double **covar, *dpar, **C, chisq, last_chisq, lambda;
    int iter, i, j;
    marq_res_t res;

    chisq = last_chisq = INFINITY;
    C = matr(num_data + num_params, num_params);
    lambda = -1;		/* use sign as flag */
    iter = 0;			/* iteration counter  */

    /* ctrlc now serves as Hotkey */
    ctrlc_setup();

    /* Initialize internal variables and 1st chi-square check */
    if ((res = marquardt(a, C, &chisq, &lambda)) == ML_ERROR)
	Eex("FIT: error occurred during fit");
    res = BETTER;

    show_fit(iter, chisq, chisq, a, lambda, STANDARD);
    show_fit(iter, chisq, chisq, a, lambda, log_f);

    /* MAIN FIT LOOP: do the regression iteration */

    /* HBB 981118: initialize new variable 'user_break' */
    user_stop = FALSE;

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
	if (kbhit()) {
	    do {
		getchx();
	    } while (kbhit());
	    ctrlc_flag = TRUE;
	}
#endif

	if (ctrlc_flag) {
	    show_fit(iter, chisq, last_chisq, a, lambda, STANDARD);
	    ctrlc_flag = FALSE;
	    if (!fit_interrupt())	/* handle keys */
		break;
	}
	if (res == BETTER) {
	    iter++;
	    last_chisq = chisq;
	}
	if ((res = marquardt(a, C, &chisq, &lambda)) == BETTER)
	    show_fit(iter, chisq, last_chisq, a, lambda, STANDARD);
    } while ((res != ML_ERROR)
	     && (lambda < MAX_LAMBDA)
	     && ((maxiter == 0) || (iter <= maxiter))
	     && (res == WORSE
		 || ((chisq > NEARLY_ZERO)
		     ? ((last_chisq - chisq) / chisq)
		     : (last_chisq - chisq)) > epsilon
	     )
	);

    /* fit done */

#if !defined(ATARI) && !defined(MTOS)
    /* restore original SIGINT function */
    interrupt_setup();
#endif

    /* HBB 970304: the maxiter patch: */
    if ((maxiter > 0) && (iter > maxiter)) {
	Dblf2("\nMaximum iteration count (%d) reached. Fit stopped.\n", maxiter);
    } else if (user_stop) {
	Dblf2("\nThe fit was stopped by the user after %d iterations.\n", iter);
    } else {
	Dblf2("\nAfter %d iterations the fit converged.\n", iter);
    }

    Dblf2("final sum of squares of residuals : %g\n", chisq);
    if (chisq > NEARLY_ZERO) {
	Dblf2("rel. change during last iteration : %g\n\n", (chisq - last_chisq) / chisq);
    } else {
	Dblf2("abs. change during last iteration : %g\n\n", (chisq - last_chisq));
    }

    if (res == ML_ERROR)
	Eex("FIT: error occurred during fit");

    /* compute errors in the parameters */

#ifdef GP_FIT_ERRVARS
    if (fit_errorvariables)
	/* Set error variable to zero before doing this */
	/* Thus making sure they are created */
	for (i = 0; i < num_params; i++)
	    setvarerr(par_name[i], 0.0);
#endif

    if (num_data == num_params) {
	int k;

	Dblf("\nExactly as many data points as there are parameters.\n");
	Dblf("In this degenerate case, all errors are zero by definition.\n\n");
	Dblf("Final set of parameters \n");
	Dblf("======================= \n\n");
	for (k = 0; k < num_params; k++)
	    Dblf3("%-15.15s = %-15g\n", par_name[k], a[k]);
    } else if (chisq < NEARLY_ZERO) {
	int k;

	Dblf("\nHmmmm.... Sum of squared residuals is zero. Can't compute errors.\n\n");
	Dblf("Final set of parameters \n");
	Dblf("======================= \n\n");
	for (k = 0; k < num_params; k++)
	    Dblf3("%-15.15s = %-15g\n", par_name[k], a[k]);
    } else {
	Dblf2("degrees of freedom (ndf) : %d\n", num_data - num_params);
	Dblf2("rms of residuals      (stdfit) = sqrt(WSSR/ndf)      : %g\n", sqrt(chisq /
										  (num_data - num_params)));
	Dblf2("variance of residuals (reduced chisquare) = WSSR/ndf : %g\n\n", chisq / (num_data - num_params));

	/* get covariance-, Korrelations- and Kurvature-Matrix */
	/* and errors in the parameters                     */

	/* compute covar[][] directly from C */
	Givens(C, 0, 0, 0, num_data, num_params, 0);

	/* Use lower square of C for covar */
	covar = C + num_data;
	Invert_RtR(C, covar, num_params);

	/* calculate unscaled parameter errors in dpar[]: */
	dpar = vec(num_params);
	for (i = 0; i < num_params; i++) {
	    /* FIXME: can this still happen ? */
	    if (covar[i][i] <= 0.0)	/* HBB: prevent floating point exception later on */
		Eex("Calculation error: non-positive diagonal element in covar. matrix");
	    dpar[i] = sqrt(covar[i][i]);
	}

	/* transform covariances into correlations */
	for (i = 0; i < num_params; i++) {
	    /* only lower triangle needs to be handled */
	    for (j = 0; j <= i; j++)
		covar[i][j] /= dpar[i] * dpar[j];
	}

	/* scale parameter errors based on chisq */
	chisq = sqrt(chisq / (num_data - num_params));
	for (i = 0; i < num_params; i++)
	    dpar[i] *= chisq;

	Dblf("Final set of parameters            Asymptotic Standard Error\n");
	Dblf("=======================            ==========================\n\n");

	for (i = 0; i < num_params; i++) {
	    double temp = (fabs(a[i]) < NEARLY_ZERO)
		? 0.0
		: fabs(100.0 * dpar[i] / a[i]);

	    Dblf6("%-15.15s = %-15g  %-3.3s %-12.4g (%.4g%%)\n",
		  par_name[i], a[i], PLUSMINUS, dpar[i], temp);
#ifdef GP_FIT_ERRVARS
	    if (fit_errorvariables)
		setvarerr(par_name[i], dpar[i]);
#endif
	}

	Dblf("\n\ncorrelation matrix of the fit parameters:\n\n");
	Dblf("               ");

	for (j = 0; j < num_params; j++)
	    Dblf2("%-6.6s ", par_name[j]);

	Dblf("\n");
	for (i = 0; i < num_params; i++) {
	    Dblf2("%-15.15s", par_name[i]);
	    for (j = 0; j <= i; j++) {
		/* Only print lower triangle of symmetric matrix */
		Dblf2("%6.3f ", covar[i][j]);
	    }
	    Dblf("\n");
	}

	free(dpar);
    }

    /* HBB 990220: re-imported this snippet from older versions. Finally,
     * some user noticed that it *is* necessary, after all. Not even
     * Carsten Grammes himself remembered what it was for... :-(
     * The thing is: the value of the last parameter is not reset to
     * its original one after the derivatives have been calculated
     */
    /* restore last parameter's value (not done by calculate) */
    {
	struct value val;
	Gcomplex(&val, a[num_params - 1], 0.0);
	setvar(par_name[num_params - 1], val);
    }

    /* call destructor for allocated vars */
    lambda = -2;		/* flag value, meaning 'destruct!' */
    (void) marquardt(a, C, &chisq, &lambda);

    free_matr(C);
    return TRUE;
}


/*****************************************************************
    display actual state of the fit
*****************************************************************/
static void
show_fit(
    int i,
    double chisq, double last_chisq,
    double *a, double lambda,
    FILE *device)
{
    int k;

    fprintf(device, "\n\n\
 Iteration %d\n\
 WSSR        : %-15g   delta(WSSR)/WSSR   : %g\n\
 delta(WSSR) : %-15g   limit for stopping : %g\n\
 lambda	  : %g\n\n%s parameter values\n\n",
	    i, chisq, chisq > NEARLY_ZERO ? (chisq - last_chisq) / chisq : 0.0,
	    chisq - last_chisq, epsilon, lambda,
	    (i > 0 ? "resultant" : "initial set of free"));
    for (k = 0; k < num_params; k++)
	fprintf(device, "%-15.15s = %g\n", par_name[k], a[k]);
}



/*****************************************************************
    is_empty: check for valid string entries
*****************************************************************/
static TBOOLEAN
is_empty(char *s)
{
    while (*s == ' ' || *s == '\t' || *s == '\n')
	s++;
    return (TBOOLEAN) (*s == '#' || *s == '\0');
}


/*****************************************************************
    get next word of a multi-word string, advance pointer
*****************************************************************/
static char *
get_next_word(char **s, char *subst)
{
    char *tmp = *s;

    while (*tmp == ' ' || *tmp == '\t' || *tmp == '=')
	tmp++;
    if (*tmp == '\n' || *tmp == '\0')	/* not found */
	return NULL;
    if ((*s = strpbrk(tmp, " =\t\n")) == NULL)
	*s = tmp + strlen(tmp);
    *subst = **s;
    *(*s)++ = '\0';
    return tmp;
}


/*****************************************************************
    check for variable identifiers
*****************************************************************/
static TBOOLEAN
is_variable(char *s)
{
    while (*s != '\0') {
	if (!isalnum((unsigned char) *s) && *s != '_')
	    return FALSE;
	s++;
    }
    return TRUE;
}

/*****************************************************************
    first time settings
*****************************************************************/
void
init_fit()
{
    func.at = (struct at_type *) NULL;	/* need to parse 1 time */
}


/*****************************************************************
	    Set a GNUPLOT user-defined variable
******************************************************************/

static void
setvar(char *varname, struct value data)
{
    struct udvt_entry *udv_ptr = add_udv_by_name(varname);
    udv_ptr->udv_value = data;
    udv_ptr->udv_undef = FALSE;
}

#ifdef GP_FIT_ERRVARS
/*****************************************************************
            Set a GNUPLOT user-defined variable for an error
            variable: so take the parameter name, turn it
            into an error parameter name (e.g. a to a_err)
            and then set it.
******************************************************************/
static void
setvarerr(char *varname, double value)
{
	struct value errval;    /* This will hold the gnuplot value created from value*/
	char* pErrValName;      /* The name of the (new) error variable */

	/* Create the variable name by appending _err */
	pErrValName = gp_alloc(strlen(varname)+sizeof(char)+4, "");

	sprintf(pErrValName,"%s_err",varname);
	Gcomplex(&errval, value, 0.0);
	setvar(pErrValName, errval);
	free(pErrValName);
}
#endif /* GP_FIT_ERRVARS */

/*****************************************************************
    Read INTGR Variable value, return 0 if undefined or wrong type
*****************************************************************/
static int
getivar(const char *varname)
{
    struct udvt_entry *udv_ptr = first_udv;

    while (udv_ptr) {
	if (!strcmp(varname, udv_ptr->udv_name))
	    return udv_ptr->udv_value.type == INTGR
		? udv_ptr->udv_value.v.int_val	/* valid */
		: 0;		/* wrong type */
	udv_ptr = udv_ptr->next_udv;
    }
    return 0;			/* not in table */
}


/*****************************************************************
    Read DOUBLE Variable value, return 0 if undefined or wrong type
   I dont think it's a problem that it's an integer - div
*****************************************************************/
static double
getdvar(const char *varname)
{
    struct udvt_entry *udv_ptr = first_udv;

    for (; udv_ptr; udv_ptr = udv_ptr->next_udv)
	if (strcmp(varname, udv_ptr->udv_name) == 0)
	    return real(&(udv_ptr->udv_value));

    /* get here => not found */
    return 0;
}

/*****************************************************************
   like getdvar, but
   - convert it from integer to real if necessary
   - create it with value INITIAL_VALUE if not found or undefined
*****************************************************************/
static double
createdvar(char *varname, double value)
{
    struct udvt_entry *udv_ptr = first_udv;

    for (; udv_ptr; udv_ptr = udv_ptr->next_udv)
	if (strcmp(varname, udv_ptr->udv_name) == 0) {
	    if (udv_ptr->udv_undef) {
		udv_ptr->udv_undef = 0;
		(void) Gcomplex(&udv_ptr->udv_value, value, 0.0);
	    } else if (udv_ptr->udv_value.type == INTGR) {
		(void) Gcomplex(&udv_ptr->udv_value, (double) udv_ptr->udv_value.v.int_val, 0.0);
	    }
	    return real(&(udv_ptr->udv_value));
	}
    /* get here => not found */

    {
	struct value tempval;
	(void) Gcomplex(&tempval, value, 0.0);
	setvar(varname, tempval);
    }

    return value;
}


/*****************************************************************
    Split Identifier into path and filename
*****************************************************************/
static void
splitpath(char *s, char *p, char *f)
{
    char *tmp = s + strlen(s) - 1;

    while (tmp >= s && *tmp != '\\' && *tmp != '/' && *tmp != ':')
	tmp--;
    /* FIXME HBB 20010121: unsafe! Sizes of 'f' and 'p' are not known.
     * May write past buffer end. */
    strcpy(f, tmp + 1);
    memcpy(p, s, (size_t) (tmp - s + 1));
    p[tmp - s + 1] = NUL;
}


/* argument: char *fn */
#define VALID_FILENAME(fn) ((fn) != NULL && (*fn) != '\0')

/*****************************************************************
    write the actual parameters to start parameter file
*****************************************************************/
void
update(char *pfile, char *npfile)
{
    char fnam[256], path[256], sstr[256], pname[64], tail[127], *s = sstr, *tmp, c;
    char ifilename[256], *ofilename;
    FILE *of, *nf;
    double pval;


    /* update pfile npfile:
       if npfile is a valid file name,
       take pfile as input file and
       npfile as output file
     */
    if (VALID_FILENAME(npfile)) {
	safe_strncpy(ifilename, pfile, sizeof(ifilename));
	ofilename = npfile;
    } else {
#ifdef BACKUP_FILESYSTEM
	/* filesystem will keep original as previous version */
	safe_strncpy(ifilename, pfile, sizeof(ifilename));
#else
	backup_file(ifilename, pfile);	/* will Eex if it fails */
#endif
	ofilename = pfile;
    }

    /* split into path and filename */
    splitpath(ifilename, path, fnam);
    if (!(of = loadpath_fopen(ifilename, "r")))
	Eex2("parameter file %s could not be read", ifilename);

    if (!(nf = fopen(ofilename, "w")))
	Eex2("new parameter file %s could not be created", ofilename);

    while (fgets(s = sstr, sizeof(sstr), of) != NULL) {

	if (is_empty(s)) {
	    fputs(s, nf);	/* preserve comments */
	    continue;
	}
	if ((tmp = strchr(s, '#')) != NULL) {
	    safe_strncpy(tail, tmp, sizeof(tail));
	    *tmp = NUL;
	} else
	    strcpy(tail, "\n");

	tmp = get_next_word(&s, &c);
	if (!is_variable(tmp) || strlen(tmp) > MAX_ID_LEN) {
	    (void) fclose(nf);
	    (void) fclose(of);
	    Eex2("syntax error in parameter file %s", fnam);
	}
	safe_strncpy(pname, tmp, sizeof(pname));
	/* next must be '=' */
	if (c != '=') {
	    tmp = strchr(s, '=');
	    if (tmp == NULL) {
		(void) fclose(nf);
		(void) fclose(of);
		Eex2("syntax error in parameter file %s", fnam);
	    }
	    s = tmp + 1;
	}
	tmp = get_next_word(&s, &c);
	if (!sscanf(tmp, "%lf", &pval)) {
	    (void) fclose(nf);
	    (void) fclose(of);
	    Eex2("syntax error in parameter file %s", fnam);
	}
	if ((tmp = get_next_word(&s, &c)) != NULL) {
	    (void) fclose(nf);
	    (void) fclose(of);
	    Eex2("syntax error in parameter file %s", fnam);
	}
	/* now modify */

	if ((pval = getdvar(pname)) == 0)
	    pval = (double) getivar(pname);

	sprintf(sstr, "%g", pval);
	if (!strchr(sstr, '.') && !strchr(sstr, 'e'))
	    strcat(sstr, ".0");	/* assure CMPLX-type */

	fprintf(nf, "%-15.15s = %-15.15s   %s", pname, sstr, tail);
    }

    if (fclose(nf) || fclose(of))
	Eex("I/O error during update");

}


/*****************************************************************
    Backup a file by renaming it to something useful. Return
    the new name in tofile
*****************************************************************/

/* tofile must point to a char array[] or allocated data. See update() */

static void
backup_file(char *tofile, const char *fromfile)
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
    while ((tmpn = strchr(tofile, '.')) != NULL)
	*tmpn = '_';
#endif /*VMS */

    strcat(tofile, BACKUP_SUFFIX);

    if (rename(fromfile, tofile) == 0)
	return;			/* hurrah */
#endif


#if defined (WIN32) || defined(MSDOS)

    /* first attempt for msdos. Second attempt for win32s */

    /* Copy only the first 8 characters of the filename, to comply
     * with the restrictions of FAT filesystems. */
    safe_strncpy(tofile, fromfile, 8 + 1);

    while ((tmpn = strchr(tofile, '.')) != NULL)
	*tmpn = '_';

    strcat(tofile, BACKUP_SUFFIX);

    if (rename(fromfile, tofile) == 0)
	return;			/* success */

#endif /* win32 || msdos */

    /* get here => rename failed. */
    Eex3("Could not rename file %s to %s", fromfile, tofile);
}

/* A modified copy of save.c:save_range(), but this one reports
 * _current_ values, not the 'set' ones, by default */
static void
log_axis_restriction(FILE *log_f, AXIS_INDEX axis)
{
    char s[80];
    AXIS *this_axis = axis_array + axis;

    fprintf(log_f, "\t%s range restricted to [", axis_defaults[axis].name);
    if (this_axis->autoscale & AUTOSCALE_MIN) {
	putc('*', log_f);
    } else if (this_axis->is_timedata) {
	putc('"', log_f);
	gstrftime(s, 80, this_axis->timefmt, this_axis->min);
	fputs(s, log_f);
	putc('"', log_f);
    } else {
	fprintf(log_f, "%#g", this_axis->min);
    }

    fputs(" : ", log_f);
    if (this_axis->autoscale & AUTOSCALE_MAX) {
	putc('*', log_f);
    } else if (this_axis->is_timedata) {
	putc('"', log_f);
	gstrftime(s, 80, this_axis->timefmt, this_axis->max);
	fputs(s, log_f);
	putc('"', log_f);
    } else {
	fprintf(log_f, "%#g", this_axis->max);
    }
    fputs("]\n", log_f);
}

/*****************************************************************
    Interface to the classic gnuplot-software
*****************************************************************/

void
fit_command()
{
/* HBB 20000430: revised this completely, to make it more similar to
 * what plot3drequest() does */

    int dummy_x = -1, dummy_y = -1;	/* eg  fit [u=...] [v=...] */
    int zrange_token = -1;
    TBOOLEAN is_a_3d_fit;

    int i;
    double v[4];
    double tmpd;
    time_t timer;
    int token1, token2, token3;
    char *tmp, *file_name;

    c_token++;

    /* first look for a restricted x fit range... */

    /* put stuff into arrays to simplify access */
    AXIS_INIT3D(FIRST_X_AXIS, 0, 0);
    AXIS_INIT3D(FIRST_Y_AXIS, 0, 0);
    AXIS_INIT3D(FIRST_Z_AXIS, 0, 1);

    /* use global default indices in axis.c to simplify access to
     * per-axis variables */
    x_axis = FIRST_X_AXIS;
    y_axis = FIRST_Y_AXIS;
    z_axis = FIRST_Z_AXIS;

    PARSE_NAMED_RANGE(FIRST_X_AXIS, dummy_x);
    PARSE_NAMED_RANGE(FIRST_Y_AXIS, dummy_y);
    /* HBB 980401: new: allow restricting the z range as well */
    if (equals(c_token, "["))
      zrange_token = c_token;
    PARSE_RANGE(FIRST_Z_AXIS);


    /* now compile the function */

    token1 = c_token;

    if (func.at) {
	free_at(func.at);
	func.at = NULL;		/* in case perm_at() does int_error */
    }
    dummy_func = &func;


    /* set the dummy variable names */

    if (dummy_x >= 0)
	copy_str(c_dummy_var[0], dummy_x, MAX_ID_LEN);
    else
	strcpy(c_dummy_var[0], set_dummy_var[0]);

    if (dummy_y >= 0)
	copy_str(c_dummy_var[0], dummy_y, MAX_ID_LEN);
    else
	strcpy(c_dummy_var[1], set_dummy_var[1]);

    func.at = perm_at();
    dummy_func = NULL;

    token2 = c_token;

    /* get filename */
    file_name = try_to_get_string();
    if (!file_name)
	int_error(c_token, "missing filename");

    /* use datafile module to parse the datafile and qualifiers */
    df_set_plot_mode(MODE_QUERY);  /* Does nothing except for binary datafiles */
    columns = df_open(file_name, 4);	/* up to 4 using specs allowed */
    free(file_name);
    if (columns < 0)
	int_error(NO_CARET,"Can't read data file");
    if (columns == 1)
	int_error(c_token, "Need 2 to 4 using specs");
    is_a_3d_fit = (columns == 4);

    /* The following patch was made by Remko Scharroo, 25-Mar-1999
     * We need to check if one of the columns is time data, like
     * in plot2d and plot3d */

    if (X_AXIS.is_timedata) {
	if (columns < 2)
	    int_error(c_token, "Need full using spec for x time data");
    }
    if (Y_AXIS.is_timedata) {
	if (columns < 1)
	    int_error(c_token, "Need using spec for y time data");
    }
    df_axis[0] = FIRST_X_AXIS;
    df_axis[1] = (is_a_3d_fit) ? FIRST_Y_AXIS : FIRST_Z_AXIS;
    /* don't parse delta_z as times */
    df_axis[2] = (is_a_3d_fit) ? FIRST_Z_AXIS : -1;
    df_axis[3] = -1;

    /* HBB 20000430: No need for such a check for the z axis. That
     * axis is only used iff columns==4, i.e. the detection method for
     * 3d fits requires a 4 column 'using', already. */
    /* End of patch by Remko Scharroo */


    /* HBB 980401: if this is a single-variable fit, we shouldn't have
     * allowed a variable name specifier for 'y': */
    if ((dummy_y >= 0) && !is_a_3d_fit)
	int_error(dummy_y, "Can't re-name 'y' in a one-variable fit");

    /* HBB 981210: two range specs mean different things, depending
     * on wether this is a 2D or 3D fit */
    if (!is_a_3d_fit) {
	if (zrange_token != -1)
	    int_error(zrange_token, "Three range-specs not allowed in on-variable fit");
	else {
	    /* 2D fit, 2 ranges: second range is for *z*, not y: */
	    Z_AXIS.autoscale = Y_AXIS.autoscale;
	    if (!(Y_AXIS.autoscale & AUTOSCALE_MIN))
		Z_AXIS.min = Y_AXIS.min;
	    if (!(Y_AXIS.autoscale & AUTOSCALE_MAX))
		Z_AXIS.max = Y_AXIS.max;
	}
    }
    /* defer actually reading the data until we have parsed the rest
     * of the line */

    token3 = c_token;

    tmpd = getdvar(FITLIMIT);	/* get epsilon if given explicitly */
    if (tmpd < 1.0 && tmpd > 0.0)
	epsilon = tmpd;

    /* HBB 970304: maxiter patch */
    maxiter = getivar(FITMAXITER);

    /* get startup value for lambda, if given */
    tmpd = getdvar(FITSTARTLAMBDA);

    if (tmpd > 0.0) {
	startup_lambda = tmpd;
	printf("Lambda Start value set: %g\n", startup_lambda);
    }
    /* get lambda up/down factor, if given */
    tmpd = getdvar(FITLAMBDAFACTOR);

    if (tmpd > 0.0) {
	lambda_up_factor = lambda_down_factor = tmpd;
	printf("Lambda scaling factors reset:  %g\n", lambda_up_factor);
    }
    *fit_script = NUL;
    if ((tmp = getenv(FITSCRIPT)) != NULL)
	safe_strncpy(fit_script, tmp, sizeof(fit_script));

    {
	char *logfile = getfitlogfile();

	if (!log_f && !(log_f = fopen(logfile, "a")))
	    Eex2("could not open log-file %s", logfile);

	if (logfile)
	    free(logfile);
    }

    fputs("\n\n*******************************************************************************\n", log_f);
    (void) time(&timer);
    fprintf(log_f, "%s\n\n", ctime(&timer));
    {
	char *line = NULL;

	m_capture(&line, token2, token3 - 1);
	fprintf(log_f, "FIT:    data read from %s\n", line);
	free(line);
    }

    /* HBB 20040201: somebody insisted they need this ... */
    if (AUTOSCALE_BOTH != (X_AXIS.autoscale & AUTOSCALE_BOTH))
	log_axis_restriction(log_f, x_axis);
    if (AUTOSCALE_BOTH != (Y_AXIS.autoscale & AUTOSCALE_BOTH))
	log_axis_restriction(log_f, y_axis);

    max_data = MAX_DATA;
    fit_x = vec(max_data);	/* start with max. value */
    fit_y = vec(max_data);
    fit_z = vec(max_data);

    /* first read in experimental data */

    err_data = vec(max_data);
    num_data = 0;

    while ((i = df_readline(v, 4)) != DF_EOF) {
	if (num_data >= max_data) {
	    /* increase max_data by factor of 1.5 */
	    max_data = (max_data * 3) / 2;
	    if (0
		|| !redim_vec(&fit_x, max_data)
		|| !redim_vec(&fit_y, max_data)
		|| !redim_vec(&fit_z, max_data)
		|| !redim_vec(&err_data, max_data)
		) {
		/* Some of the reallocations went bad: */
		df_close();
		Eex2("Out of memory in fit: too many datapoints (%d)?", max_data);
	    } else {
		/* Just so we know that the routine is at work: */
		fprintf(STANDARD, "Max. number of data points scaled up to: %d\n",
			max_data);
	    }
	} /* if (need to extend storage space) */

	switch (i) {
	case DF_MISSING:
	case DF_UNDEFINED:
	case DF_FIRST_BLANK:
	case DF_SECOND_BLANK:
	    continue;
	case 0:
	    Eex2("bad data on line %d of datafile", df_line_number);
	    break;
	case 1:		/* only z provided */
	    v[2] = v[0];
	    v[0] = (double) df_datum;
	    break;
	case 2:		/* x and z */
	    v[2] = v[1];
	    break;

	    /* only if the explicitly asked for 4 columns do we
	     * do a 3d fit. (We can get here if they didn't
	     * specify a using spec, and the file has 4 columns
	     */
	case 4:		/* x, y, z, error */
	    if (is_a_3d_fit)
		break;
	    /* else fall through */
	case 3:		/* x, z, error */
	    v[3] = v[2];	/* error */
	    v[2] = v[1];	/* z */
	    break;

	}

	/* skip this point if it is out of range */
	if (!(X_AXIS.autoscale & AUTOSCALE_MIN)
	    && (v[0] < X_AXIS.min))
	    continue;
	if (!(X_AXIS.autoscale & AUTOSCALE_MAX)
	    && (v[0] > X_AXIS.max))
	    continue;
	if (is_a_3d_fit) {
	    if (!(Y_AXIS.autoscale & AUTOSCALE_MIN)
		&& (v[1] < Y_AXIS.min))
		continue;
	    if (!(Y_AXIS.autoscale & AUTOSCALE_MAX)
		&& (v[1] > Y_AXIS.max))
		continue;
	}
	/* HBB 980401: check *z* range for all fits */
	if (!(Z_AXIS.autoscale & AUTOSCALE_MIN)
	    && (v[2] < Z_AXIS.min))
	    continue;
	if (!(Z_AXIS.autoscale & AUTOSCALE_MAX)
	    && (v[2] > Z_AXIS.max))
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

    /* now resize fields to actual length: */
    redim_vec(&fit_x, num_data);
    redim_vec(&fit_y, num_data);
    redim_vec(&fit_z, num_data);
    redim_vec(&err_data, num_data);

    fprintf(log_f, "        #datapoints = %d\n", num_data);

    if (columns < 3)
	fputs("        residuals are weighted equally (unit weight)\n\n", log_f);

    {
	char *line = NULL;

	m_capture(&line, token1, token2 - 1);
	fprintf(log_f, "function used for fitting: %s\n", line);
	free(line);
    }

    /* read in parameters */

    max_params = MAX_PARAMS;	/* HBB 971023: make this resizeable */

    if (!equals(c_token, "via"))
	int_error(c_token, "Need via and either parameter list or file");

    a = vec(max_params);
    par_name = (fixstr *) gp_alloc((max_params + 1) * sizeof(fixstr),
				   "fit param");
    num_params = 0;

    if (isstring(++c_token)) {	/* It's a parameter *file* */
	TBOOLEAN fixed;
	double tmp_par;
	char c, *s;
	char sstr[MAX_LINE_LEN + 1];
	FILE *f;

	quote_str(sstr, c_token++, MAX_LINE_LEN);

	/* get parameters and values out of file and ignore fixed ones */

	fprintf(log_f, "fitted parameters and initial values from file: %s\n\n", sstr);
	if (!(f = loadpath_fopen(sstr, "r")))
	    Eex2("could not read parameter-file %s", sstr);
	while (TRUE) {
	    if (!fgets(s = sstr, (int) sizeof(sstr), f))	/* EOF found */
		break;
	    if ((tmp = strstr(s, GP_FIXED)) != NULL) {	/* ignore fixed params */
		*tmp = NUL;
		fprintf(log_f, "FIXED:  %s\n", s);
		fixed = TRUE;
	    } else
		fixed = FALSE;
	    if ((tmp = strchr(s, '#')) != NULL)
		*tmp = NUL;
	    if (is_empty(s))
		continue;
	    tmp = get_next_word(&s, &c);
	    if (!is_variable(tmp) || strlen(tmp) > MAX_ID_LEN) {
		(void) fclose(f);
		Eex("syntax error in parameter file");
	    }
	    safe_strncpy(par_name[num_params], tmp, sizeof(fixstr));
	    /* next must be '=' */
	    if (c != '=') {
		tmp = strchr(s, '=');
		if (tmp == NULL) {
		    (void) fclose(f);
		    Eex("syntax error in parameter file");
		}
		s = tmp + 1;
	    }
	    tmp = get_next_word(&s, &c);
	    if (sscanf(tmp, "%lf", &tmp_par) != 1) {
		(void) fclose(f);
		Eex("syntax error in parameter file");
	    }
	    /* make fixed params visible to GNUPLOT */
	    if (fixed) {
		struct value tempval;
		(void) Gcomplex(&tempval, tmp_par, 0.0);
		/* use parname as temp */
		setvar(par_name[num_params], tempval);
	    } else {
		if (num_params >= max_params) {
		    max_params = (max_params * 3) / 2;
		    if (0
			|| !redim_vec(&a, max_params)
			|| !(par_name = gp_realloc(par_name, (max_params + 1) * sizeof(fixstr), "fit param resize"))
			) {
			(void) fclose(f);
			Eex("Out of memory in fit: too many parameters?");
		    }
		}
		a[num_params++] = tmp_par;
	    }

	    if ((tmp = get_next_word(&s, &c)) != NULL) {
		(void) fclose(f);
		Eex("syntax error in parameter file");
	    }
	}
	(void) fclose(f);

    } else {
	/* not a string after via: it's a variable listing */

	fputs("fitted parameters initialized with current variable values\n\n", log_f);
	do {
	    if (!isletter(c_token))
		Eex("no parameter specified");
	    capture(par_name[num_params], c_token, c_token, (int) sizeof(par_name[0]));
	    if (num_params >= max_params) {
		max_params = (max_params * 3) / 2;
		if (0
		    || !redim_vec(&a, max_params)
		    || !(par_name = gp_realloc(par_name, (max_params + 1) * sizeof(fixstr), "fit param resize"))
		    ) {
		    Eex("Out of memory in fit: too many parameters?");
		}
	    }
	    /* create variable if it doesn't exist */
	    a[num_params] = createdvar(par_name[num_params], INITIAL_VALUE);
	    ++num_params;
	} while (equals(++c_token, ",") && ++c_token);
    }

    redim_vec(&a, num_params);
    par_name = (fixstr *) gp_realloc(par_name, (num_params + 1) * sizeof(fixstr), "fit param");

    if (num_data < num_params)
	Eex("Number of data points smaller than number of parameters");

    /* avoid parameters being equal to zero */
    for (i = 0; i < num_params; i++)
	if (a[i] == 0)
	    a[i] = NEARLY_ZERO;

    (void) regress(a);

    (void) fclose(log_f);
    log_f = NULL;
    free(fit_x);
    free(fit_y);
    free(fit_z);
    free(err_data);
    free(a);
    free(par_name);
    if (func.at) {
	free_at(func.at);		/* release perm. action table */
	func.at = (struct at_type *) NULL;
    }
    safe_strncpy(last_fit_command, gp_input_line, sizeof(last_fit_command));
}

#if defined(ATARI) || defined(MTOS)
static int
kbhit()
{
    fd_set rfds;
    struct timeval timeout;

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    rfds = (fd_set) (1L << fileno(stdin));
    return ((select(0, SELECT_TYPE_ARG234 &rfds, NULL, NULL, SELECT_TYPE_ARG5 &timeout) > 0) ? 1 : 0);
}
#endif

/*
 * Print msg to stderr and log file
 */
#if defined(VA_START) && defined(STDC_HEADERS)
static void
Dblfn(const char *fmt, ...)
#else
static void
Dblfn(const char *fmt, va_dcl)
#endif
{
#ifdef VA_START
    va_list args;

    VA_START(args, fmt);
# if defined(HAVE_VFPRINTF) || _LIBC
    vfprintf(STANDARD, fmt, args);
    va_end(args);
    VA_START(args, fmt);
    vfprintf(log_f, fmt, args);
# else
    _doprnt(fmt, args, STANDARD);
    _doprnt(fmt, args, log_f);
# endif
    va_end(args);
#else
    fprintf(STANDARD, fmt, a1, a2, a3, a4, a5, a6, a7, a8);
    fprintf(log_f, fmt, a1, a2, a3, a4, a5, a6, a7, a8);
#endif /* VA_START */
}

/* HBB/H.Harders NEW 20020927: make fit log filename exchangeable at
 * run-time, not only by setting an environment variable. */
char *
getfitlogfile()
{
    char *logfile = NULL;

    if (fitlogfile == NULL) {
	char *tmp = getenv(GNUFITLOG);	/* open logfile */

	if (tmp != NULL && *tmp != '\0') {
	    char *tmp2 = tmp + (strlen(tmp) - 1);

	    /* if given log file name ends in path separator, treat it
	     * as a directory to store the default "fit.log" in */
	    if (*tmp2 == '/' || *tmp2 == '\\') {
		logfile = gp_alloc(strlen(tmp)
				   + strlen(fitlogfile_default) + 1,
				   "logfile");
		strcpy(logfile, tmp);
		strcat(logfile, fitlogfile_default);
	    } else {
		logfile = gp_strdup(tmp);
	    }
	} else {
	    logfile = gp_strdup(fitlogfile_default);
	}
    } else {
	logfile = gp_strdup(fitlogfile);
    }
    return logfile;
}
