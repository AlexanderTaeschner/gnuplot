#ifndef lint
static char *RCSid() { return RCSid("$Id: command.c,v 1.102 2004/08/09 18:07:18 sfeam Exp $"); }
#endif

/* GNUPLOT - command.c */

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
 * Changes:
 *
 * Feb 5, 1992  Jack Veenstra   (veenstra@cs.rochester.edu) Added support to
 * filter data values read from a file through a user-defined function before
 * plotting. The keyword "thru" was added to the "plot" command. Example
 * syntax: f(x) = x / 100 plot "test.data" thru f(x) This example divides all
 * the y values by 100 before plotting. The filter function processes the
 * data before any log-scaling occurs. This capability should be generalized
 * to filter x values as well and a similar feature should be added to the
 * "splot" command.
 *
 * 19 September 1992  Lawrence Crowl  (crowl@cs.orst.edu)
 * Added user-specified bases for log scaling.
 *
 * April 1999 Franz Bakan (bakan@ukezyk.desy.de)
 * Added code to support mouse-input from OS/2 PM window
 * Changes marked by USE_MOUSE
 *
 * May 1999, update by Petr Mikulik
 * Use gnuplot's pid in shared mem name
 *
 * August 1999 Franz Bakan and Petr Mikulik
 * Encapsulating read_line into a thread, acting on input when thread or
 * gnupmdrv posts an event semaphore. Thus mousing works even when gnuplot
 * is used as a plotting device (commands passed via pipe).
 *
 */

#include "command.h"

#include "alloc.h"
#include "eval.h"
#include "fit.h"
#include "binary.h"
#include "datafile.h"
#include "getcolor.h"
#include "gp_hist.h"
#include "gp_time.h"
#include "misc.h"
#include "parse.h"
#include "plot.h"
#include "plot2d.h"
#include "plot3d.h"
#include "readline.h"
#include "save.h"
#include "scanner.h"
#include "setshow.h"
#include "tables.h"
#include "term_api.h"
#include "util.h"

#ifdef USE_MOUSE
# include "mouse.h"
#endif

#define PROMPT "gnuplot> "

#if (defined(MSDOS) || defined(DOS386)) && defined(__TURBOC__) && !defined(_Windows)
unsigned _stklen = 16394;        /* increase stack size */
#endif /* MSDOS && TURBOC */

#ifdef OS2_IPC
#  define INCL_DOSMEMMGR
#  define INCL_DOSPROCESS
#  define INCL_DOSSEMAPHORES
#  include <os2.h>
PVOID input_from_PM_Terminal = NULL;
char mouseSharedMemName[40] = "";
HEV semInputReady = 0;      /* semaphore to be created in plot.c */
int thread_rl_Running = 0;  /* running status */
int thread_rl_RetCode = -1; /* return code from readline in a thread */
#endif /* OS2_IPC */


#ifndef _Windows
# include "help.h"
#else
static int winsystem __PROTO((const char *));
#endif /* _Windows */

#ifdef _Windows
# include <windows.h>
# ifdef __MSC__
#  include <malloc.h>
# else
#  include <alloc.h>
#  include <dir.h>		/* setdisk() */
# endif				/* !MSC */
# include "win/winmain.h"
#endif /* _Windows */

#ifdef VMS
int vms_vkid;			/* Virtual keyboard id */
int vms_ktid;			/* key table id, for translating keystrokes */
#endif /* VMS */


/* static prototypes */
static void command __PROTO((void));
static int changedir __PROTO((char *path));
static char* fgets_ipc __PROTO((char* dest, int len));
static int read_line __PROTO((const char *prompt));
static void do_system __PROTO((const char *));
static void test_palette_subcommand __PROTO((void));
static void test_time_subcommand __PROTO((void));

#ifdef AMIGA_AC_5
static void getparms __PROTO((char *, char **));
#endif

struct lexical_unit *token;
int token_table_size;


char *input_line;
size_t input_line_len;
int inline_num;			/* input line number */

struct udft_entry *dummy_func;

/* support for replot command */
char *replot_line;
int plot_token;			/* start of 'plot' command */

/* flag to disable `replot` when some data are sent through stdin;
 * used by mouse/hotkey capable terminals */
TBOOLEAN replot_disabled = FALSE;

#ifdef USE_MOUSE
TBOOLEAN paused_for_mouse = FALSE;
TBOOLEAN paused_for_mousekeys = FALSE;
#endif

/* If last plot was a 3d one. */
TBOOLEAN is_3d_plot = FALSE;

/* output file for the print command */
FILE *print_out = NULL;
char *print_out_name = NULL;

/* input data, parsing variables */
#ifdef AMIGA_SC_6_1
__far int num_tokens, c_token;
#else
int num_tokens, c_token;
#endif

static int if_depth = 0;
static TBOOLEAN if_condition = FALSE;

static int command_exit_status = 0;

/* support for dynamic size of input line */
void
extend_input_line()
{
    if (input_line_len == 0) {
	/* first time */
	input_line = gp_alloc(MAX_LINE_LEN, "input_line");
	input_line_len = MAX_LINE_LEN;
	input_line[0] = NUL;

#ifdef OS2_IPC
	sprintf( mouseSharedMemName, "\\SHAREMEM\\GP%i_Mouse_Input", getpid() );
	if (DosAllocSharedMem((PVOID) & input_from_PM_Terminal,
	    	mouseSharedMemName,
			      MAX_LINE_LEN,
			      PAG_WRITE | PAG_COMMIT))
	    fputs("command.c: DosAllocSharedMem ERROR\n",stderr);
#endif /* OS2_IPC */

    } else {
	input_line = gp_realloc(input_line, input_line_len + MAX_LINE_LEN,
				"extend input line");
	input_line_len += MAX_LINE_LEN;
	FPRINTF((stderr, "extending input line to %d chars\n",
		 input_line_len));
    }
}

/* constant by which token table grows */
#define MAX_TOKENS 400

void
extend_token_table()
{
    if (token_table_size == 0) {
	/* first time */
	token = (struct lexical_unit *) gp_alloc(MAX_TOKENS * sizeof(struct lexical_unit), "token table");
	token_table_size = MAX_TOKENS;
	/* HBB: for checker-runs: */
	memset(token, 0, MAX_TOKENS * sizeof(*token));
    } else {
	token = gp_realloc(token, (token_table_size + MAX_TOKENS) * sizeof(struct lexical_unit), "extend token table");
	memset(token+token_table_size, 0, MAX_TOKENS * sizeof(*token));
	token_table_size += MAX_TOKENS;
	FPRINTF((stderr, "extending token table to %d elements\n", token_table_size));
    }
}


#ifdef OS2_IPC
void thread_read_line()
{
   thread_rl_Running = 1;
   thread_rl_RetCode = ( read_line(PROMPT) );
   thread_rl_Running = 0;
   DosPostEventSem(semInputReady);
}
#endif /* OS2_IPC */


int
com_line()
{
#ifdef OS2_IPC
static char *input_line_SharedMem = NULL;

    if (input_line_SharedMem == NULL) {  /* get shared mem only once */
    if (DosGetNamedSharedMem((PVOID) &input_line_SharedMem,
		mouseSharedMemName, PAG_WRITE | PAG_READ))
	fputs("readline.c: DosGetNamedSharedMem ERROR\n", stderr);
    else
	*input_line_SharedMem = 0;
    }
#endif /* OS2_IPC */

    if (multiplot) {
	/* calls int_error() if it is not happy */
	term_check_multiplot_okay(interactive);

	if (read_line("multiplot> "))
	    return (1);
    } else {

#ifndef USE_MOUSE
	if (read_line(PROMPT))
	    return (1);
#else
# ifdef OS2_IPC
	ULONG u;
        if (thread_rl_Running == 0) {
	    int res = _beginthread(thread_read_line,NULL,32768,NULL);
	    if (res == -1)
		fputs("error command.c could not begin thread\n",stderr);
	}
	/* wait until a line is read or gnupmdrv makes shared mem available */
	DosWaitEventSem(semInputReady,SEM_INDEFINITE_WAIT);
	DosResetEventSem(semInputReady,&u);
	if (thread_rl_Running) {
	    if (input_line_SharedMem == NULL || !*input_line_SharedMem)
		return (0);
	    if (*input_line_SharedMem=='%') {
		do_event( (struct gp_event_t*)(input_line_SharedMem+1) ); /* pass terminal's event */
		input_line_SharedMem[0] = 0; /* discard the whole command line */
		thread_rl_RetCode = 0;
		return (0);
	    }
	    if (*input_line_SharedMem &&
	        strstr(input_line_SharedMem,"plot") != NULL &&
		(strcmp(term->name,"pm") && strcmp(term->name,"x11"))) {
		/* avoid plotting if terminal is not PM or X11 */
		fprintf(stderr,"\n\tCommand(s) ignored for other than PM and X11 terminals\a\n");
		if (interactive) fputs(PROMPT,stderr);
		input_line_SharedMem[0] = 0; /* discard the whole command line */
		return (0);
	    }
#  if 0
	    fprintf(stderr,"shared mem received: |%s|\n",input_line_SharedMem);
	    if (*input_line_SharedMem && input_line_SharedMem[strlen(input_line_SharedMem)-1] != '\n') fprintf(stderr,"\n");
#  endif
	    strcpy(input_line, input_line_SharedMem);
	    input_line_SharedMem[0] = 0;
	    thread_rl_RetCode = 0;
	}
	if (thread_rl_RetCode)
	    return (1);
# else /* OS2_IPC */
	if (read_line(PROMPT))
	    return (1);
# endif /* OS2_IPC */
#endif /* USE_MOUSE */
    }

    /* So we can flag any new output: if false at time of error,
     * we reprint the command line before printing caret.
     * TRUE for interactive terminals, since the command line is typed.
     * FALSE for non-terminal stdin, so command line is printed anyway.
     * (DFK 11/89)
     */
    screen_ok = interactive;

    if (do_line())
	return (1);
    else
	return (0);
}


int
do_line()
{
    /* Line continuation has already been handled
     * by read_line() */
    char *inlptr = input_line;

    /* Skip leading whitespace */
    while (isspace((unsigned char) *inlptr))
	inlptr++;

    if (inlptr != input_line) {
	/* If there was leading whitespace, copy the actual
	 * command string to the front. use memmove() because
	 * source and target may overlap */
	memmove(input_line, inlptr, strlen(inlptr));
	/* Terminate resulting string */
	input_line[strlen(inlptr)] = NUL;
    }
    FPRINTF((stderr, "Input line: \"%s\"\n", input_line));

    /* also used in load_file */
    if (is_system(input_line[0])) {
	do_system(input_line + 1);
	if (interactive)	/* 3.5 did it unconditionally */
	    (void) fputs("!\n", stderr);	/* why do we need this ? */
	return (0);
    }

    if_depth = 0;
    if_condition = TRUE;
    num_tokens = scanner(&input_line, &input_line_len);
    c_token = 0;
    while (c_token < num_tokens) {
	command();
	if (command_exit_status) {
	    command_exit_status = 0;
	    return 1;
	}
	if (c_token < num_tokens) {	/* something after command */
	    if (equals(c_token, ";"))
		c_token++;
	    else
		int_error(c_token, "';' expected");
	}
    }
    return (0);
}


void
do_string(char *s)
{
    char *orig_input_line = gp_strdup(input_line);

    while (input_line_len < strlen(s) + 1)
	extend_input_line();
    strcpy(input_line, s);

#ifdef USE_MOUSE
    if (display_ipc_commands())
	fprintf(stderr, "%s\n", s);
#endif

    do_line();

    strcpy(input_line, orig_input_line);
    free(orig_input_line);
}


#ifdef USE_MOUSE
void
toggle_display_of_ipc_commands()
{
    if (mouse_setting.verbose)
	mouse_setting.verbose = 0;
    else
	mouse_setting.verbose = 1;
}

int
display_ipc_commands()
{
    return mouse_setting.verbose;
}

void
do_string_replot(char *s)
{
    char *orig_input_line = gp_strdup(input_line);

    while (input_line_len < strlen(s) + 1)
	extend_input_line();
    strcpy(input_line, s);
    if (display_ipc_commands())
	fprintf(stderr, "%s\n", s);

    do_line();
    if (!replot_disabled)
	replotrequest();

    strcpy(input_line, orig_input_line);
    free(orig_input_line);
}

void
restore_prompt()
{
    if (interactive) {
#if defined(HAVE_LIBREADLINE)
	rl_forced_update_display();
#else
	fputs(PROMPT, stderr);
	fflush(stderr);
#endif
    }
}
#endif /* USE_MOUSE */


void
define()
{
    int start_token;	/* the 1st token in the function definition */
    struct udvt_entry *udv;
    struct udft_entry *udf;

    if (equals(c_token + 1, "(")) {
	/* function ! */
	int dummy_num = 0;
	struct at_type *at_tmp;
	char save_dummy[MAX_NUM_VAR][MAX_ID_LEN+1];
	memcpy(save_dummy, c_dummy_var, sizeof(save_dummy));
	start_token = c_token;
	do {
	    c_token += 2;	/* skip to the next dummy */
	    copy_str(c_dummy_var[dummy_num++], c_token, MAX_ID_LEN);
	} while (equals(c_token + 1, ",") && (dummy_num < MAX_NUM_VAR));
	if (equals(c_token + 1, ","))
	    int_error(c_token + 2, "function contains too many parameters");
	c_token += 3;		/* skip (, dummy, ) and = */
	if (END_OF_COMMAND)
	    int_error(c_token, "function definition expected");
	udf = dummy_func = add_udf(start_token);
	if ((at_tmp = perm_at()) == (struct at_type *) NULL)
	    int_error(start_token, "not enough memory for function");
	if (udf->at)		/* already a dynamic a.t. there */
	    free((char *) udf->at);	/* so free it first */
	udf->at = at_tmp;	/* before re-assigning it. */
	memcpy(c_dummy_var, save_dummy, sizeof(save_dummy));
	m_capture(&(udf->definition), start_token, c_token - 1);
	dummy_func = NULL;	/* dont let anyone else use our workspace */
    } else {
	/* variable ! */
	start_token = c_token;
	c_token += 2;
	udv = add_udv(start_token);
	(void) const_express(&(udv->udv_value));
	udv->udv_undef = FALSE;
    }
}


static void
command()
{
    int i;

    for (i = 0; i < MAX_NUM_VAR; i++)
	c_dummy_var[i][0] = NUL;	/* no dummy variables */

    if (is_definition(c_token))
	define();
    else
	(*lookup_ftable(&command_ftbl[0],c_token))();

    return;
}


#ifdef USE_MOUSE

#define WHITE_AFTER_TOKEN(x) \
(' ' == input_line[token[x].start_index + token[x].length] \
|| '\t' == input_line[token[x].start_index + token[x].length] \
|| '\0' == input_line[token[x].start_index + token[x].length])

/* process the 'bind' command */
void
bind_command()
{
    char* lhs = (char*) 0;
    char* rhs = (char*) 0;
    ++c_token;

    if (!END_OF_COMMAND && equals(c_token,"!")) {
	bind_remove_all();
	++c_token;
	return;
    }

    /* get left hand side: the key or key sequence */
    if (!END_OF_COMMAND) {
	char* first = input_line + token[c_token].start_index;
	int size = (int) (strchr(first, ' ') - first);
	if (size < 0) {
	    size = (int) (strchr(first, '\0') - first);
	}
	if (size < 0) {
	    fprintf(stderr, "(bind_command) %s:%d\n", __FILE__, __LINE__);
	    return;
	}
	lhs = (char*) gp_alloc(size + 1, "bind_command->lhs");
	if (isstring(c_token)) {
	    quote_str(lhs, c_token, token_len(c_token));
	} else {
	    char* ptr = lhs;
	    while (!END_OF_COMMAND) {
		copy_str(ptr, c_token, token_len(c_token) + 1);
		ptr += token_len(c_token);
		if (WHITE_AFTER_TOKEN(c_token)) {
		    break;
		}
		++c_token;
	    }
	}
	++c_token;
    }

    /* get right hand side: the command. allocating the size
     * of input_line is too big, but shouldn't hurt too much. */
    if (!END_OF_COMMAND) {
	rhs = (char*) gp_alloc(strlen(input_line) + 1, "bind_command->rhs");
	if (isstring(c_token)) {
	    /* bind <lhs> "..." */
	    quote_str(rhs, c_token, token_len(c_token));
	    c_token++;
	} else {
	    char* ptr = rhs;
	    while (!END_OF_COMMAND) {
		/* bind <lhs> ... ... ... */
		copy_str(ptr, c_token, token_len(c_token) + 1);
		ptr += token_len(c_token);
		if (WHITE_AFTER_TOKEN(c_token)) {
		    *ptr++ = ' ';
		    *ptr = '\0';
		}
		c_token++;
	    }
	}
    }

    FPRINTF((stderr, "(bind_command) |%s| |%s|\n", lhs, rhs));

#if 0
    if (!END_OF_COMMAND) {
	int_error(c_token, "usage: bind \"<lhs>\" \"<rhs>\"");
    }
#endif

    /* bind_process() will eventually free lhs / rhs ! */
    bind_process(lhs, rhs);
}
#endif /* USE_MOUSE */


/*
 * Command parser functions
 */

/* process the 'call' command */
void
call_command()
{
    char *save_file = NULL;

    c_token++;
    save_file = try_to_get_string();
    
    if (!save_file)
	int_error(c_token, "expecting filename");

    gp_expand_tilde(&save_file);
    /* Argument list follows filename */
    load_file(loadpath_fopen(save_file, "r"), save_file, TRUE);
    /* input_line[] and token[] now destroyed! */
    c_token = 0;
    num_tokens = 0;
    free(save_file);
}


/* process the 'cd' command */
void
changedir_command()
{
    char *save_file = NULL;

    c_token++;
    save_file = try_to_get_string();
    if (!save_file)
	int_error(c_token, "expecting directory name");

    gp_expand_tilde(&save_file);
    if (changedir(save_file))
	int_error(c_token, "Can't change to this directory");
    free(save_file);
}


/* process the 'clear' command */
void
clear_command()
{

    term_start_plot();

    if (multiplot && term->fillbox) {
	unsigned int xx1 = (unsigned int) (xoffset * term->xmax);
	unsigned int yy1 = (unsigned int) (yoffset * term->ymax);
	unsigned int width = (unsigned int) (xsize * term->xmax);
	unsigned int height = (unsigned int) (ysize * term->ymax);
	(*term->fillbox) (0, xx1, yy1, width, height);
    }
    term_end_plot();

    screen_ok = FALSE;
    c_token++;

}


/* process the 'exit' and 'quit' commands */
void
exit_command()
{
    /* graphics will be tidied up in main */
    command_exit_status = 1;
}


/* fit_command() is in fit.c */


/* help_command() is below */


/* process the 'history' command
 * FIXME: chock full of memory leaks
 */
void
history_command()
{
#if defined(READLINE) && !defined(HAVE_LIBREADLINE)
    struct value a;
    char *name = NULL; /* name of the output file; NULL for stdout */
    int n = 0;         /* print only <last> entries */
    TBOOLEAN append = FALSE;    /* rewrite output file or append it */

    c_token++;

    if (!END_OF_COMMAND && equals(c_token,"?")) {
	/* find and show the entries */
	c_token++;
	m_capture(&name, c_token, c_token);
	printf ("history ?%s\n", name);
	if (!history_find_all(name))
	    int_error(c_token,"not in history");
	c_token++;
    } else if (!END_OF_COMMAND && equals(c_token,"!")) {
	/* execute the entry */
	static char flag = 0;
	static char *save_input_line;
	static int save_c_token, save_input_line_len;

	if (flag) {
	    flag = 0;
	    input_line = save_input_line;
	    c_token = save_c_token;
	    input_line_len = save_input_line_len;
	    num_tokens = scanner(&input_line, &input_line_len);
	    int_error(c_token,"recurrency forbidden");
	}
	c_token++;
	m_capture(&name, c_token, c_token);
	name = history_find(name);
	if (name == NULL)
	    int_error(c_token,"not in history");
	else {
	    /* execute the command "name" */
	    char *copy_name = gp_strdup(name);
	    save_input_line = input_line;
	    save_c_token = c_token;
	    save_input_line_len = input_line_len;
	    flag = 1;
	    input_line = copy_name;
	    printf("  Executing:\n\t%s\n",name);
	    do_line();
	    free(copy_name);
	    input_line = save_input_line;
	    c_token = save_c_token;
	    input_line_len = save_input_line_len;
	    num_tokens = scanner(&input_line, &input_line_len);
	    flag = 0;
	}
	c_token++;
    } else {
	TBOOLEAN quiet = FALSE;
	if (!END_OF_COMMAND && almost_equals(c_token,"q$uiet")) {
	    /* option quiet to suppress history entry numbers */
	    quiet = TRUE;
	    c_token++;
	}
	/* show history entries */
	if (!END_OF_COMMAND && isanumber(c_token)) {
	    n = (int)real(const_express(&a));
	}
	if ((name = try_to_get_string())) {
	    if (!END_OF_COMMAND && almost_equals(c_token, "ap$pend")) {
		append = TRUE;
		c_token++;
	    }
	}
	write_history_n(n, (quiet ? "" : name), (append ? "a" : "w"));
	free(name);
    }
#else
    c_token++;
    int_warn(NO_CARET, "Command 'history' is supported by gnuplot's readline, but not by GNU readline (you are welcome to contribute).");
#endif /* READLINE && !HAVE_LIBREADLINE */
}

#define REPLACE_ELSE(tok)             \
do {                                  \
    int idx = token[tok].start_index; \
    token[tok].length = 1;            \
    input_line[idx++] = ';'; /* e */  \
    input_line[idx++] = ' '; /* l */  \
    input_line[idx++] = ' '; /* s */  \
    input_line[idx++] = ' '; /* e */  \
} while (0)

#if 0
#define PRINT_TOKEN(tok)                                                    \
do {                                                                        \
    int i;                                                                  \
    int end_index = token[tok].start_index + token[tok].length;             \
    for (i = token[tok].start_index; i < end_index && input_line[i]; i++) { \
	fputc(input_line[i], stderr);                                       \
    }                                                                       \
    fputc('\n', stderr);                                                    \
    fflush(stderr);                                                         \
} while (0)
#endif

/* process the 'if' command */
void
if_command()
{
    double exprval;
    struct value t;

    if_depth++;

    if (!equals(++c_token, "("))	/* no expression */
	int_error(c_token, "expecting (expression)");
    exprval = real(const_express(&t));
    if (exprval != 0.0) {
	/* fake the condition of a ';' between commands */
	int eolpos = token[num_tokens - 1].start_index + token[num_tokens - 1].length;
	--c_token;
	token[c_token].length = 1;
	token[c_token].start_index = eolpos + 2;
	input_line[eolpos + 2] = ';';
	input_line[eolpos + 3] = NUL;

	if_condition = TRUE;
    } else {
	while (c_token < num_tokens) {
	    /* skip over until the next command */
	    while (!END_OF_COMMAND) {
		++c_token;
	    }
	    if (++c_token < num_tokens && (equals(c_token, "else"))) {
		/* break if an "else" was found */
		if_condition = FALSE;
		--c_token; /* go back to ';' */
		return;
	    }
	}
	/* no else found */
	c_token = num_tokens = 0;
    }
}

/* process the 'else' command */
void
else_command()
{
    if (if_depth <= 0) {
	int_error(c_token, "else without if");
	return;
    } else {
	if_depth--;
    }

    if (TRUE == if_condition) {
	/* First part of line was true so
	 * discard the rest of the line. */
	c_token = num_tokens = 0;
    } else {
	REPLACE_ELSE(c_token);
	if_condition = TRUE;
    }
}


/* process the 'load' command */
void
load_command()
{
    FILE *fp;
    char *save_file;

    c_token++;
    save_file = try_to_get_string();
    if (!save_file)
	int_error(c_token, "expecting filename");

    gp_expand_tilde(&save_file);
    fp = strcmp(save_file, "-") ? loadpath_fopen(save_file, "r") : stdout;
    load_file(fp, save_file, FALSE);
    /* input_line[] and token[] now destroyed! */
    c_token = num_tokens = 0;
    free(save_file);
}



/* null command */
void
null_command()
{
    return;
}


/* process the 'pause' command */
void
pause_command()
{
    struct value a;
    int text = 0;
    double sleep_time;
    char *buf = gp_alloc(MAX_LINE_LEN+1, "pause argument");

    c_token++;

    *buf = NUL;

#ifdef USE_MOUSE
    paused_for_mouse = FALSE;
    paused_for_mousekeys = FALSE;
    if (equals(c_token,"mouse")) {
	sleep_time = -1;
	c_token++;

/*	EAM FIXME - This is not the correct test; what we really want */
/*	to know is whether or not the terminal supports mouse feedback */
/*	if (term_initialised) { */
	if (mouse_setting.on && term) {
	    struct udvt_entry *current;
	    paused_for_mouse = TRUE;
	    if (!(END_OF_COMMAND)) {
		if (almost_equals(c_token,"key$press")) {
		paused_for_mousekeys = TRUE;
		c_token++;
		}
	    }
	    /* Set the pause mouse return codes to -1 */
	    current = get_udv("MOUSE_KEY");
	    current->udv_undef = FALSE;
	    Ginteger(&current->udv_value,-1);
	    current = get_udv("MOUSE_BUTTON");
	    current->udv_undef = FALSE;
	    Ginteger(&current->udv_value,-1);
	} else
	    int_warn(NO_CARET,"Mousing not active");
    } else
#endif
	sleep_time = real(const_express(&a));

    if (!(END_OF_COMMAND)) {
	if (!isstring(c_token))
	    int_error(c_token, "expecting string");
	else {
	    quote_str(buf, c_token, MAX_LINE_LEN);
	    ++c_token;
#ifdef _Windows
	    if (sleep_time >= 0)
#elif defined(OS2)
		if (strcmp(term->name, "pm") != 0 || sleep_time >= 0)
#elif defined(MTOS)
		    if (strcmp(term->name, "mtos") != 0 || sleep_time >= 0)
#endif /* _Windows */
			fputs(buf, stderr);
	    text = 1;
	}
    }
    if (sleep_time < 0) {
#ifdef _Windows
	if (!Pause(buf)) {
	    free(buf);
	    bail_to_command_line();
	}
#elif defined(OS2)
	if (strcmp(term->name, "pm") == 0 && sleep_time < 0) {
	    int rc;
	    if ((rc = PM_pause(buf)) == 0) {
		/* if (!CallFromRexx)
		 * would help to stop REXX programs w/o raising an error message
		 * in RexxInterface() ...
		 */
		free(buf);
		bail_to_command_line();
	    } else if (rc == 2) {
		fputs(buf, stderr);
		text = 1;
		(void) fgets(buf, strlen(buf), stdin);
	    }
	}
#elif defined(_Macintosh)
	if (strcmp(term->name, "macintosh") == 0 && sleep_time < 0)
	    Pause( (int)sleep_time );
#elif defined(MTOS)
	if (strcmp(term->name, "mtos") == 0) {
	    int MTOS_pause(char *buf);
	    int rc;
	    if ((rc = MTOS_pause(buf)) == 0)
		free(buf);
	    bail_to_command_line();
	    else if (rc == 2) {
		fputs(buf, stderr);
		text = 1;
		(void) fgets(buf, strlen(buf), stdin);
	    }
	} else if (strcmp(term->name, "atari") == 0) {
	    char *line = readline("");
	    if (line)
		free(line);
	} else
	    (void) fgets(buf, strlen(buf), stdin);
#elif defined(ATARI)
	if (strcmp(term->name, "atari") == 0) {
	    char *line = readline("");
	    if (line)
		free(line);
	} else
	    (void) fgets(buf, strlen(buf), stdin);
#else /* !(_Windows || OS2 || _Macintosh || MTOS || ATARI) */
#ifdef USE_MOUSE
	if (term && term->waitforinput) {
	    /* term->waitforinput() will return,
	     * if CR was hit */
	    term->waitforinput();
	} else {
#endif /* USE_MOUSE */
	(void) fgets(buf, sizeof(buf), stdin);
	/* Hold until CR hit. */
#ifdef USE_MOUSE
	}
#endif /* USE_MOUSE */
#endif /* !(_Windows || OS2 || _Macintosh || MTOS || ATARI) */
    }
    if (sleep_time > 0)
	GP_SLEEP(sleep_time);

    if (text != 0 && sleep_time >= 0)
	fputc('\n', stderr);
    screen_ok = FALSE;

    free(buf);

}


/* process the 'plot' command */
void
plot_command()
{
    plot_token = c_token++;
    plotted_data_from_stdin = FALSE;
    SET_CURSOR_WAIT;
#ifdef USE_MOUSE
    plot_mode(MODE_PLOT);
    get_udv("MOUSE_X")->udv_undef = TRUE;
    get_udv("MOUSE_Y")->udv_undef = TRUE;
    get_udv("MOUSE_X2")->udv_undef = TRUE;
    get_udv("MOUSE_Y2")->udv_undef = TRUE;
    get_udv("MOUSE_BUTTON")->udv_undef = TRUE;
    get_udv("MOUSE_SHIFT")->udv_undef = TRUE;
    get_udv("MOUSE_ALT")->udv_undef = TRUE;
    get_udv("MOUSE_CTRL")->udv_undef = TRUE;
#endif
    plotrequest();
    SET_CURSOR_ARROW;
}


void
print_set_output(char *name, TBOOLEAN append_p)
{
    if (print_out && print_out != stderr && print_out != stdout) {
#ifdef PIPES
	if (print_out_name[0] == '|') {
	    if (0 > pclose(print_out))
		perror(print_out_name);
	} else
#endif
	    if (0 > fclose(print_out))
		perror(print_out_name);
    }

    if (print_out_name)
	free(print_out_name);

    print_out_name = NULL;

    if (! name) {
	print_out = stderr;
	return;
    }

    if (! strcmp(name, "-")) {
	print_out = stdout;
	return;
    }

#ifdef PIPES
    if (name[0]=='|') {
	print_out = popen(name + 1, "w");
	if (!print_out)
	    perror(name);
	else
	    print_out_name = name;
	return;
    }
#endif

    print_out = fopen(name, append_p ? "a" : "w");
    if (!print_out) {
	perror(name);
	return;
    }

    print_out_name = name;
}

char *
print_show_output()
{
    if (!print_out_name)
	return "<STDERR>";
    if (!strcmp(print_out_name, "-"))
	return "<STDOUT>";
    return print_out_name;
}

/* process the 'print' command */
void
print_command()
{
    struct value a;
    char *s;
    /* space printed between two expressions only */
    int need_space = 0;

    if (!print_out) {
        print_out = stderr;
    }
    screen_ok = FALSE;
    do {
	++c_token;
	s = try_to_get_string();
	if (s) {
	    fputs(s, print_out);
	    need_space = 0;
	    free(s);
	} else {
	    (void) const_express(&a);
	    if (need_space)
		putc(' ', print_out);
	    need_space = 1;
	    disp_value(print_out, &a);
	}
    } while (!END_OF_COMMAND && equals(c_token, ","));

    (void) putc('\n', print_out);
    fflush(print_out);
}


/* process the 'pwd' command */
void
pwd_command()
{
    char *save_file = NULL;

    save_file = (char *) gp_alloc(PATH_MAX, "print current dir");
    if (save_file) {
	GP_GETCWD(save_file, PATH_MAX);
	fprintf(stderr, "%s\n", save_file);
	free(save_file);
    }
    c_token++;
}


/* process the 'replot' command */
void
replot_command()
{
    if (!*replot_line)
	int_error(c_token, "no previous plot");
    /* Disable replot for some reason; currently used by the mouse/hotkey
       capable terminals to avoid replotting when some data come from stdin,
       i.e. when  plotted_data_from_stdin==1  after plot "-".
    */
    if (replot_disabled) {
	replot_disabled = FALSE;
#if 1
	bail_to_command_line(); /* be silent --- don't mess the screen */
#else
	int_error(c_token, "cannot replot data coming from stdin");
#endif
    }
    c_token++;
    SET_CURSOR_WAIT;
    if (term->flags & TERM_INIT_ON_REPLOT)
	term->init();
    replotrequest();
    SET_CURSOR_ARROW;
}


/* process the 'reread' command */
void
reread_command()
{
    FILE *fp = lf_top();

    if (fp != (FILE *) NULL)
	rewind(fp);
    c_token++;
}


/* process the 'save' command */
void
save_command()
{
    FILE *fp;
    char *save_file = NULL;
    int what;

    c_token++;
    what = lookup_table(&save_tbl[0], c_token);

    switch (what) {
	case SAVE_FUNCS:
	case SAVE_SET:
	case SAVE_TERMINAL:
	case SAVE_VARS:
	    c_token++;
	    break;
	default:
	    break;
    }

    save_file = try_to_get_string();
    if (!save_file)
	    int_error(c_token, "expecting filename");
#ifdef PIPES
    if (save_file[0]=='|')
	fp = popen(save_file+1,"w");
    else
#endif
    {
    gp_expand_tilde(&save_file);
    fp = strcmp(save_file,"-") ? loadpath_fopen(save_file,"w") : stdout;
    }
    
    if (!fp)
	os_error(c_token, "Cannot open save file");

    switch (what) {
	case SAVE_FUNCS:
	    save_functions(fp);
	break;
    case SAVE_SET:
	    save_set(fp);
	break;
    case SAVE_TERMINAL:
	    save_term(fp);
	break;
    case SAVE_VARS:
	    save_variables(fp);
	break;
    default:
	    save_all(fp);
    }

    if (stdout != fp) {
#ifdef PIPES
	if (save_file[0] == '|')
	    (void) pclose(fp);
	else
#endif
	    (void) fclose(fp);
    }

    c_token++;

    free(save_file);
}


/* process the 'screendump' command */
void
screendump_command()
{
    c_token++;
#ifdef _Windows
    screen_dump();
#else
    fputs("screendump not implemented\n", stderr);
#endif
}


/* set_command() is in set.c */

/* 'shell' command is processed by do_shell(), see below */

/* show_command() is in show.c */


/* process the 'splot' command */
void
splot_command()
{
    plot_token = c_token++;
    plotted_data_from_stdin = FALSE;
    SET_CURSOR_WAIT;
#ifdef USE_MOUSE
    plot_mode(MODE_SPLOT);
    get_udv("MOUSE_X")->udv_undef = TRUE;
    get_udv("MOUSE_Y")->udv_undef = TRUE;
    get_udv("MOUSE_X2")->udv_undef = TRUE;
    get_udv("MOUSE_Y2")->udv_undef = TRUE;
    get_udv("MOUSE_BUTTON")->udv_undef = TRUE;
#endif
    plot3drequest();
    SET_CURSOR_ARROW;
}


/* process the 'system' command */
void
system_command()
{
    if (!isstring(++c_token))
	int_error(c_token, "expecting command");
    else {
	char *e = input_line + token[c_token].start_index + token[c_token].length - 1;
	char c = *e;
	*e = NUL;
	do_system(input_line + token[c_token].start_index + 1);
	*e = c;
    }
    c_token++;
}


/* process the 'test palette' command
 *
 * note 1: it works on terminals supporting as well as not supporting pm3d
 * note 2: due to the call to load_file(), the rest of the current command
 *	   line after 'test palette ;' is discarded
 */
static void
test_palette_subcommand()
{
#ifndef PM3D
    int_error(c_token, "'test palette' requires pm3d support built-in");
#else
    enum {test_palette_colors = 256};

    double gray, z[test_palette_colors];
    rgb_color rgb1[test_palette_colors];
    int i;
    static const char pre1[] = "\
reset;set multi;\
uns border;uns key;uns xtics;uns ytics;\
se tic out;se cbtic 0,0.1,1; se cbtic nomirr;\
se xr[0:1];se yr[0:1];se zr[0:1];se cbr[0:1];\
se pm3d map;set colorbox hor user orig 0.08,0.07 size 0.79,0.12;";
    static const char pre2[] = "splot 1/0;\n\n\n";
	/* note: those \n's are because of x11 terminal problems with blocking pipe */
    static const char pre3[] = "\
se size 1,0.8;se orig 0,0.2;uns pm3d;\
se key outside;se grid;se tics in;se xtics 0,0.1;se ytics 0,0.1;\
se tit'R,G,B profiles of the current color palette';";
    static const char post[] = "\
\n\n\nuns multi;se orig 0,0;se size 1,1;\n"; /* no final 'reset' in favour of mousing */
    int can_pm3d = (term->make_palette && term->set_color);
    char *order = "rgb";
    char *save_replot_line;
    TBOOLEAN save_is_3d_plot;
    FILE *f = tmpfile();

    c_token++;
    /* parse optional option */
    if (!END_OF_COMMAND) {
	int err = (token[c_token].length != 3);

	order = input_line + token[c_token].start_index;
	if (!err) {
	    err += (memchr(order, 'r', 3) == NULL);
	    err += (memchr(order, 'g', 3) == NULL);
	    err += (memchr(order, 'b', 3) == NULL);
	}
	if (err)
	    int_error(c_token, "combination rgb or gbr or brg etc. expected");
	c_token++;
    }
    if (!f)
	int_error(NO_CARET, "cannot write temporary file");

    /* generate r,g,b curves */
    for (i = 0; i < test_palette_colors; i++) {
	/* colours equidistantly from [0,1] */
	z[i] = (double)i / (test_palette_colors - 1);
	gray = (sm_palette.positive == SMPAL_NEGATIVE) ? 1-z[i] : z[i];
	rgb1_from_gray(gray, &rgb1[i]);
    }

    /* commands to setup the test palette plot */
    enable_reset_palette = 0;
    save_replot_line = gp_strdup(replot_line);
    save_is_3d_plot = is_3d_plot;
    fputs(pre1, f);
    if (can_pm3d)
	fputs(pre2, f);
    fputs(pre3, f);
    /* put inline data of the r,g,b curves */
    fputs("p", f);
    for (i=0; i<strlen(order); i++) {
	if (i > 0)
	    fputs(",", f);
	fputs("'-'tit'", f);
	switch (order[i]) {
	case 'r':
	    fputs("red'w l 1", f);
	    break;
	case 'g':
	    fputs("green'w l 2", f);
	    break;
	case 'b':
	    fputs("blue'w l 3", f);
	    break;
	} /* switch(order[i]) */
    } /* for (i) */
    fputs("\n", f);
    for (i = 0; i < 3; i++) {
	int k, c = order[i];

	for (k = 0; k < test_palette_colors; k++) {
	    double rgb = (c=='r')
		? rgb1[k].r :
		((c=='g') ? rgb1[k].g : rgb1[k].b);

	    fprintf(f, "%0.4f\t%0.4f\n", z[k], rgb);
	}
	fputs("e\n", f);
    }
    fputs(post, f);

    /* save current gnuplot 'set' status because of the tricky set's for our temporary testing plot */
    save_set(f);

    /* execute all commands from the temporary file */
    rewind(f);
    load_file(f, NULL, FALSE); /* note: it does fclose(f) */

    /* enable reset_palette() and restore replot line */
    enable_reset_palette = 1;
    free(replot_line);
    replot_line = save_replot_line;
    is_3d_plot = save_is_3d_plot;

    /* further, input_line[] and token[] now destroyed! */
    c_token = num_tokens = 0;
#endif /* PM3D */
}


/* process the undocumented 'test time' command
 *	test time 'format' 'string'
 * to assist testing of time routines
 */
static void
test_time_subcommand()
{
    char *format = NULL;
    char *string = NULL;
    struct tm tm;
    double secs;

    /* given a format and a time string, exercise the time code */

    if (isstring(++c_token)) {
	m_quote_capture(&format, c_token, c_token);
	if (isstring(++c_token)) {
	    m_quote_capture(&string, c_token, c_token);
	    memset(&tm, 0, sizeof(tm));
	    gstrptime(string, format, &tm);
	    secs = gtimegm(&tm);
	    fprintf(stderr, "internal = %f - %d/%d/%d::%d:%d:%d , wday=%d, yday=%d\n",
		    secs, tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100,
		    tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_wday,
		    tm.tm_yday);
	    memset(&tm, 0, sizeof(tm));
	    ggmtime(&tm, secs);
	    gstrftime(string, strlen(string), format, secs);
	    fprintf(stderr, "convert back \"%s\" - %d/%d/%d::%d:%d:%d , wday=%d, yday=%d\n",
		    string, tm.tm_mday, tm.tm_mon + 1, tm.tm_year % 100,
		    tm.tm_hour, tm.tm_min, tm.tm_sec, tm.tm_wday,
		    tm.tm_yday);
	    free(string);
	    ++c_token;
	} /* else: expecting time string */
	free(format);
    } /* else: expecting format string */
}


/* process the 'test' command */
void
test_command()
{
    int what;
    c_token++;
    if (END_OF_COMMAND) {
	test_term();
	return;
    }

    what = lookup_table(&test_tbl[0], c_token);
    switch (what) {
	case TEST_TERMINAL: test_term(); break;
	case TEST_PALETTE: test_palette_subcommand(); break;
	case TEST_TIME: test_time_subcommand(); break;
	default:
#if 1
	    int_error(c_token, "none or keyword 'terminal' or 'palette' expected");
#else
	    /* don't document undocumented command :-) */
	    int_error(c_token, "none or keyword 'terminal', 'palette' or 'time' expected");
#endif
    }
}


/* unset_command is in unset.c */


/* process the 'update' command */
void
update_command()
{
    /* old parameter filename */
    char *opfname = NULL;
    /* new parameter filename */
    char *npfname = NULL;

    c_token++;
    if (!(opfname = try_to_get_string()))
	int_error(c_token, "Parameter filename expected");
    if ((END_OF_COMMAND) || !(npfname = try_to_get_string()))
	int_error(c_token, "New parameter filename expected");

    update(opfname, npfname);
    free(npfname);
    free(opfname);
}


/* process invalid commands and, on OS/2, REXX commands */
void
invalid_command()
{
#ifdef OS2
   if (token[c_token].is_token) {
      int rc;
      rc = ExecuteMacro(input_line + token[c_token].start_index,
	      token[c_token].length);
      if (rc == 0) {
         c_token = num_tokens = 0;
         return;
      }
    }
#endif
    int_error(c_token, "invalid command");
}


/*
 * Auxiliary routines
 */

/* used by changedir_command() */
static int
changedir(char *path)
{
#if defined(MSDOS) || defined(WIN16) || defined(ATARI) || defined(DOS386)
# if defined(__ZTC__)
    unsigned dummy;		/* it's a parameter needed for dos_setdrive */
# endif

    /* first deal with drive letter */

    if (isalpha(path[0]) && (path[1] == ':')) {
	int driveno = toupper(path[0]) - 'A';	/* 0=A, 1=B, ... */

# if defined(ATARI)
	(void) Dsetdrv(driveno);
# endif

# if defined(__ZTC__)
	(void) dos_setdrive(driveno + 1, &dummy);
# endif

# if (defined(MSDOS) && defined(__EMX__)) || defined(__MSC__)
	(void) _chdrive(driveno + 1);
# endif


/* HBB: recent versions of DJGPP also have setdisk():,
 * so I del'ed the special code */
# if ((defined(MSDOS) || defined(_Windows)) && defined(__TURBOC__)) || defined(DJGPP)
	(void) setdisk(driveno);
# endif
	path += 2;		/* move past drive letter */
    }
    /* then change to actual directory */
    if (*path)
	if (chdir(path))
	    return 1;

    return 0;			/* should report error with setdrive also */

#elif defined(WIN32)
    return !(SetCurrentDirectory(path));
#elif defined(__EMX__) && defined(OS2)
    return _chdir2(path);
#else
    return chdir(path);
#endif /* MSDOS, ATARI etc. */
}


/* used by replot_command() */
void
replotrequest()
{
    if (equals(c_token, "["))
	int_error(c_token, "cannot set range with replot");

    /* do not store directly into the replot_line string, until the
     * new plot line has been successfully plotted. This way,
     * if user makes a typo in a replot line, they do not have
     * to start from scratch. The replot_line will be committed
     * after do_plot has returned, whence we know all is well
     */
    if (END_OF_COMMAND) {
	char *rest_args = &input_line[token[c_token].start_index];
	size_t replot_len = strlen(replot_line);
	size_t rest_len = strlen(rest_args);

	/* preserve commands following 'replot ;' */
	/* move rest of input line to the start
	 * necessary because of realloc() in extend_input_line() */
	memmove(input_line,rest_args,rest_len+1);
	/* reallocs if necessary */
	while (input_line_len < replot_len+rest_len+1)
	    extend_input_line();
	/* move old rest args off begin of input line to
	 * make space for replot_line */
	memmove(input_line+replot_len,input_line,rest_len+1);
	/* copy previous plot command to start of input line */
	memcpy(input_line, replot_line, replot_len);
    } else {
	char *replot_args = NULL;	/* else m_capture will free it */
	int last_token = num_tokens - 1;

	/* length = length of old part + length of new part + ", " + \0 */
	size_t newlen = strlen(replot_line) + token[last_token].start_index +
	token[last_token].length - token[c_token].start_index + 3;

	m_capture(&replot_args, c_token, last_token);	/* might be empty */
	while (input_line_len < newlen)
	    extend_input_line();
	strcpy(input_line, replot_line);
	strcat(input_line, ", ");
	strcat(input_line, replot_args);
	free(replot_args);
    }
    plot_token = 0;		/* whole line to be saved as replot line */

    screen_ok = FALSE;
    num_tokens = scanner(&input_line, &input_line_len);
    c_token = 1;		/* skip the 'plot' part */
    if (is_3d_plot)
	plot3drequest();
    else
	plotrequest();
}


/* Is 'set view map' currently working inside 'splot' or not? Calculation of
 * mouse coordinates and the corresponding routines must know it, because
 * 'splot' can be either true 3D plot or a 2D map.
 * This flag is set when entering splot command and 'set view map', i.e. by
 * splot_map_activate(), and reset when calling splot_map_deactivate().
 */
static int splot_map_active = 0;
/* Store values reset by 'set view map' during splot, used by those two
 * routines below.
 */
static float splot_map_surface_rot_x;
static float splot_map_surface_rot_z;
static float splot_map_surface_scale;

/* This routine is called at the beginning of 'splot'. It sets up some splot
 * parameters needed to present the 'set view map'.
 */
void
splot_map_activate()
{
    if (splot_map_active)
	return;
    splot_map_active = 1;
    /* save current values */
    splot_map_surface_rot_x = surface_rot_x;
    splot_map_surface_rot_z = surface_rot_z ;
    splot_map_surface_scale = surface_scale;
    /* set new values */
    surface_rot_x = 180;
    surface_rot_z = 0;
    surface_scale = 1.3;
    axis_array[FIRST_Y_AXIS].range_flags  ^= RANGE_REVERSE;
    axis_array[SECOND_Y_AXIS].range_flags ^= RANGE_REVERSE;
	/* note: ^ is xor */
}


/* This routine is called when the current 'set view map' is no more needed,
 * i.e., when calling "plot" --- the reversed y-axis et al must still be
 * available for mousing.
 */
void
splot_map_deactivate()
{
    if (!splot_map_active)
	return;
    splot_map_active = 0;
    /* restore the original values */
    surface_rot_x = splot_map_surface_rot_x;
    surface_rot_z = splot_map_surface_rot_z;
    surface_scale = splot_map_surface_scale;
    axis_array[FIRST_Y_AXIS].range_flags  ^= RANGE_REVERSE;
    axis_array[SECOND_Y_AXIS].range_flags ^= RANGE_REVERSE;
	/* note: ^ is xor */
}


/* Support for input, shell, and help for various systems */

#ifdef VMS

# include <descrip.h>
# include <rmsdef.h>
# include <smgdef.h>
# include <smgmsg.h>
# include <ssdef.h>

extern lib$get_input(), lib$put_output();
extern smg$read_composed_line();
extern sys$putmsg();
extern lbr$output_help();
extern lib$spawn();

int vms_len;

unsigned int status[2] = { 1, 0 };

static char Help[MAX_LINE_LEN+1] = "gnuplot";

$DESCRIPTOR(prompt_desc, PROMPT);
/* temporary fix until change to variable length */
struct dsc$descriptor_s line_desc =
{0, DSC$K_DTYPE_T, DSC$K_CLASS_S, NULL};

$DESCRIPTOR(help_desc, Help);
$DESCRIPTOR(helpfile_desc, "GNUPLOT$HELP");

/* HBB 990829: confirmed this to be used on VMS, only --> moved into
 * the VMS-specific section */
void
done(int status)
{
    term_reset();
    exit(status);
}

/* please note that the vms version of read_line doesn't support variable line
   length (yet) */

static int
read_line(const char *prompt)
{
    int more, start = 0;
    char expand_prompt[40];

    current_prompt = prompt;	/* HBB NEW 20040727 */

    prompt_desc.dsc$w_length = strlen(prompt);
    prompt_desc.dsc$a_pointer = (char *) prompt;
    strcpy(expand_prompt, "_");
    strncat(expand_prompt, prompt, 38);
    do {
	line_desc.dsc$w_length = MAX_LINE_LEN - start;
	line_desc.dsc$a_pointer = &input_line[start];
	switch (status[1] = smg$read_composed_line(&vms_vkid, &vms_ktid, &line_desc, &prompt_desc, &vms_len)) {
	case SMG$_EOF:
	    done(EXIT_SUCCESS);	/* ^Z isn't really an error */
	    break;
	case RMS$_TNS:		/* didn't press return in time */
	    vms_len--;		/* skip the last character */
	    break;		/* and parse anyway */
	case RMS$_BES:		/* Bad Escape Sequence */
	case RMS$_PES:		/* Partial Escape Sequence */
	    sys$putmsg(status);
	    vms_len = 0;	/* ignore the line */
	    break;
	case SS$_NORMAL:
	    break;		/* everything's fine */
	default:
	    done(status[1]);	/* give the error message */
	}
	start += vms_len;
	input_line[start] = NUL;
	inline_num++;
	if (input_line[start - 1] == '\\') {
	    /* Allow for a continuation line. */
	    prompt_desc.dsc$w_length = strlen(expand_prompt);
	    prompt_desc.dsc$a_pointer = expand_prompt;
	    more = 1;
	    --start;
	} else {
	    line_desc.dsc$w_length = strlen(input_line);
	    line_desc.dsc$a_pointer = input_line;
	    more = 0;
	}
    } while (more);
    return 0;
}


# ifdef NO_GIH
void
help_command()
{
    int first = c_token;

    while (!END_OF_COMMAND)
	++c_token;

    strcpy(Help, "GNUPLOT ");
    capture(Help + 8, first, c_token - 1, sizeof(Help) - 9);
    help_desc.dsc$w_length = strlen(Help);
    if ((vaxc$errno = lbr$output_help(lib$put_output, 0, &help_desc,
				      &helpfile_desc, 0, lib$get_input)) != SS$_NORMAL)
	os_error(NO_CARET, "can't open GNUPLOT$HELP");
}
# endif				/* NO_GIH */


void
do_shell()
{
    screen_ok = FALSE;
    c_token++;

    if ((vaxc$errno = lib$spawn()) != SS$_NORMAL) {
	os_error(NO_CARET, "spawn error");
    }
}


static void
do_system(const char *cmd)
{

     if (!cmd)
	return;

    /* input_line is filled by read_line or load_file, but
     * line_desc length is set only by read_line; adjust now
     */
    line_desc.dsc$w_length = strlen(cmd);
    line_desc.dsc$a_pointer = (char *) cmd;

    if ((vaxc$errno = lib$spawn(&line_desc)) != SS$_NORMAL)
	os_error(NO_CARET, "spawn error");

    (void) putc('\n', stderr);

}
#endif /* VMS */


#ifdef _Windows
# ifdef NO_GIH
void
help_command()
{

    if (END_OF_COMMAND)
	WinHelp(textwin.hWndParent, (LPSTR) winhelpname, HELP_INDEX, (DWORD) NULL);
    else {
	char buf[128];
	int start = ++c_token;
	while (!(END_OF_COMMAND))
	    c_token++;
	capture(buf, start, c_token - 1, 128);
	WinHelp(textwin.hWndParent, (LPSTR) winhelpname, HELP_PARTIALKEY, (DWORD) buf);
    }
}
# endif				/* NO_GIH */
#endif /* _Windows */


/*
 * help_command: (not VMS, although it would work) Give help to the user. It
 * parses the command line into helpbuf and supplies help for that string.
 * Then, if there are subtopics available for that key, it prompts the user
 * with this string. If more input is given, help_command is called
 * recursively, with argument 0.  Thus a more specific help can be supplied.
 * This can be done repeatedly.  If null input is given, the function returns,
 * effecting a backward climb up the tree.
 * David Kotz (David.Kotz@Dartmouth.edu) 10/89
 * drd - The help buffer is first cleared when called with toplevel=1.
 * This is to fix a bug where help is broken if ^C is pressed whilst in the
 * help.
 * Lars - The "int toplevel" argument is gone. I have converted it to a
 * static variable.
 *
 * FIXME - helpbuf is never free()'d
 */

#ifndef NO_GIH
void
help_command()
{
    static char *helpbuf = NULL;
    static char *prompt = NULL;
    static int toplevel = 1;
    int base;			/* index of first char AFTER help string */
    int len;			/* length of current help string */
    TBOOLEAN more_help;
    TBOOLEAN only;		/* TRUE if only printing subtopics */
    TBOOLEAN subtopics;		/* 0 if no subtopics for this topic */
    int start;			/* starting token of help string */
    char *help_ptr;		/* name of help file */
# if defined(SHELFIND)
    static char help_fname[256] = "";	/* keep helpfilename across calls */
# endif

# if defined(ATARI) || defined(MTOS)
    char const *const ext[] = { NULL };
# endif

    if ((help_ptr = getenv("GNUHELP")) == (char *) NULL)
# ifndef SHELFIND
	/* if can't find environment variable then just use HELPFILE */

/* patch by David J. Liu for getting GNUHELP from home directory */
#  if (defined(__TURBOC__) && (defined(MSDOS) || defined(DOS386))) || defined(__DJGPP__)
	help_ptr = HelpFile;
#  else
#   if defined(ATARI) || defined(MTOS)
    {
	/* I hope findfile really can accept a NULL argument ... */
	if ((help_ptr = findfile(HELPFILE, user_gnuplotpath, ext)) == NULL)
	    help_ptr = findfile(HELPFILE, getenv("PATH"), ext);
	if (!help_ptr)
	    help_ptr = HELPFILE;
    }
#   else
    help_ptr = HELPFILE;
#   endif			/* ATARI || MTOS */
#  endif			/* __TURBOC__ */
#ifdef OS2
  {
  /* look in the path where the executable lives */
  static char buf[MAXPATHLEN];
  char *ptr;

  _execname(buf, sizeof(buf));
  _fnslashify(buf);
  ptr=strrchr(buf, '/');
  if (ptr) {
     *(ptr+1)='\0';
     strcat(buf, HELPFILE);
     help_ptr=&buf[0];
  }
  else
     help_ptr = HELPFILE;
  }
#endif
/* end of patch  - DJL */

# else				/* !SHELFIND */
    /* try whether we can find the helpfile via shell_find. If not, just
       use the default. (tnx Andreas) */

    if (!strchr(HELPFILE, ':') && !strchr(HELPFILE, '/') &&
	!strchr(HELPFILE, '\\')) {
	if (strlen(help_fname) == 0) {
	    strcpy(help_fname, HELPFILE);
	    if (shel_find(help_fname) == 0) {
		strcpy(help_fname, HELPFILE);
	    }
	}
	help_ptr = help_fname;
    } else {
	help_ptr = HELPFILE;
    }
# endif				/* !SHELFIND */

    /* Since MSDOS DGROUP segment is being overflowed we can not allow such  */
    /* huge static variables (1k each). Instead we dynamically allocate them */
    /* on the first call to this function...                                 */
    if (helpbuf == NULL) {
	helpbuf = gp_alloc(MAX_LINE_LEN, "help buffer");
	prompt = gp_alloc(MAX_LINE_LEN, "help prompt");
	helpbuf[0] = prompt[0] = 0;
    }
    if (toplevel)
	helpbuf[0] = prompt[0] = 0;	/* in case user hit ^c last time */

    /* if called recursively, toplevel == 0; toplevel must == 1 if called
     * from command() to get the same behaviour as before when toplevel
     * supplied as function argument
     */
    toplevel = 1;

    len = base = strlen(helpbuf);

    start = ++c_token;

    /* find the end of the help command */
    while (!(END_OF_COMMAND))
	c_token++;

    /* copy new help input into helpbuf */
    if (len > 0)
	helpbuf[len++] = ' ';	/* add a space */
    capture(helpbuf + len, start, c_token - 1, MAX_LINE_LEN - len);
    squash_spaces(helpbuf + base);	/* only bother with new stuff */
    lower_case(helpbuf + base);	/* only bother with new stuff */
    len = strlen(helpbuf);

    /* now, a lone ? will print subtopics only */
    if (strcmp(helpbuf + (base ? base + 1 : 0), "?") == 0) {
	/* subtopics only */
	subtopics = 1;
	only = TRUE;
	helpbuf[base] = NUL;	/* cut off question mark */
    } else {
	/* normal help request */
	subtopics = 0;
	only = FALSE;
    }

    switch (help(helpbuf, help_ptr, &subtopics)) {
    case H_FOUND:{
	    /* already printed the help info */
	    /* subtopics now is true if there were any subtopics */
	    screen_ok = FALSE;

	    do {
		if (subtopics && !only) {
		    /* prompt for subtopic with current help string */
		    if (len > 0) {
			strcpy (prompt, "Subtopic of ");
			strncat (prompt, helpbuf, MAX_LINE_LEN - 16);
			strcat (prompt, ": ");
		    } else
			strcpy(prompt, "Help topic: ");
		    read_line(prompt);
		    num_tokens = scanner(&input_line, &input_line_len);
		    c_token = 0;
		    more_help = !(END_OF_COMMAND);
		    if (more_help) {
			c_token--;
			toplevel = 0;
			/* base for next level is all of current helpbuf */
			help_command();
		    }
		} else
		    more_help = FALSE;
	    } while (more_help);

	    break;
	}
    case H_NOTFOUND:
	printf("Sorry, no help for '%s'\n", helpbuf);
	break;
    case H_ERROR:
	perror(help_ptr);
	break;
    default:
	int_error(NO_CARET, "Impossible case in switch");
	break;
    }

    helpbuf[base] = NUL;	/* cut it off where we started */
}
#endif /* !NO_GIH */

#ifndef VMS

static void
do_system(const char *cmd)
{
# ifdef AMIGA_AC_5
    static char *parms[80];
    if (!cmd)
	return;
    getparms(input_line + 1, parms);
    fexecv(parms[0], parms);
# elif (defined(ATARI) && defined(__GNUC__))
/* || (defined(MTOS) && defined(__GNUC__)) */
    /* use preloaded shell, if available */
    short (*shell_p) (char *command);
    void *ssp;

    if (!cmd)
	return;

    ssp = (void *) Super(NULL);
    shell_p = *(short (**)(char *)) 0x4f6;
    Super(ssp);

    /* this is a bit strange, but we have to have a single if */
    if (shell_p)
	(*shell_p) (cmd);
    else
	system(cmd);
# elif defined(_Windows) && defined(USE_OWN_WINSYSTEM_FUNCTION)
    if (!cmd)
	return;
    winsystem(cmd);
# else /* !(AMIGA_AC_5 || ATARI && __GNUC__ || _Windows) */
/* (am, 19980929)
 * OS/2 related note: cmd.exe returns 255 if called w/o argument.
 * i.e. calling a shell by "!" will always end with an error message.
 * A workaround has to include checking for EMX,OS/2, two environment
 *  variables,...
 */
    if (!cmd)
	return;
    system(cmd);
# endif /* !(AMIGA_AC_5 || ATARI&&__GNUC__ || _Windows) */
}


# ifdef AMIGA_AC_5
/******************************************************************************
 * Parses the command string (for fexecv use) and  converts the first token
 * to lower case
 *****************************************************************************/
static void
getparms(char *command, char **parms)
{
    static char strg0[256];
    int i = 0, j = 0, k = 0;		/* A bunch of indices */

    while (command[j] != NUL) {	/* Loop on string characters */
	parms[k++] = strg0 + i;
	while (command[j] == ' ')
	    ++j;
	while (command[j] != ' ' && command[j] != NUL) {
	    if (command[j] == '"') {	/* Get quoted string */
		do {
		    strg0[i++] = command[j++];
		} while (command[j] != '"' && command[j] != NUL);
	    }
	    strg0[i++] = command[j++];
	}
	if (strg0[i] != NUL)
	    strg0[i++] = NUL;	/* NUL terminate every token */
    }
    parms[k] = NUL;

    /* Convert to lower case */
    /* FIXME HBB 20010621: do we really want to stop on char *before*
     * the actual end of the string strg0[]? */
    for (k=0; strg0[k+1] != NUL; k++)
	if (strg0[k] >= 'A' && (strg0[k] <= 'Z'))
	    strg0[k] += ('a' - 'A');
}

# endif				/* AMIGA_AC_5 */


# if defined(READLINE) || defined(HAVE_LIBREADLINE)
/* keep some compilers happy */
static char *rlgets __PROTO((char *s, size_t n, const char *prompt));

static char *
rlgets(char *s, size_t n, const char *prompt)
{
    static char *line = (char *) NULL;
    static int leftover = -1;	/* index of 1st char leftover from last call */

    if (leftover == -1) {
	/* If we already have a line, first free it */
	if (line != (char *) NULL) {
	    free(line);
	    line = NULL;
	    /* so that ^C or int_error during readline() does
	     * not result in line being free-ed twice */
	}
	line = readline_ipc((interactive) ? prompt : "");
	leftover = 0;
	/* If it's not an EOF */
	if (line && *line) {
#  ifdef HAVE_LIBREADLINE
	    HIST_ENTRY *temp;

	    /* Must always be called at this point or
	     * 'temp' has the wrong value. */
	    using_history();
	    temp = previous_history();

	    if (temp == 0 || strcmp(temp->line, line) != 0)
		add_history(line);

#  else /* !HAVE_LIBREADLINE */
	    add_history(line);
#  endif
	}
    }
    if (line) {
	safe_strncpy(s, line + leftover, n);
	leftover += strlen(s);
	if (line[leftover] == NUL)
	    leftover = -1;
	return s;
    }
    return NULL;
}
# endif				/* READLINE || HAVE_LIBREADLINE */


# if defined(MSDOS) || defined(_Windows) || defined(DOS386)
void
do_shell()
{
    screen_ok = FALSE;
    c_token++;

    if (user_shell) {
#  if defined(_Windows)
	if (WinExec(user_shell, SW_SHOWNORMAL) <= 32)
#  elif defined(DJGPP)
	    if (system(user_shell) == -1)
#  else
		if (spawnl(P_WAIT, user_shell, NULL) == -1)
#  endif			/* !(_Windows || DJGPP) */
		    os_error(NO_CARET, "unable to spawn shell");
    }
}

# elif defined(AMIGA_SC_6_1)

void
do_shell()
{
    screen_ok = FALSE;
    c_token++;

    if (user_shell) {
	if (system(user_shell))
	    os_error(NO_CARET, "system() failed");
    }
    (void) putc('\n', stderr);
}

#  elif defined(OS2)

void
do_shell()
{
    screen_ok = FALSE;
    c_token++;

    if (user_shell) {
	if (system(user_shell) == -1)
	    os_error(NO_CARET, "system() failed");

    }
    (void) putc('\n', stderr);
}

#  else				/* !OS2 */

/* plain old Unix */

#define EXEC "exec "
void
do_shell()
{
    static char exec[100] = EXEC;

    screen_ok = FALSE;
    c_token++;

    if (user_shell) {
	if (system(safe_strncpy(&exec[sizeof(EXEC) - 1], user_shell,
				sizeof(exec) - sizeof(EXEC) - 1)))
	    os_error(NO_CARET, "system() failed");
    }
    (void) putc('\n', stderr);
}

# endif				/* !MSDOS */

/* read from stdin, everything except VMS */

# if !defined(READLINE) && !defined(HAVE_LIBREADLINE)
#  if (defined(MSDOS) || defined(DOS386)) && !defined(_Windows) && !defined(__EMX__) && !defined(DJGPP)

/* if interactive use console IO so CED will work */

#define PUT_STRING(s) cputs(s)
#define GET_STRING(s,l) ((interactive) ? cgets_emu(s,l) : fgets(s,l,stdin))

#   ifdef __TURBOC__
/* cgets implemented using dos functions */
/* Maurice Castro 22/5/91 */
static char *doscgets __PROTO((char *));

static char *
doscgets(char *s)
{
    long datseg;

    /* protect and preserve segments - call dos to do the dirty work */
    datseg = _DS;

    _DX = FP_OFF(s);
    _DS = FP_SEG(s);
    _AH = 0x0A;
    geninterrupt(33);
    _DS = datseg;

    /* check for a carriage return and then clobber it with a null */
    if (s[s[1] + 2] == '\r')
	s[s[1] + 2] = 0;

    /* return the input string */
    return (&(s[2]));
}
#   endif			/* __TURBOC__ */

#   ifdef __ZTC__
void
cputs(char *s)
{
    int i = 0;
    while (s[i] != NUL)
	bdos(0x02, s[i++], NULL);
}

char *
cgets(char *s)
{
    bdosx(0x0A, s, NULL);

    if (s[s[1] + 2] == '\r')
	s[s[1] + 2] = 0;

    /* return the input string */
    return (&(s[2]));
}
#   endif			/* __ZTC__ */

/* emulate a fgets like input function with DOS cgets */
char *
cgets_emu(char *str, int len)
{
    static char buffer[128] = "";
    static int leftover = 0;

    if (buffer[leftover] == NUL) {
	buffer[0] = 126;
#   ifdef __TURBOC__
	doscgets(buffer);
#   else
	cgets(buffer);
#   endif
	fputc('\n', stderr);
	if (buffer[2] == 26)
	    return NULL;
	leftover = 2;
    }
    safe_strncpy(str, buffer + leftover, len);
    leftover += strlen(str);
    return str;
}
#  else				/* !plain DOS */

#   define PUT_STRING(s) fputs(s, stderr)
#   define GET_STRING(s,l) fgets(s, l, stdin)

#  endif			/* !plain DOS */
# endif				/* !READLINE && !HAVE_LIBREADLINE) */

/* this function is called for non-interactive operation. Its usage is
 * like fgets(), but additionally it checks for ipc events from the
 * terminals waitforinput() (if USE_MOUSE, and terminal is
 * mouseable). This function will be used when reading from a pipe. */
static char*
fgets_ipc(
    char *dest,			/* string to fill */
    int len)			/* size of it */
{
#ifdef USE_MOUSE
    if (term && term->waitforinput) {
	/* This a mouseable terminal --- must expect input from it */
	int c;			/* char got from waitforinput() */
	size_t i=0;		/* position inside dest */

	dest[0] = '\0';
	for (i=0; i < len; i++) {
	    c = term->waitforinput();
	    if ('\n' == c) {
		dest[i] = '\0';
		return dest;
	    } else if (EOF == c) {
		return (char*) 0;
	    } else {
		dest[i] = c;
	    }
	}
	return dest;
    } else
#endif
	return fgets(dest, len, stdin);
}

/* Non-VMS version */
static int
read_line(const char *prompt)
{
    int start = 0;
    TBOOLEAN more = FALSE;
    int last = 0;

    current_prompt = prompt;	/* HBB NEW 20040727 */

# if !defined(READLINE) && !defined(HAVE_LIBREADLINE)
    if (interactive)
	PUT_STRING(prompt);
# endif				/* no READLINE */

    do {
	/* grab some input */
# if defined(READLINE) || defined(HAVE_LIBREADLINE)
	if (((interactive)
	     ? rlgets(input_line + start, input_line_len - start,
		     ((more) ? "> " : prompt))
	     : fgets_ipc(input_line + start, input_line_len - start)
	    ) == (char *) NULL)
# else /* !(READLINE || HAVE_LIBREADLINE) */
	if (GET_STRING(input_line + start, input_line_len - start)
	    == (char *) NULL)
# endif /* !(READLINE || HAVE_LIBREADLINE) */
	{
	    /* end-of-file */
	    if (interactive)
		(void) putc('\n', stderr);
	    input_line[start] = NUL;
	    inline_num++;
	    if (start > 0)	/* don't quit yet - process what we have */
		more = FALSE;
	    else
		return (1);	/* exit gnuplot */
	} else {
	    /* normal line input */
	    last = strlen(input_line) - 1;
	    if (last >= 0) {
		if (input_line[last] == '\n') {	/* remove any newline */
		    input_line[last] = NUL;
		    /* Watch out that we don't backup beyond 0 (1-1-1) */
		    if (last > 0)
			--last;
		} else if (last + 2 >= input_line_len) {
		    extend_input_line();
		    if (input_line[last] != '\\') {
			/* read rest of line, don't print "> " */
			start = last + 1;
			more = TRUE;
			continue;
		    }
		    /* else fall through to continuation handling */
		} /* if(grow buffer?) */
		if (input_line[last] == '\\') {
		    /* line continuation */
		    start = last;
		    more = TRUE;
		} else
		    more = FALSE;
	    } else
		more = FALSE;
	}
# if !defined(READLINE) && !defined(HAVE_LIBREADLINE)
	if (more && interactive)
	    PUT_STRING("> ");
# endif
    } while (more);
    return (0);
}

#endif /* !VMS */

#ifdef _Windows
/* there is a system like call on MS Windows but it is a bit difficult to
   use, so we will invoke the command interpreter and use it to execute the
   commands */
static int
winsystem(const char *s)
{
    LPSTR comspec;
    LPSTR execstr;
    LPCSTR p;

    /* get COMSPEC environment variable */
# ifdef WIN32
    char envbuf[81];
    GetEnvironmentVariable("COMSPEC", envbuf, 80);
    if (*envbuf == NUL)
	comspec = "\\command.com";
    else
	comspec = envbuf;
# else
    p = GetDOSEnvironment();
    comspec = "\\command.com";
    while (*p) {
	if (!strncmp(p, "COMSPEC=", 8)) {
	    comspec = p + 8;
	    break;
	}
	p += strlen(p) + 1;
    }
# endif
    /* if the command is blank we must use command.com */
    p = s;
    while ((*p == ' ') || (*p == '\n') || (*p == '\r'))
	p++;
    if (*p == NUL) {
	WinExec(comspec, SW_SHOWNORMAL);
    } else {
	/* attempt to run the windows/dos program via windows */
	if (WinExec(s, SW_SHOWNORMAL) <= 32) {
	    /* attempt to run it as a dos program from command line */
	    execstr = gp_alloc(strlen(s) + strlen(comspec) + 6,
			       "winsystem cmdline");
	    strcpy(execstr, comspec);
	    strcat(execstr, " /c ");
	    strcat(execstr, s);
	    WinExec(execstr, SW_SHOWNORMAL);
	    free(execstr);
	}
    }

    /* regardless of the reality return OK - the consequences of */
    /* failure include shutting down Windows */
    return (0);			/* success */
}

void
call_kill_pending_Pause_dialog()
{
    kill_pending_Pause_dialog();
}
#endif /* _Windows */
