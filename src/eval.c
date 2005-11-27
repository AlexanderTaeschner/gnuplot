#ifndef lint
static char *RCSid() { return RCSid("$Id: eval.c,v 1.38 2005/08/12 17:42:32 sfeam Exp $"); }
#endif

/* GNUPLOT - eval.c */

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

/* HBB 20010724: I moved several variables and functions from parse.c
 * to here, because they're involved with *evaluating* functions, not
 * with parsing them: evaluate_at(), fpe(), the APOLLO signal handling
 * stuff, and fpe_env */

#include "eval.h"

#include "syscfg.h"
#include "alloc.h"
#include "datafile.h"
#include "internal.h"
#include "specfun.h"
#include "standard.h"
#include "util.h"

#include <signal.h>
#include <setjmp.h>

/* Internal prototypes */
static RETSIGTYPE fpe __PROTO((int an_int));
#ifdef APOLLO
static pfm_$fh_func_val_t apollo_sigfpe(pfm_$fault_rec_t & fault_rec)
#endif

/* Global variables exported by this module */
struct udvt_entry udv_pi = { NULL, "pi", FALSE, {INTGR, {0} } };
/* first in linked list */
struct udvt_entry *first_udv = &udv_pi;
struct udft_entry *first_udf = NULL;

TBOOLEAN undefined;

/* The stack this operates on */
static struct value stack[STACK_DEPTH];
static int s_p = -1;		/* stack pointer */
#define top_of_stack stack[s_p]

static int jump_offset;		/* to be modified by 'jump' operators */

/* The table of built-in functions */
/* HBB 20010725: I've removed all the casts to type (FUNC_PTR) ---
 * According to ANSI/ISO C Standards it causes undefined behaviouf if
 * you cast a function pointer to any other type, including a function
 * pointer with a different set of arguments, and then call the
 * function.  Instead, I made all these functions adhere to the common
 * type, directly */
const struct ft_entry GPFAR ft[] =
{
    /* internal functions: */
    {"push",  f_push},
    {"pushc",  f_pushc},
    {"pushd1",  f_pushd1},
    {"pushd2",  f_pushd2},
    {"pushd",  f_pushd},
    {"pushv", f_pushv},
    {"call",  f_call},
    {"calln",  f_calln},
    {"lnot",  f_lnot},
    {"bnot",  f_bnot},
    {"uminus",  f_uminus},
    {"lor",  f_lor},
    {"land",  f_land},
    {"bor",  f_bor},
    {"xor",  f_xor},
    {"band",  f_band},
    {"eq",  f_eq},
    {"ne",  f_ne},
    {"gt",  f_gt},
    {"lt",  f_lt},
    {"ge",  f_ge},
    {"le",  f_le},
    {"plus",  f_plus},
    {"minus",  f_minus},
    {"mult",  f_mult},
    {"div",  f_div},
    {"mod",  f_mod},
    {"power",  f_power},
    {"factorial",  f_factorial},
    {"bool",  f_bool},
    {"dollars",  f_dollars},	/* for using extension */
#ifdef GP_STRING_VARS
    {"concatenate",  f_concatenate},	/* for string variables only */
    {"eqs",  f_eqs},			/* for string variables only */
    {"nes",  f_nes},			/* for string variables only */
    {"[]",  f_range},			/* for string variables only */
#endif
    {"jump",  f_jump},
    {"jumpz",  f_jumpz},
    {"jumpnz",  f_jumpnz},
    {"jtern",  f_jtern},

/* standard functions: */
    {"real",  f_real},
    {"imag",  f_imag},
    {"arg",  f_arg},
    {"conjg",  f_conjg},
    {"sin",  f_sin},
    {"cos",  f_cos},
    {"tan",  f_tan},
    {"asin",  f_asin},
    {"acos",  f_acos},
    {"atan",  f_atan},
    {"atan2",  f_atan2},
    {"sinh",  f_sinh},
    {"cosh",  f_cosh},
    {"tanh",  f_tanh},
    {"int",  f_int},
    {"abs",  f_abs},
    {"sgn",  f_sgn},
    {"sqrt",  f_sqrt},
    {"exp",  f_exp},
    {"log10",  f_log10},
    {"log",  f_log},
    {"besj0",  f_besj0},
    {"besj1",  f_besj1},
    {"besy0",  f_besy0},
    {"besy1",  f_besy1},
    {"erf",  f_erf},
    {"erfc",  f_erfc},
    {"gamma",  f_gamma},
    {"lgamma",  f_lgamma},
    {"ibeta",  f_ibeta},
    {"igamma",  f_igamma},
    {"rand",  f_rand},
    {"floor",  f_floor},
    {"ceil",  f_ceil},
    {"defined",  f_isvar},       /* isvar function */

    {"norm",  f_normal},	/* XXX-JG */
    {"inverf",  f_inverse_erf},	/* XXX-JG */
    {"invnorm",  f_inverse_normal},	/* XXX-JG */
    {"asinh",  f_asinh},
    {"acosh",  f_acosh},
    {"atanh",  f_atanh},
    {"lambertw",  f_lambertw}, /* HBB, from G.Kuhnle 20001107 */

    {"column",  f_column},	/* for using */
    {"valid",  f_valid},	/* for using */
    {"timecolumn",  f_timecolumn},	/* for using */

    {"tm_sec",  f_tmsec},	/* for timeseries */
    {"tm_min",  f_tmmin},	/* for timeseries */
    {"tm_hour",  f_tmhour},	/* for timeseries */
    {"tm_mday",  f_tmmday},	/* for timeseries */
    {"tm_mon",  f_tmmon},	/* for timeseries */
    {"tm_year",  f_tmyear},	/* for timeseries */
    {"tm_wday",  f_tmwday},	/* for timeseries */
    {"tm_yday",  f_tmyday},	/* for timeseries */

#ifdef GP_STRING_VARS
    {"stringcolumn",  f_stringcolumn},	/* for using */
    {"sprintf",  f_sprintf},	/* for string variables only */
    {"gprintf",  f_gprintf},	/* for string variables only */
    {"strlen",  f_strlen},	/* for string variables only */
    {"strstrt",  f_strstrt},	/* for string variables only */
    {"substr",  f_range},	/* for string variables only */
    {"word", f_words},		/* for string variables only */
    {"words", f_words},		/* implemented as word(s,-1) */
    {"system", f_system},       /* "dynamic backtics" */
#endif

    {NULL, NULL}
};

/* Module-local variables: */

#if defined(_Windows) && !defined(WIN32)
static JMP_BUF far fpe_env;
#else
static JMP_BUF fpe_env;
#endif

/* Internal helper functions: */

static RETSIGTYPE
fpe(int an_int)
{
#if defined(MSDOS) && !defined(__EMX__) && !defined(DJGPP) && !defined(_Windows) || defined(DOS386)
    /* thanks to lotto@wjh12.UUCP for telling us about this  */
    _fpreset();
#endif

    (void) an_int;		/* avoid -Wunused warning */
    (void) signal(SIGFPE, (sigfunc) fpe);
    undefined = TRUE;
    LONGJMP(fpe_env, TRUE);
}

/* FIXME HBB 20010724: do we really want this in *here*? Maybe it
 * should be in syscfg.c or somewhere similar. */
#ifdef APOLLO
# include <apollo/base.h>
# include <apollo/pfm.h>
# include <apollo/fault.h>

/*
 * On an Apollo, the OS can signal a couple errors that are not mapped into
 * SIGFPE, namely signalling NaN and branch on an unordered comparison.  I
 * suppose there are others, but none of these are documented, so I handle
 * them as they arise.
 *
 * Anyway, we need to catch these faults and signal SIGFPE.
 */

static pfm_$fh_func_val_t
apollo_sigfpe(pfm_$fault_rec_t & fault_rec)
{
    kill(getpid(), SIGFPE);
    return pfm_$continue_fault_handling;
}

/* This is called from main(), if the platform is an APOLLO */
void
apollo_pfm_catch()
{
    status_$t status;
    pfm_$establish_fault_handler(fault_$fp_bsun, pfm_$fh_backstop,
				 apollo_sigfpe, &status);
    pfm_$establish_fault_handler(fault_$fp_sig_nan, pfm_$fh_backstop,
				 apollo_sigfpe, &status);
}
#endif /* APOLLO */

/* Exported functions */

/* First, some functions tha help other modules use 'struct value' ---
 * these might justify a separate module, but I'll stick with this,
 * for now */

/* returns the real part of val */
double
real(struct value *val)
{
    switch (val->type) {
    case INTGR:
	return ((double) val->v.int_val);
    case CMPLX:
	return (val->v.cmplx_val.real);
#ifdef GP_STRING_VARS
    case STRING:              /* is this ever used? */
	return (atof(val->v.string_val));
#endif
    default:
	int_error(NO_CARET, "unknown type in real()");
    }
    /* NOTREACHED */
    return ((double) 0.0);
}


/* returns the imag part of val */
double
imag(struct value *val)
{
    switch (val->type) {
    case INTGR:
	return (0.0);
    case CMPLX:
	return (val->v.cmplx_val.imag);
#ifdef GP_STRING_VARS
    case STRING:
	/* This is where we end up if the user tries: */
	/*     x = 2;  plot sprintf(format,x)         */
	int_warn(NO_CARET, "encountered a string when expecting a number");
	int_error(NO_CARET, "Did you try to generate a file name using dummy variable x or y?");
#endif
    default:
	int_error(NO_CARET, "unknown type in imag()");
    }
    /* NOTREACHED */
    return ((double) 0.0);
}



/* returns the magnitude of val */
double
magnitude(struct value *val)
{
    switch (val->type) {
    case INTGR:
	return ((double) abs(val->v.int_val));
    case CMPLX:
	return (sqrt(val->v.cmplx_val.real *
		     val->v.cmplx_val.real +
		     val->v.cmplx_val.imag *
		     val->v.cmplx_val.imag));
    default:
	int_error(NO_CARET, "unknown type in magnitude()");
    }
    /* NOTREACHED */
    return ((double) 0.0);
}



/* returns the angle of val */
double
angle(struct value *val)
{
    switch (val->type) {
    case INTGR:
	return ((val->v.int_val >= 0) ? 0.0 : M_PI);
    case CMPLX:
	if (val->v.cmplx_val.imag == 0.0) {
	    if (val->v.cmplx_val.real >= 0.0)
		return (0.0);
	    else
		return (M_PI);
	}
	return (atan2(val->v.cmplx_val.imag,
		      val->v.cmplx_val.real));
    default:
	int_error(NO_CARET, "unknown type in angle()");
    }
    /* NOTREACHED */
    return ((double) 0.0);
}


struct value *
Gcomplex(struct value *a, double realpart, double imagpart)
{
    a->type = CMPLX;
    a->v.cmplx_val.real = realpart;
    a->v.cmplx_val.imag = imagpart;
    return (a);
}


struct value *
Ginteger(struct value *a, int i)
{
    a->type = INTGR;
    a->v.int_val = i;
    return (a);
}

#ifdef GP_STRING_VARS
struct value *
Gstring(struct value *a, char *s)
{
    a->type = STRING;
    a->v.string_val = s;
    return (a);
}
#endif

/* It is always safe to call gpfree_string with a->type is INTGR or CMPLX.
 * However it would be fatal to call it with a->type = STRING if a->string_val
 * was not obtained by a previous call to gp_alloc(), or has already been freed.
 * Thus 'a->type' is set to INTGR afterwards to make subsequent calls safe.
 */
struct value *
gpfree_string(struct value *a)
{
#ifdef GP_STRING_VARS
    if (a->type == STRING) {
	free(a->v.string_val);
	/* I would have set it to INVALID if such a type existed */
	a->type = INTGR;
    }
#endif
    return a;
}

/* some machines have trouble with exp(-x) for large x
 * if E_MINEXP is defined at compile time, use gp_exp(x) instead,
 * which returns 0 for exp(x) with x < E_MINEXP
 * exp(x) will already have been defined as gp_exp(x) in plot.h
 */

double
gp_exp(double x)
{
#ifdef E_MINEXP
    return (x < (E_MINEXP)) ? 0.0 : exp(x);
#else  /* E_MINEXP */
    int old_errno = errno;
    double result = exp(x);

    /* exp(-large) quite uselessly raises ERANGE --- stop that */
    if (result == 0.0)
	errno = old_errno;
    return result;
#endif /* E_MINEXP */
}

void
reset_stack()
{
    s_p = -1;
}


void
check_stack()
{				/* make sure stack's empty */
    if (s_p != -1)
	fprintf(stderr, "\n\
warning:  internal error--stack not empty!\n\
          (function called with too many parameters?)\n");
}

TBOOLEAN
more_on_stack()
{
    return (s_p >= 0);
}

struct value *
pop(struct value *x)
{
    if (s_p < 0)
	int_error(NO_CARET, "stack underflow (function call with missing parameters?)");
    *x = stack[s_p--];
    return (x);
}

#if (GP_STRING_VARS > 1)
/*
 * Allow autoconversion of string variables to floats if they
 * are dereferenced in a numeric context.
 */
struct value *
pop_or_convert_from_string(struct value *v)
{
    (void) pop(v);
    if (v->type == STRING) {
	double d = atof(v->v.string_val);
	gpfree_string(v);
	Gcomplex(v, d, 0.);
	FPRINTF((stderr,"converted string to CMPLX value %g\n",real(v)));
    }
    return(v);
}
#endif

void
push(struct value *x)
{
    if (s_p == STACK_DEPTH - 1)
	int_error(NO_CARET, "stack overflow");
    stack[++s_p] = *x;
#ifdef GP_STRING_VARS
    /* WARNING - This is a memory leak if the string is not later freed */
    if (x->type == STRING && x->v.string_val)
	stack[s_p].v.string_val = gp_strdup(x->v.string_val);
#endif
}


void
int_check(struct value *v)
{
    if (v->type != INTGR)
	int_error(NO_CARET, "non-integer passed to boolean operator");
}



/* Internal operators of the stack-machine, not directly represented
 * by any user-visible operator, or using private status variables
 * directly */

/* converts top-of-stack to boolean */
void
f_bool(union argument *x)
{
    (void) x;			/* avoid -Wunused warning */

    int_check(&top_of_stack);
    top_of_stack.v.int_val = !!top_of_stack.v.int_val;
}


void
f_jump(union argument *x)
{
    (void) x;			/* avoid -Wunused warning */
    jump_offset = x->j_arg;
}


void
f_jumpz(union argument *x)
{
    struct value a;

    (void) x;			/* avoid -Wunused warning */
    int_check(&top_of_stack);
    if (top_of_stack.v.int_val) {	/* non-zero --> no jump*/
	(void) pop(&a);
    } else
	jump_offset = x->j_arg;	/* leave the argument on TOS */
}


void
f_jumpnz(union argument *x)
{
    struct value a;

    (void) x;			/* avoid -Wunused warning */
    int_check(&top_of_stack);
    if (top_of_stack.v.int_val)	/* non-zero */
	jump_offset = x->j_arg;	/* leave the argument on TOS */
    else {
	(void) pop(&a);
    }
}

void
f_jtern(union argument *x)
{
    struct value a;

    (void) x;			/* avoid -Wunused warning */
    int_check(pop(&a));
    if (! a.v.int_val)
	jump_offset = x->j_arg;	/* go jump to FALSE code */
}

/* This is the heart of the expression evaluation module: the stack
   program execution loop.

  'ft' is a table containing C functions within this program.

   An 'action_table' contains pointers to these functions and
   arguments to be passed to them.

   at_ptr is a pointer to the action table which must be executed
   (evaluated).

   so the iterated line exectues the function indexed by the at_ptr
   and passes the address of the argument which is pointed to by the
   arg_ptr

*/

void
execute_at(struct at_type *at_ptr)
{
    int instruction_index, operator, count;
    int saved_jump_offset = jump_offset;

    count = at_ptr->a_count;
    for (instruction_index = 0; instruction_index < count;) {
	operator = (int) at_ptr->actions[instruction_index].index;
	jump_offset = 1;	/* jump operators can modify this */
	(*ft[operator].func) (&(at_ptr->actions[instruction_index].arg));
	assert(is_jump(operator) || (jump_offset == 1));
	instruction_index += jump_offset;
    }

    jump_offset = saved_jump_offset;
}

/* 20010724: moved here from parse.c, where it didn't belong */
void
evaluate_at(struct at_type *at_ptr, struct value *val_ptr)
{
    double temp = 0;

    undefined = FALSE;
    errno = 0;
    reset_stack();

#ifndef DOSX286
    if (SETJMP(fpe_env, 1))
	return;			/* just bail out */
    (void) signal(SIGFPE, (sigfunc) fpe);
#endif

    execute_at(at_ptr);

#ifndef DOSX286
    (void) signal(SIGFPE, SIG_DFL);
#endif

    if (errno == EDOM || errno == ERANGE) {
	undefined = TRUE;
    } else if (!undefined) {	/* undefined (but not errno) may have been set by matherr */
	(void) pop(val_ptr);
	check_stack();
	/* At least one machine (ATT 3b1) computes Inf without a SIGFPE */
#if (GP_STRING_VARS > 1)
	if (val_ptr->type != STRING)
#endif
	temp = real(val_ptr);
	if (temp > VERYLARGE || temp < -VERYLARGE) {
	    undefined = TRUE;
	}
    }
#if defined(NeXT) || defined(ultrix)
    /*
     * linux was able to fit curves which NeXT gave up on -- traced it to
     * silently returning NaN for the undefined cases and plowing ahead
     * I can force that behavior this way.  (0.0/0.0 generates NaN)
     */
    if (undefined && (errno == EDOM || errno == ERANGE)) {	/* corey@cac */
	undefined = FALSE;
	errno = 0;
	Gcomplex(val_ptr, 0.0 / 0.0, 0.0 / 0.0);
    }
#endif /* NeXT || ultrix */

}

void
free_at(struct at_type *at_ptr)
{
#ifdef GP_STRING_VARS
    int i;
    /* All string constants belonging to this action table have to be
     * freed before destruction. */
    if (!at_ptr)
        return;
    for(i=0; i<at_ptr->a_count; i++) {
	struct at_entry *a = &(at_ptr->actions[i]);
	/* if union a->arg is used as a->arg.v_arg free potential string */
	if ( a->index == PUSHC || a->index == DOLLARS )
	    gpfree_string(&(a->arg.v_arg));
    }
#endif
    free(at_ptr);
}

/* EAM July 2003 - Return pointer to udv with this name; if the key does not
 * match any existing udv names, create a new one and return a pointer to it.
 */
struct udvt_entry *
add_udv_by_name(char *key)
{
    struct udvt_entry **udv_ptr = &first_udv;

    /* check if it's already in the table... */

    while (*udv_ptr) {
	if (!strcmp(key, (*udv_ptr)->udv_name))
	    return (*udv_ptr);
	udv_ptr = &((*udv_ptr)->next_udv);
    }

    *udv_ptr = (struct udvt_entry *)
	gp_alloc(sizeof(struct udvt_entry), "value");
    (*udv_ptr)->next_udv = NULL;
    (*udv_ptr)->udv_name = gp_strdup(key);
    (*udv_ptr)->udv_undef = TRUE;
    return (*udv_ptr);
}
