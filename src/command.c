#ifndef lint
static char *RCSid() { return RCSid("$Id: command.c,v 1.230 2011/11/12 11:12:35 markisch Exp $"); }
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
 * May 2011 Ethan A Merritt
 * Introduce block structure defined by { ... }, which may span multiple lines.
 * In order to have the entire block available at one time we now count
 * +/- curly brackets during input and keep extending the current input line
 * until the net count is zero.  This is done in do_line() for interactive
 * input, and load_file() for non-interactive input.
 */

#include "command.h"

#include "axis.h"

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
#include "stats.h"
#include "tables.h"
#include "term_api.h"
#include "util.h"

#ifdef USE_MOUSE
# include "mouse.h"
int paused_for_mouse = 0;
#endif

#define PROMPT "gnuplot> "

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
# ifdef USE_OWN_WINSYSTEM_FUNCTION
static int winsystem __PROTO((const char *));
# endif
#endif /* _Windows */

#ifdef _Windows
# include <windows.h>
# ifdef __MSC__
#  include <malloc.h>
#  include <direct.h>          /* getcwd() */
# else
#  include <alloc.h>
#  ifndef __WATCOMC__
#   include <dir.h>		/* setdisk() */
#  endif
# endif				/* !MSC */
# ifdef WITH_HTML_HELP
#   include <htmlhelp.h>
# endif
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
static char* gp_get_string __PROTO((char *, size_t, const char *));
static int read_line __PROTO((const char *prompt, int start));
static void do_system __PROTO((const char *));
static void test_palette_subcommand __PROTO((void));
static void test_time_subcommand __PROTO((void));
static int find_clause __PROTO((int *, int *));

#ifdef GP_MACROS
TBOOLEAN expand_macros = FALSE;
#endif

struct lexical_unit *token;
int token_table_size;


char *gp_input_line;
size_t gp_input_line_len;
int inline_num;			/* input line number */

struct udft_entry *dummy_func;

/* support for replot command */
char *replot_line;
int plot_token;			/* start of 'plot' command */

/* flag to disable `replot` when some data are sent through stdin;
 * used by mouse/hotkey capable terminals */
TBOOLEAN replot_disabled = FALSE;

/* output file for the print command */
FILE *print_out = NULL;
char *print_out_name = NULL;

/* input data, parsing variables */
int num_tokens, c_token;

int if_depth = 0;
TBOOLEAN if_condition = FALSE;
TBOOLEAN if_open_for_else = FALSE;

static int clause_depth = 0;

static int command_exit_status = 0;

/* support for dynamic size of input line */
void
extend_input_line()
{
    if (gp_input_line_len == 0) {
	/* first time */
	gp_input_line = gp_alloc(MAX_LINE_LEN, "gp_input_line");
	gp_input_line_len = MAX_LINE_LEN;
	gp_input_line[0] = NUL;

#ifdef OS2_IPC
	sprintf( mouseSharedMemName, "\\SHAREMEM\\GP%i_Mouse_Input", getpid() );
	if (DosAllocSharedMem((PVOID) & input_from_PM_Terminal,
		mouseSharedMemName, MAX_LINE_LEN, PAG_WRITE | PAG_COMMIT))
	    fputs("command.c: DosAllocSharedMem ERROR\n",stderr);
#endif /* OS2_IPC */

    } else {
	gp_input_line = gp_realloc(gp_input_line, gp_input_line_len + MAX_LINE_LEN,
				"extend input line");
	gp_input_line_len += MAX_LINE_LEN;
	FPRINTF((stderr, "extending input line to %d chars\n",
		 gp_input_line_len));
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
   thread_rl_RetCode = ( read_line(PROMPT, 0) );
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

	if (read_line("multiplot> ", 0))
	    return (1);
    } else {

#if defined(OS2_IPC) && defined(USE_MOUSE)
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
	    strcpy(gp_input_line, input_line_SharedMem);
	    input_line_SharedMem[0] = 0;
	    thread_rl_RetCode = 0;
	}
	if (thread_rl_RetCode)
	    return (1);

#else	/* The normal case */
	if (read_line(PROMPT, 0))
	    return (1);
#endif	/* defined(OS2_IPC) && defined(USE_MOUSE) */
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
    /* Line continuation has already been handled by read_line() */
    char *inlptr;

#ifdef GP_MACROS
    /* Expand any string variables in the current input line.
     * Allow up to 3 levels of recursion */
    if (expand_macros)
    if (string_expand_macros() && string_expand_macros() 
    &&  string_expand_macros() && string_expand_macros())
	int_error(NO_CARET, "Too many levels of nested macros");
#endif

    /* Skip leading whitespace */
    inlptr = gp_input_line;
    while (isspace((unsigned char) *inlptr))
	inlptr++;

    /* Strip off trailing comment */
    FPRINTF((stderr,"doline( \"%s\" )\n", gp_input_line));
    if (strchr(inlptr, '#')) {
        num_tokens = scanner(&gp_input_line, &gp_input_line_len);
	if (gp_input_line[token[num_tokens].start_index] == '#')
	    gp_input_line[token[num_tokens].start_index] = NUL;
    }

    if (inlptr != gp_input_line) {
	/* If there was leading whitespace, copy the actual
	 * command string to the front. use memmove() because
	 * source and target may overlap */
	memmove(gp_input_line, inlptr, strlen(inlptr));
	/* Terminate resulting string */
	gp_input_line[strlen(inlptr)] = NUL;
    }
    FPRINTF((stderr, "  echo: \"%s\"\n", gp_input_line));

    /* EAM May 2011 - This will not work in a bracketed clause. Should it? */
    if (is_system(gp_input_line[0])) {
	do_system(gp_input_line + 1);
	return (0);
    }

#if 0
    /* EAM May 2011 */
    /* Resetting here prevents handling "else" on a separate line */
    if_condition = TRUE;
#endif
    if_depth = 0;
    num_tokens = scanner(&gp_input_line, &gp_input_line_len);

    /* 
     * Expand line if necessary to contain a complete bracketed clause {...}
     * Insert a ';' after current line and append the next input line.
     * NB: This may leave an "else" condition on the next line.
     */
    if (curly_brace_count < 0)
	int_error(NO_CARET,"Unexpected }");
    while (curly_brace_count > 0) {
	strcat(gp_input_line,";");
	read_line("more> ", strlen(gp_input_line));
	num_tokens = scanner(&gp_input_line, &gp_input_line_len);
	if (gp_input_line[token[num_tokens].start_index] == '#')
	    gp_input_line[token[num_tokens].start_index] = NUL;
    }

    c_token = 0;
    while (c_token < num_tokens) {
	command();
	if (command_exit_status) {
	    command_exit_status = 0;
	    return 1;
	}
	if (c_token < num_tokens) {	/* something after command */
	    if (equals(c_token, ";")) {
		c_token++;
	    } else if (equals(c_token, "{")) {
		begin_clause();
	    } else if (equals(c_token, "}")) {
		end_clause();
	    } else
		int_error(c_token, "';' expected");
	}
    }
    return (0);
}


void
do_string(const char *s)
{
    char *cmdline = gp_strdup(s);
    do_string_and_free(cmdline);
}
 
void
do_string_and_free(char *cmdline)
{
#ifdef USE_MOUSE
    if (display_ipc_commands())
	fprintf(stderr, "%s\n", cmdline);
#endif

    lf_push(NULL, NULL, cmdline); /* save state for errors and recursion */
    while (gp_input_line_len < strlen(cmdline) + 1)
	extend_input_line();
    strcpy(gp_input_line, cmdline);
    screen_ok = FALSE;
    do_line();

    /* We don't know if screen_ok is appropriate so leave it FALSE. */
    lf_pop();
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
do_string_replot(const char *s)
{
    do_string(s);

#ifdef VOLATILE_REFRESH
    if (volatile_data && refresh_ok) {
	if (display_ipc_commands())
	    fprintf(stderr, "refresh\n");
	refresh_request();
    } else
#endif

    if (!replot_disabled)
	replotrequest();
    else
	int_warn(NO_CARET, "refresh not possible and replot is disabled");

}

void
restore_prompt()
{
    if (interactive) {
#if defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
#  if !defined(MISSING_RL_FORCED_UPDATE_DISPLAY)
	rl_forced_update_display();
#  else
	rl_redisplay();
#  endif
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
    struct value result;

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
	udf->dummy_num = dummy_num;
	if ((at_tmp = perm_at()) == (struct at_type *) NULL)
	    int_error(start_token, "not enough memory for function");
	if (udf->at)		/* already a dynamic a.t. there */
	    free_at(udf->at);	/* so free it first */
	udf->at = at_tmp;	/* before re-assigning it. */
	memcpy(c_dummy_var, save_dummy, sizeof(save_dummy));
	m_capture(&(udf->definition), start_token, c_token - 1);
	dummy_func = NULL;	/* dont let anyone else use our workspace */

	/* Save function definition in a user-accessible variable */
	if (1) {
	    char *tmpnam = gp_alloc(8+strlen(udf->udf_name), "varname");
	    strcpy(tmpnam, "GPFUN_");
	    strcat(tmpnam, udf->udf_name);
	    fill_gpval_string(tmpnam, udf->definition);
	    free(tmpnam);
	}
    } else {
	/* variable ! */
	char *varname = gp_input_line + token[c_token].start_index;
	if (!strncmp(varname, "GPVAL_", 6) || !strncmp(varname, "MOUSE_", 6))
	    int_error(c_token, "Cannot set internal variables GPVAL_ and MOUSE_");
	start_token = c_token;
	c_token += 2;
	udv = add_udv(start_token);
	(void) const_express(&result);
	/* Prevents memory leak if the variable name is re-used */
	if (!udv->udv_undef)
	    gpfree_string(&udv->udv_value);
	udv->udv_value = result;
	udv->udv_undef = FALSE;
    }
}


void
undefine_command()
{
    char key[MAX_ID_LEN+1];
    TBOOLEAN wildcard;

    c_token++;               /* consume the command name */

    while (!END_OF_COMMAND) {
        /* copy next var name into key */
        copy_str(key, c_token, MAX_ID_LEN);

	/* Peek ahead - must do this, because a '*' is returned as a 
	   separate token, not as part of the 'key' */
	wildcard = equals(c_token+1,"*");
	if (wildcard)
	    c_token++;

        /* ignore internal variables */
	if (strncmp(key, "GPVAL_", 6) && strncmp(key, "MOUSE_", 6))
	    del_udv_by_name( key, wildcard );

	c_token++;
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


/* process the 'raise' or 'lower' command */
void
raise_lower_command(int lower)
{
    ++c_token;

    if (END_OF_COMMAND) {
	if (lower) {
#ifdef OS2
	    pm_lower_terminal_window();
#endif
#ifdef X11
	    x11_lower_terminal_group();
#endif
#ifdef _Windows
	    win_lower_terminal_group();
#endif
#ifdef WXWIDGETS
	    wxt_lower_terminal_group();
#endif
	} else {
#ifdef OS2
	    pm_raise_terminal_window();
#endif
#ifdef X11
	    x11_raise_terminal_group();
#endif
#ifdef _Windows
	    win_raise_terminal_group();
#endif
#ifdef WXWIDGETS
	    wxt_raise_terminal_group();
#endif
	}
	return;
    } else {
	int number;
	int negative = equals(c_token, "-");

	if (negative || equals(c_token, "+")) c_token++;
	if (!END_OF_COMMAND && isanumber(c_token)) {
	    number = real_expression();
	    if (negative)
	    number = -number;
	    if (lower) {
#ifdef OS2
		pm_lower_terminal_window();
#endif
#ifdef X11
		x11_lower_terminal_window(number);
#endif
#ifdef _Windows
		win_lower_terminal_window(number);
#endif
#ifdef WXWIDGETS
		wxt_lower_terminal_window(number);
#endif
	    } else {
#ifdef OS2
		pm_raise_terminal_window();
#endif
#ifdef X11
		x11_raise_terminal_window(number);
#endif
#ifdef _Windows
		win_raise_terminal_window(number);
#endif
#ifdef WXWIDGETS
		wxt_raise_terminal_window(number);
#endif
	    }
	    ++c_token;
	    return;
	}
    }
    if (lower)
	int_error(c_token, "usage: lower {plot_id}");
    else
	int_error(c_token, "usage: raise {plot_id}");
}

void
raise_command(void)
{
    raise_lower_command(0);
}

void
lower_command(void)
{
    raise_lower_command(1);
}


#ifdef USE_MOUSE

#define WHITE_AFTER_TOKEN(x) \
(' ' == gp_input_line[token[x].start_index + token[x].length] \
|| '\t' == gp_input_line[token[x].start_index + token[x].length] \
|| '\0' == gp_input_line[token[x].start_index + token[x].length])

/* process the 'bind' command */
void
bind_command()
{
    char* lhs = (char*) 0;
    char* rhs = (char*) 0;
    TBOOLEAN allwindows = FALSE;
    ++c_token;

    if (!END_OF_COMMAND && equals(c_token,"!")) {
	bind_remove_all();
	++c_token;
	return;
    }

    if (!END_OF_COMMAND && almost_equals(c_token,"all$windows")) {
	allwindows = TRUE;
	c_token++;
    }

    /* get left hand side: the key or key sequence */
    if (!END_OF_COMMAND) {
	char* first = gp_input_line + token[c_token].start_index;
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
     * of gp_input_line is too big, but shouldn't hurt too much. */
    if (!END_OF_COMMAND) {
	rhs = (char*) gp_alloc(strlen(gp_input_line) + 1, "bind_command->rhs");
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

    /* bind_process() will eventually free lhs / rhs ! */
    bind_process(lhs, rhs, allwindows);

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
    else
	update_gpval_variables(5);
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

/* process the 'evaluate' command */
void
eval_command()
{
    char *command;
    c_token++;
    command = try_to_get_string();
    if (!command)
	int_error(c_token, "Expected command string");
    do_string_and_free(command);
}


/* process the 'exit' and 'quit' commands */
void
exit_command()
{
    /* If the command is "exit gnuplot" then do so */
    if (equals(c_token+1,"gnuplot"))
	exit(0);

    /* else graphics will be tidied up in main */
    command_exit_status = 1;
}


/* fit_command() is in fit.c */


/* help_command() is below */


/* process the 'history' command */
void
history_command()
{
#if defined(READLINE) || defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
    c_token++;

    if (!END_OF_COMMAND && equals(c_token,"?")) {
	static char *search_str = NULL;  /* string from command line to search for */

	/* find and show the entries */
	c_token++;
	m_capture(&search_str, c_token, c_token);  /* reallocates memory */
	printf ("history ?%s\n", search_str);
	if (!history_find_all(search_str))
	    int_error(c_token,"not in history");
	c_token++;

    } else if (!END_OF_COMMAND && equals(c_token,"!")) {
	char *search_str = NULL;  /* string from command line to search for */
	const char *line_to_do = NULL;  /* command to execute at end if necessary */

	c_token++;
	m_capture(&search_str, c_token, c_token);
	line_to_do = history_find(search_str);
	free(search_str);
	if (line_to_do == NULL)
	    int_error(c_token, "not in history");
	printf("  Executing:\n\t%s\n", line_to_do);
	do_string(line_to_do);
	c_token++;

    } else {
	int n = 0;		   /* print only <last> entries */
	TBOOLEAN append = FALSE;   /* rewrite output file or append it */
	static char *name = NULL;  /* name of the output file; NULL for stdout */

	TBOOLEAN quiet = FALSE;
	if (!END_OF_COMMAND && almost_equals(c_token,"q$uiet")) {
	    /* option quiet to suppress history entry numbers */
	    quiet = TRUE;
	    c_token++;
	}
	/* show history entries */
	if (!END_OF_COMMAND && isanumber(c_token)) {
	    n = int_expression();
	}
	free(name);
	if ((name = try_to_get_string())) {
	    if (!END_OF_COMMAND && almost_equals(c_token, "ap$pend")) {
		append = TRUE;
		c_token++;
	    }
	}
	write_history_n(n, (quiet ? "" : name), (append ? "a" : "w"));
    }

#else
    c_token++;
    int_warn(NO_CARET, "You have to compile gnuplot with builtin readline or GNU readline or BSD editline to enable history support.");
#endif /* defined(READLINE) || defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE) */
}

#define REPLACE_ELSE(tok)             \
do {                                  \
    int idx = token[tok].start_index; \
    token[tok].length = 1;            \
    gp_input_line[idx++] = ';'; /* e */  \
    gp_input_line[idx++] = ' '; /* l */  \
    gp_input_line[idx++] = ' '; /* s */  \
    gp_input_line[idx++] = ' '; /* e */  \
} while (0)

#if 0
#define PRINT_TOKEN(tok)                                                    \
do {                                                                        \
    int i;                                                                  \
    int end_index = token[tok].start_index + token[tok].length;             \
    for (i = token[tok].start_index; i < end_index && gp_input_line[i]; i++) { \
	fputc(gp_input_line[i], stderr);                                       \
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

    if (!equals(++c_token, "("))	/* no expression */
	int_error(c_token, "expecting (expression)");
    exprval = real_expression();

    /*
     * EAM May 2011
     * New if {...} else {...} syntax can span multiple lines.
     * Isolate the active clause and execute it recursively.
     */
    if (equals(c_token,"{")) {
	/* Identify start and end position of the clause substring */
	char *clause = NULL;
	int if_start, if_end, else_start=0, else_end=0;
	int clause_start, clause_end;

	c_token = find_clause(&if_start, &if_end);

	if (equals(c_token,"else")) {
	    if (!equals(++c_token,"{"))
		int_error(c_token,"expected {else-clause}");
	    c_token = find_clause(&else_start, &else_end);
	}

	if (exprval != 0) {
	    clause_start = if_start;
	    clause_end = if_end;
	    if_condition = TRUE;
	} else {
	    clause_start = else_start;
	    clause_end = else_end;
	    if_condition = FALSE;
	}
	if_open_for_else = (else_start) ? FALSE : TRUE;

	clause_depth++;
	if (if_condition || else_start != 0) {
	    /* Make a clean copy without the opening and closing braces */
	    clause = gp_alloc(clause_end - clause_start, "clause");
	    memcpy(clause, &gp_input_line[clause_start+1], clause_end - clause_start);
	    clause[clause_end - clause_start - 1] = '\0';
	    FPRINTF((stderr,"%s CLAUSE: \"{%s}\"\n",
		    (exprval != 0.0) ? "IF" : "ELSE", clause));
	    do_string_and_free(clause);
	}

	c_token--; 	/* Let the parser see the closing curly brace */
	return;
    }

    /*
     * EAM May 2011
     * Old if/else syntax (no curly braces) affects the rest of the current line.
     * Deprecate?
     */
    if (clause_depth > 0)
	int_error(c_token,"Old-style if/else statement encountered inside brackets");
    if_depth++;
    if (exprval != 0.0) {
	/* fake the condition of a ';' between commands */
	int eolpos = token[num_tokens - 1].start_index + token[num_tokens - 1].length;
	--c_token;
	token[c_token].length = 1;
	token[c_token].start_index = eolpos + 2;
	gp_input_line[eolpos + 2] = ';';
	gp_input_line[eolpos + 3] = NUL;

	if_condition = TRUE;
    } else {
	while (c_token < num_tokens) {
	    /* skip over until the next command */
	    while (!END_OF_COMMAND) {
		++c_token;
	    }
	    if (equals(++c_token, "else")) {
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
   /*
    * EAM May 2011
    * New if/else syntax permits else clause to appear on a new line
    */
    if (equals(c_token+1,"{")) {
	int clause_start, clause_end;
	char *clause;

	if (if_open_for_else)
	    if_open_for_else = FALSE;
	else
	    int_error(c_token,"Invalid {else-clause}");

	c_token++;	/* Advance to the opening curly brace */
	c_token = find_clause(&clause_start, &clause_end);
	c_token--;	/* Let the parser eventually see the closing curly brace */

	clause_depth++;
	if (!if_condition) {
	    clause = gp_alloc(clause_end - clause_start, "clause");
	    memcpy(clause, &gp_input_line[clause_start+1], clause_end - clause_start);
	    clause[clause_end - clause_start - 1] = '\0';
	    do_string_and_free(clause);
	}
	return;
    }


   /* EAM May 2011
    * The rest is only relevant to the old if/else syntax (no curly braces)
    */
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

/* process commands of the form 'do for [i=1:N] ...' */
void
do_command()
{
    t_iterator *do_iterator;
    int do_start, do_end;
    char *clause;

    c_token++;
    do_iterator = check_for_iteration();

    if (!equals(c_token,"{"))
	int_error(c_token,"expecting {do-clause}");
    c_token = find_clause(&do_start, &do_end);

    clause_depth++;
    c_token--;	 /* Let the parser see the closing curly brace */

    clause = gp_alloc(do_end - do_start, "clause");
    memcpy(clause, &gp_input_line[do_start+1], do_end - do_start);
    clause[do_end - do_start - 1] = '\0';

    do {
	do_string(clause);
    } while (next_iteration(do_iterator));

    free(clause);
    do_iterator = cleanup_iteration(do_iterator);
}

/* process commands of the form 'while (foo) {...}' */
void
while_command()
{
    int do_start, do_end;
    char *clause;
    int save_token, end_token;
    double exprval;

    c_token++;
    save_token = c_token;
    exprval = real_expression();

    if (!equals(c_token,"{"))
	int_error(c_token,"expecting {while-clause}");
    end_token = find_clause(&do_start, &do_end);

    clause = gp_alloc(do_end - do_start, "clause");
    memcpy(clause, &gp_input_line[do_start+1], do_end - do_start);
    clause[do_end - do_start - 1] = '\0';
    clause_depth++;

    while (exprval != 0) {
	do_string(clause);
	c_token = save_token;
	exprval = real_expression();
    };

    free(clause);
    c_token = end_token;
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
}



/* null command */
void
null_command()
{
    return;
}

/* Clauses enclosed by curly brackets:
 * do for [i = 1:N] { a; b; c; }
 * if (<test>) {
 *    line1;
 *    line2;
 * } else {
 *    ...
 * }
 */

/* Find the start and end character positions within gp_input_line
 * bounding a clause delimited by {...}.
 * Assumes that c_token indexes the opening left curly brace.
 */
int
find_clause(int *clause_start, int *clause_end)
{
    int i, depth;

    *clause_start = token[c_token].start_index;
    for (i=++c_token, depth=1; i<num_tokens; i++) {
	if (equals(i,"{"))
	    depth++;
	else if (equals(i,"}"))
	    depth--;
	if (depth == 0)
	    break;
    }
    *clause_end = token[i].start_index;

    return (i+1);
}

void
begin_clause()
{
    clause_depth++;
    c_token++;
    return;
}

void
end_clause()
{
    if (clause_depth == 0)
	int_error(c_token, "unexpected }");
    else
	clause_depth--;
    c_token++;
    return;
}

void
clause_reset_after_error()
{
    if (clause_depth)
	FPRINTF((stderr,"CLAUSE RESET after error at depth %d\n",clause_depth));
    clause_depth = 0;
}


/* process the 'pause' command */
void
pause_command()
{
    int text = 0;
    double sleep_time;
    static char *buf = NULL;

    c_token++;

#ifdef USE_MOUSE
    paused_for_mouse = 0;
    if (equals(c_token,"mouse")) {
	sleep_time = -1;
	c_token++;

/*	EAM FIXME - This is not the correct test; what we really want */
/*	to know is whether or not the terminal supports mouse feedback */
/*	if (term_initialised) { */
	if (mouse_setting.on && term) {
	    struct udvt_entry *current;
	    int end_condition = 0;

	    while (!(END_OF_COMMAND)) {
		if (almost_equals(c_token,"key$press")) {
		    end_condition |= PAUSE_KEYSTROKE;
		    c_token++;
		} else if (equals(c_token,",")) {
		    c_token++;
		} else if (equals(c_token,"any")) {
		    end_condition |= PAUSE_ANY;
		    c_token++;
		} else if (equals(c_token,"button1")) {
		    end_condition |= PAUSE_BUTTON1;
		    c_token++;
		} else if (equals(c_token,"button2")) {
		    end_condition |= PAUSE_BUTTON2;
		    c_token++;
		} else if (equals(c_token,"button3")) {
		    end_condition |= PAUSE_BUTTON3;
		    c_token++;
		} else if (equals(c_token,"close")) {
		    end_condition |= PAUSE_WINCLOSE;
		    c_token++;
		} else
		    break;
	    }

	    if (end_condition)
	        paused_for_mouse = end_condition;
	    else
	        paused_for_mouse = PAUSE_CLICK;

	    /* Set the pause mouse return codes to -1 */
	    current = add_udv_by_name("MOUSE_KEY");
	    current->udv_undef = FALSE;
	    Ginteger(&current->udv_value,-1);
	    current = add_udv_by_name("MOUSE_BUTTON");
	    current->udv_undef = FALSE;
	    Ginteger(&current->udv_value,-1);
	} else
	    int_warn(NO_CARET,"Mousing not active");
    } else
#endif
	sleep_time = real_expression();

    if (END_OF_COMMAND) {
	free(buf); /* remove the previous message */
	buf = gp_strdup("paused"); /* default message, used in Windows GUI pause dialog */
    } else {
	free(buf);
	buf = try_to_get_string();
	if (!buf)
	    int_error(c_token, "expecting string");
	else {
#ifdef _Windows
	    if (sleep_time >= 0)
#elif defined(OS2)
		if (strcmp(term->name, "pm") != 0 || sleep_time >= 0)
#endif /* _Windows */
			fputs(buf, stderr);
	    text = 1;
	}
    }

    if (sleep_time < 0) {
#if defined(_Windows)
# ifdef WXWIDGETS
	if (!strcmp(term->name, "wxt")) {
	    /* copy of the code below:  !(_Windows || OS2 || _Macintosh) */
	    if (term && term->waitforinput && paused_for_mouse){
		fprintf(stderr,"%s\n", buf);
		term->waitforinput();
	    } else {
#  if defined(WGP_CONSOLE)
		fprintf(stderr,"%s\n", buf);
		if (term && term->waitforinput)
		  while (term->waitforinput() != (int)'\r') {}; /* waiting for Enter*/
#  else /* !WGP_CONSOLE */
		if (!Pause(buf)) 
		bail_to_command_line();
#  endif
	    }
	} else
# endif /* _Windows && WXWIDGETS */
	{
	    if (paused_for_mouse && !GraphHasWindow(graphwin)) {
		if (interactive) { /* cannot wait for Enter in a non-interactive session without the graph window */
		    if (buf) fprintf(stderr,"%s\n", buf);
		    fgets(buf, sizeof(buf), stdin); /* graphical window not yet initialized, wait for any key here */
		}
	    } else { /* pausing via graphical windows */
		int tmp = paused_for_mouse;
		if (buf && paused_for_mouse) fprintf(stderr,"%s\n", buf);
		if (!tmp) {
#  if defined(WGP_CONSOLE)
		    fprintf(stderr,"%s\n", buf);
			if (term && term->waitforinput)
		      while (term->waitforinput() != (int)'\r') {}; /* waiting for Enter*/
#  else
		    if (!Pause(buf)) 
		       bail_to_command_line();
#  endif
		} else {
		    if (!Pause(buf)) 
		      if (!GraphHasWindow(graphwin)) 
		        bail_to_command_line();
		}
	    }
	}
#elif defined(OS2)
	if (strcmp(term->name, "pm") == 0 && sleep_time < 0) {
	    int rc;
	    if ((rc = PM_pause(buf)) == 0) {
		/* if (!CallFromRexx)
		 * would help to stop REXX programs w/o raising an error message
		 * in RexxInterface() ...
		 */
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
#else /* !(_Windows || OS2 || _Macintosh) */
#ifdef USE_MOUSE
	if (term && term->waitforinput) {
	    /* term->waitforinput() will return,
	     * if CR was hit */
	    term->waitforinput();
	} else
#endif /* USE_MOUSE */
	(void) fgets(buf, sizeof(buf), stdin);

#endif /* !(_Windows || OS2 || _Macintosh) */
    }
    if (sleep_time > 0)
	GP_SLEEP(sleep_time);

    if (text != 0 && sleep_time >= 0)
	fputc('\n', stderr);
    screen_ok = FALSE;

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
    add_udv_by_name("MOUSE_X")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_Y")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_X2")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_Y2")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_BUTTON")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_SHIFT")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_ALT")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_CTRL")->udv_undef = TRUE;
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
	restrict_popen();
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
    if (print_out==stdout)
	return "<stdout>";
    if (!print_out || print_out==stderr || !print_out_name)
	return "<stderr>";
    return print_out_name;
}

/* process the 'print' command */
void
print_command()
{
    struct value a;
    /* space printed between two expressions only */
    int need_space = 0;

    if (!print_out) {
	print_out = stderr;
    }
    screen_ok = FALSE;
    do {
	++c_token;
	const_express(&a);
	if (a.type == STRING) {
	    fputs(a.v.string_val, print_out);
	    gpfree_string(&a);
	    need_space = 0;
	} else {
	    if (need_space)
		putc(' ', print_out);
	    disp_value(print_out, &a, FALSE);
	    need_space = 1;
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


#ifdef VOLATILE_REFRESH
/* EAM April 2007
 * The "refresh" command replots the previous graph without going back to read
 * the original data. This allows zooming or other operations on data that was
 * only transiently available in the input stream.
 */
void
refresh_command()
{
    c_token++;
    refresh_request();
}

void
refresh_request()
{
    FPRINTF((stderr,"refresh_request\n"));

    if ((first_plot == NULL && refresh_ok == 2)
    ||  (first_3dplot == NULL && refresh_ok == 3))
	int_error(NO_CARET, "no active plot; cannot refresh");

    if (refresh_ok == 0) {
	int_warn(NO_CARET, "cannot refresh from this state. trying full replot");
	replotrequest();
	return;
    }

    /* If the state has been reset to autoscale since the last plot,
     * initialize the axis limits.
     */
    AXIS_INIT2D_REFRESH(FIRST_X_AXIS,TRUE);
    AXIS_INIT2D_REFRESH(FIRST_Y_AXIS,TRUE);
    AXIS_INIT2D_REFRESH(SECOND_X_AXIS,TRUE);
    AXIS_INIT2D_REFRESH(SECOND_Y_AXIS,TRUE);

    AXIS_UPDATE2D_REFRESH(T_AXIS);  /* Untested: T and R want INIT2D or UPDATE2D?? */
    AXIS_UPDATE2D_REFRESH(POLAR_AXIS);

    AXIS_UPDATE2D_REFRESH(FIRST_Z_AXIS);
    AXIS_UPDATE2D_REFRESH(COLOR_AXIS);

    if (refresh_ok == 2)
	refresh_bounds(first_plot, refresh_nplots);
    else if (refresh_ok == 3)
	refresh_3dbounds(first_3dplot, refresh_nplots);

/* FIXME EAM
 * The existing mechanism defined in axis.h does not work here for some reason.
 * The following defined check works empirically.  Most of the time.  Maybe.
 */
#undef CHECK_REVERSE
#define CHECK_REVERSE(axis) do {		\
    AXIS *ax = axis_array + axis;		\
    if ((ax->range_flags & RANGE_REVERSE)) {	\
	double temp = ax->max;			\
	ax->max = ax->min; ax->min = temp;	\
    } } while (0)


    CHECK_REVERSE(FIRST_X_AXIS);
    CHECK_REVERSE(FIRST_Y_AXIS);
    CHECK_REVERSE(SECOND_X_AXIS);
    CHECK_REVERSE(SECOND_Y_AXIS);
#undef CHECK_REVERSE

    if (refresh_ok == 2)
	do_plot(first_plot, refresh_nplots);
    else if (refresh_ok == 3)
	do_3dplot(first_3dplot, refresh_nplots, 0);
    else
	int_error(NO_CARET, "Internal error - refresh of unknown plot type");

}


#endif
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
	bail_to_command_line(); /* be silent --- don't mess the screen */
    }
    if (!term) /* unknown terminal */
	int_error(c_token, "use 'set term' to set terminal type first");

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
    if (save_file[0]=='|') {
	restrict_popen();
	fp = popen(save_file+1,"w");
    } else
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
    add_udv_by_name("MOUSE_X")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_Y")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_X2")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_Y2")->udv_undef = TRUE;
    add_udv_by_name("MOUSE_BUTTON")->udv_undef = TRUE;
#endif
    plot3drequest();
    SET_CURSOR_ARROW;
}

/* process the 'stats' command */
void
stats_command()
{
#ifdef USE_STATS
    statsrequest();
#else
    int_error(NO_CARET,"This copy of gnuplot was not configured with support for the stats command");
#endif
}

/* process the 'system' command */
void
system_command()
{
    char *cmd;
    ++c_token;
    cmd = try_to_get_string();
    do_system(cmd);
    free(cmd);
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
    enum {test_palette_colors = 256};

    double gray, z[test_palette_colors];
    rgb_color rgb1[test_palette_colors];
    int i, k;
    static const char pre1[] = "\
reset;set multi;\
uns border;uns key;set tic in;uns xtics;uns ytics;\
se cbtic 0,0.1,1 mirr format '';\
se xr[0:1];se yr[0:1];se zr[0:1];se cbr[0:1];\
se pm3d map;set colorbox hor user orig 0.05,0.02 size 0.925,0.12;";
    static const char pre2[] = "splot 1/0;\n\n\n";
	/* note: those \n's are because of x11 terminal problems with blocking pipe */
    static const char pre3[] = "\
uns pm3d;\
se lmarg scre 0.05;se rmarg scre 0.975; se bmarg scre 0.22; se tmarg scre 0.86;\
se grid;se tics scale 0; se xtics 0,0.1;se ytics 0,0.1;\
se key top right at scre 0.975,0.975 horizontal \
title 'R,G,B profiles of the current color palette';";
    static const char post[] = "\
\n\n\nuns multi;\n"; /* no final 'reset' in favour of mousing */
    int can_pm3d = (term->make_palette && term->set_color);
    char *order = "rgb";
    char *save_replot_line;
    TBOOLEAN save_is_3d_plot;
    FILE *f = tmpfile();

#if defined(_MSC_VER) || defined(__MINGW32__)
    /* On Vista/Windows 7 tmpfile() fails. */
    if (!f) {
	char  buf[PATH_MAX];
	GetTempPath(sizeof(buf), buf);
	strcat(buf, "gnuplot-pal.tmp");
	f = fopen(buf, "w+");
    }
#endif

    c_token++;
    /* parse optional option */
    if (!END_OF_COMMAND) {
	int err = (token[c_token].length != 3);

	order = gp_input_line + token[c_token].start_index;
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
	    fputs("red'w l lt 1 lc rgb 'red'", f);
	    break;
	case 'g':
	    fputs("green'w l lt 2 lc rgb 'green'", f);
	    break;
	case 'b':
	    fputs("blue'w l lt 3 lc rgb 'blue'", f);
	    break;
	} /* switch(order[i]) */
    } /* for (i) */
    fputs(", '-' w l title 'NTSC' lt -1", f);
    fputs("\n", f);
    for (i = 0; i < 3; i++) {
	int c = order[i];

	for (k = 0; k < test_palette_colors; k++) {
	    double rgb = (c=='r')
		? rgb1[k].r :
		((c=='g') ? rgb1[k].g : rgb1[k].b);

	    fprintf(f, "%0.4f\t%0.4f\n", z[k], rgb);
	}
	fputs("e\n", f);
    }
    for (k = 0; k < test_palette_colors; k++) {
	double ntsc = 0.299 * rgb1[k].r
		    + 0.587 * rgb1[k].g
		    + 0.114 * rgb1[k].b;
	fprintf(f, "%0.4f\t%0.4f\n", z[k], ntsc);
    }
    fputs("e\n", f);
    fputs(post, f);

    /* save current gnuplot 'set' status because of the tricky sets 
     * for our temporary testing plot.
     */
    save_set(f);

    /* execute all commands from the temporary file */
    rewind(f);
    load_file(f, NULL, FALSE); /* note: it does fclose(f) */

    /* enable reset_palette() and restore replot line */
    enable_reset_palette = 1;
    free(replot_line);
    replot_line = save_replot_line;
    is_3d_plot = save_is_3d_plot;

    /* further, gp_input_line[] and token[] now destroyed! */
    c_token = num_tokens = 0;
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
    double usec;

    /* given a format and a time string, exercise the time code */

    if (isstring(++c_token)) {
	m_quote_capture(&format, c_token, c_token);
	if (isstring(++c_token)) {
	    m_quote_capture(&string, c_token, c_token);
	    memset(&tm, 0, sizeof(tm));
	    gstrptime(string, format, &tm, &usec);
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
    if (!term) /* unknown terminal */
	int_error(c_token, "use 'set term' to set terminal type first");
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
	    int_error(c_token, "only keywords are 'terminal' and 'palette'");
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
    if (!END_OF_COMMAND && !(npfname = try_to_get_string()))
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
      rc = ExecuteMacro(gp_input_line + token[c_token].start_index,
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
#if defined(MSDOS)
    /* first deal with drive letter */

    if (isalpha(path[0]) && (path[1] == ':')) {
	int driveno = toupper(path[0]) - 'A';	/* 0=A, 1=B, ... */

# if (defined(MSDOS) && defined(__EMX__)) || defined(__MSC__)
	(void) _chdrive(driveno + 1);
# endif

# ifdef DJGPP
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
#endif /* MSDOS etc. */
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
	char *rest_args = &gp_input_line[token[c_token].start_index];
	size_t replot_len = strlen(replot_line);
	size_t rest_len = strlen(rest_args);

	/* preserve commands following 'replot ;' */
	/* move rest of input line to the start
	 * necessary because of realloc() in extend_input_line() */
	memmove(gp_input_line,rest_args,rest_len+1);
	/* reallocs if necessary */
	while (gp_input_line_len < replot_len+rest_len+1)
	    extend_input_line();
	/* move old rest args off begin of input line to
	 * make space for replot_line */
	memmove(gp_input_line+replot_len,gp_input_line,rest_len+1);
	/* copy previous plot command to start of input line */
	memcpy(gp_input_line, replot_line, replot_len);
    } else {
	char *replot_args = NULL;	/* else m_capture will free it */
	int last_token = num_tokens - 1;

	/* length = length of old part + length of new part + ", " + \0 */
	size_t newlen = strlen(replot_line) + token[last_token].start_index +
	token[last_token].length - token[c_token].start_index + 3;

	m_capture(&replot_args, c_token, last_token);	/* might be empty */
	while (gp_input_line_len < newlen)
	    extend_input_line();
	strcpy(gp_input_line, replot_line);
	strcat(gp_input_line, ", ");
	strcat(gp_input_line, replot_args);
	free(replot_args);
    }
    plot_token = 0;		/* whole line to be saved as replot line */
    refresh_ok = 0;		/* start of replot will destory existing data */

    screen_ok = FALSE;
    num_tokens = scanner(&gp_input_line, &gp_input_line_len);
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
read_line(const char *prompt, int start)
{
    int more;
    char expand_prompt[40];

    current_prompt = prompt;	/* HBB NEW 20040727 */

    prompt_desc.dsc$w_length = strlen(prompt);
    prompt_desc.dsc$a_pointer = (char *) prompt;
    strcpy(expand_prompt, "_");
    strncat(expand_prompt, prompt, 38);
    do {
	line_desc.dsc$w_length = MAX_LINE_LEN - start;
	line_desc.dsc$a_pointer = &gp_input_line[start];
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
	gp_input_line[start] = NUL;
	inline_num++;
	if (gp_input_line[start - 1] == '\\') {
	    /* Allow for a continuation line. */
	    prompt_desc.dsc$w_length = strlen(expand_prompt);
	    prompt_desc.dsc$a_pointer = expand_prompt;
	    more = 1;
	    --start;
	} else {
	    line_desc.dsc$w_length = strlen(gp_input_line);
	    line_desc.dsc$a_pointer = gp_input_line;
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

    /* gp_input_line is filled by read_line or load_file, but
     * line_desc length is set only by read_line; adjust now
     */
    line_desc.dsc$w_length = strlen(cmd);
    line_desc.dsc$a_pointer = (char *) cmd;

    if ((vaxc$errno = lib$spawn(&line_desc)) != SS$_NORMAL)
	os_error(NO_CARET, "spawn error");

    (void) putc('\n', stderr);

}
#endif /* VMS */


#ifdef NO_GIH
#if defined(_Windows)
void
help_command()
{
    HWND parent;

    c_token++;
    parent = GetDesktopWindow();
#ifdef WITH_HTML_HELP
    /* open help file if necessary */
    help_window = HtmlHelp(parent, winhelpname, HH_GET_WIN_HANDLE, (DWORD_PTR)NULL);
    if (help_window == NULL) {
        help_window = HtmlHelp(parent, winhelpname, HH_DISPLAY_TOPIC, (DWORD_PTR)NULL);
        if (help_window == NULL) {
            fprintf(stderr, "Error: Could not open help file \"%s\"\n", winhelpname);
            return;
        }
    }
    if (END_OF_COMMAND) {
        /* show table of contents */
        HtmlHelp(parent, winhelpname, HH_DISPLAY_TOC, (DWORD_PTR)NULL);
    } else {
        /* lookup topic in index */
        HH_AKLINK link;
        char buf[128];
        int start = c_token;
        while (!(END_OF_COMMAND))
            c_token++;
        capture(buf, start, c_token - 1, 128);
        link.cbStruct =     sizeof(HH_AKLINK) ;
        link.fReserved =    FALSE;
        link.pszKeywords =  buf; 
        link.pszUrl =       NULL; 
        link.pszMsgText =   NULL; 
        link.pszMsgTitle =  NULL; 
        link.pszWindow =    NULL;
        link.fIndexOnFail = TRUE;
        HtmlHelp(parent, winhelpname, HH_KEYWORD_LOOKUP, (DWORD_PTR)&link);
    }
#else
    if (END_OF_COMMAND)
	WinHelp(parent, (LPSTR) winhelpname, HELP_INDEX, (DWORD) NULL);
    else {
	char buf[128];
	int start = c_token;
	while (!(END_OF_COMMAND))
	    c_token++;
	capture(buf, start, c_token - 1, 128);
	WinHelp(parent, (LPSTR) winhelpname, HELP_PARTIALKEY, (DWORD) buf);
    }
#endif
}
#else  /* !_Windows */
void
help_command()
{
    while (!(END_OF_COMMAND))
	c_token++;
    fputs("This gnuplot was not built with inline help\n", stderr);
}
#endif /* _Windows */
#endif /* NO_GIH */


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

    if ((help_ptr = getenv("GNUHELP")) == (char *) NULL)
# ifndef SHELFIND
	/* if can't find environment variable then just use HELPFILE */

/* patch by David J. Liu for getting GNUHELP from home directory */
#  ifdef __DJGPP__
	help_ptr = HelpFile;
#  else
	help_ptr = HELPFILE;
#  endif
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
		    read_line(prompt, 0);
		    num_tokens = scanner(&gp_input_line, &gp_input_line_len);
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
# if defined(_Windows) && defined(USE_OWN_WINSYSTEM_FUNCTION)
    if (!cmd)
	return;
    restrict_popen();
    winsystem(cmd);
# else /* _Windows) */
/* (am, 19980929)
 * OS/2 related note: cmd.exe returns 255 if called w/o argument.
 * i.e. calling a shell by "!" will always end with an error message.
 * A workaround has to include checking for EMX,OS/2, two environment
 *  variables,...
 */
    if (!cmd)
	return;
    restrict_popen();
    system(cmd);
# endif /* !(_Windows) */
}


# if defined(READLINE) || defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
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
#  if defined(HAVE_LIBREADLINE)
	    int found;
	    /* Initialize readline history functions */
	    using_history();

	    /* search in the history for entries containing line.
	     * They may have other tokens before and after line, hence
	     * the check on strcmp below. */
	    found = history_search(line, -1);
	    if (found != -1 && !strcmp(current_history()->line,line)) {
		/* this line is already in the history, remove the earlier entry */
		HIST_ENTRY *removed = remove_history(where_history());
		/* according to history docs we are supposed to free the stuff */
		if (removed) {
		    free(removed->line);
		    free(removed->data);
		    free(removed);
		}
	    }
	    add_history(line);
#  elif defined(HAVE_LIBEDITLINE)
	    /* deleting history entries does not work, so suppress adjacent 
	    duplicates only */
	    while (previous_history());
	    if (strcmp(current_history()->line, line) != 0)
		add_history(line);
#  else /* builtin readline */
	    add_history(line);
#  endif
	}
    }
    if (line) {
	/* s will be NUL-terminated here */
	safe_strncpy(s, line + leftover, n);
	leftover += strlen(s);
	if (line[leftover] == NUL)
	    leftover = -1;
	return s;
    }
    return NULL;
}
# endif				/* READLINE || HAVE_LIBREADLINE || HAVE_LIBEDITLINE */


# if defined(MSDOS) || defined(_Windows)
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

# if !defined(READLINE) && !defined(HAVE_LIBREADLINE) && !defined(HAVE_LIBEDITLINE)
#  if defined(MSDOS) && !defined(_Windows) && !defined(__EMX__) && !defined(DJGPP)

/* if interactive use console IO so CED will work */

#define PUT_STRING(s) cputs(s)
#define GET_STRING(s,l) ((interactive) ? cgets_emu(s,l) : fgets(s,l,stdin))


/* emulate a fgets like input function with DOS cgets */
char *
cgets_emu(char *str, int len)
{
    static char buffer[128] = "";
    static int leftover = 0;

    if (buffer[leftover] == NUL) {
	buffer[0] = 126;
	cgets(buffer);
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
# endif				/* !READLINE && !HAVE_LIBREADLINE && !HAVE_LIBEDITLINE */

/* this function is called for non-interactive operation. Its usage is
 * like fgets(), but additionally it checks for ipc events from the
 * terminals waitforinput() (if USE_MOUSE, and terminal is
 * mouseable). This function will be used when reading from a pipe.
 * fgets() reads in at most one less than size characters from stream and
 * stores them into the buffer pointed to by s.
 * Reading stops after an EOF or a newline.  If a newline is read, it is
 * stored into the buffer.  A '\0' is stored  after the last character in
 * the buffer. */
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
	for (i=0; i < len-1; i++) {
	    c = term->waitforinput();
	    if ('\n' == c) {
		dest[i] = '\n';
		i++;
		break;
	    } else if (EOF == c) {
		dest[i] = '\0';
		return (char*) 0;
	    } else {
		dest[i] = c;
	    }
	}
	dest[i] = '\0';
	return dest;
    } else
#endif
	return fgets(dest, len, stdin);
}

/* get a line from stdin, and display a prompt if interactive */
static char*
gp_get_string(char * buffer, size_t len, const char * prompt)
{
# if defined(READLINE) || defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
    if (interactive)
	return rlgets(buffer, len, prompt);
    else
	return fgets_ipc(buffer, len);
# else
    if (interactive)
	PUT_STRING(prompt);

    return GET_STRING(buffer, len);
# endif
}

/* Non-VMS version */
static int
read_line(const char *prompt, int start)
{
    TBOOLEAN more = FALSE;
    int last = 0;

    current_prompt = prompt;	/* HBB NEW 20040727 */

    do {
	/* grab some input */
	if (gp_get_string(gp_input_line + start, gp_input_line_len - start,
                         ((more) ? ">" : prompt))
	    == (char *) NULL)
	{
	    /* end-of-file */
	    if (interactive)
		(void) putc('\n', stderr);
	    gp_input_line[start] = NUL;
	    inline_num++;
	    if (start > 0)	/* don't quit yet - process what we have */
		more = FALSE;
	    else
		return (1);	/* exit gnuplot */
	} else {
	    /* normal line input */
	    /* gp_input_line must be NUL-terminated for strlen not to pass the
	     * the bounds of this array */
	    last = strlen(gp_input_line) - 1;
	    if (last >= 0) {
		if (gp_input_line[last] == '\n') {	/* remove any newline */
		    gp_input_line[last] = NUL;
		    if (last > 0 && gp_input_line[last-1] == '\r') {
		        gp_input_line[--last] = NUL;
		    }
		    /* Watch out that we don't backup beyond 0 (1-1-1) */
		    if (last > 0)
			--last;
		} else if (last + 2 >= gp_input_line_len) {
		    extend_input_line();
		    /* read rest of line, don't print "> " */
		    start = last + 1;
		    more = TRUE;
		    continue;
		    /* else fall through to continuation handling */
		} /* if(grow buffer?) */
		if (gp_input_line[last] == '\\') {
		    /* line continuation */
		    start = last;
		    more = TRUE;
		} else
		    more = FALSE;
	    } else
		more = FALSE;
	}
    } while (more);
    return (0);
}

#endif /* !VMS */

#if defined(_Windows)
# if defined(USE_OWN_WINSYSTEM_FUNCTION)
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
    char envbuf[81];
    GetEnvironmentVariable("COMSPEC", envbuf, 80);
    if (*envbuf == NUL)
	comspec = "\\command.com";
    else
	comspec = envbuf;
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
# endif /* USE_OWN_WINSYSTEM_FUNCTION */

void
call_kill_pending_Pause_dialog()
{
    kill_pending_Pause_dialog();
}
#endif /* _Windows */

#ifdef GP_MACROS
/*
 * Walk through the input line looking for string variables preceded by @.
 * Replace the characters @<varname> with the contents of the string.
 * Anything inside quotes is not expanded.
 */

#define COPY_CHAR gp_input_line[o++] = *c; \
                  after_backslash = FALSE;
int
string_expand_macros()
{
    TBOOLEAN in_squote = FALSE;
    TBOOLEAN in_dquote = FALSE;
    TBOOLEAN after_backslash = FALSE;
    TBOOLEAN in_comment= FALSE;
    int   len;
    int   o = 0;
    int   nfound = 0;
    char *c;
    char *temp_string;
    char  temp_char;
    char *m;
    struct udvt_entry *udv;

    /* Most lines have no macros */
    if (!strchr(gp_input_line,'@'))
	return(0);

    temp_string = gp_alloc(gp_input_line_len,"string variable");
    len = strlen(gp_input_line);
    if (len >= gp_input_line_len) len = gp_input_line_len-1;
    strncpy(temp_string,gp_input_line,len);
    temp_string[len] = '\0';

    for (c=temp_string; len && c && *c; c++, len--) {
	switch (*c) {
	case '@':	/* The only tricky bit */
		if (!in_squote && !in_dquote && !in_comment && isalpha(c[1])) {
		    /* Isolate the udv key as a null-terminated substring */
		    m = ++c;
		    while (isalnum(*c) || (*c=='_')) c++;
		    temp_char = *c; *c = '\0';
		    /* Look up the key and restore the original following char */
		    udv = add_udv_by_name(m);
		    if (udv && udv->udv_value.type == STRING) {
			nfound++;
			m = udv->udv_value.v.string_val;
			FPRINTF((stderr,"Replacing @%s with \"%s\"\n",udv->udv_name,m));
			while (strlen(m) + o + len > gp_input_line_len)
			    extend_input_line();
			while (*m)
			    gp_input_line[o++] = (*m++);
		    } else {
			int_warn( NO_CARET, "%s is not a string variable",m);
		    }
		    *c-- = temp_char;
		} else
		    COPY_CHAR;
		break;

	case '"':	
                if (!after_backslash)
		    in_dquote = !in_dquote;
		COPY_CHAR; break;
	case '\'':	
		in_squote = !in_squote;
		COPY_CHAR; break;
        case '\\':
                if (in_dquote)
                    after_backslash = !after_backslash;
                gp_input_line[o++] = *c; break;
	case '#':
		if (!in_squote && !in_dquote)
		    in_comment = TRUE;
	default :	
	        COPY_CHAR; break;
	}
    }
    gp_input_line[o] = '\0';
    free(temp_string);

    if (nfound)
	FPRINTF((stderr,
		 "After string substitution command line is:\n\t%s\n",
		 gp_input_line));

    return(nfound);
}
#endif

/* much more than what can be useful */
#define MAX_TOTAL_LINE_LEN (1024 * MAX_LINE_LEN)

int
do_system_func(const char *cmd, char **output)
{

#if defined(VMS) || defined(PIPES)
    int c;
    FILE *f;
    size_t cmd_len;
    int result_allocated, result_pos;
    char* result;
    int ierr = 0;
# if defined(VMS)
    int chan, one = 1;
    struct dsc$descriptor_s pgmdsc = {0, DSC$K_DTYPE_T, DSC$K_CLASS_S, 0};
    static $DESCRIPTOR(lognamedsc, "PLOT$MAILBOX");
# endif /* VMS */

    cmd_len = strlen(cmd);

    /* open stream */
# ifdef VMS
    pgmdsc.dsc$a_pointer = cmd;
    pgmdsc.dsc$w_length = cmd_len;
    if (!((vaxc$errno = sys$crembx(0, &chan, 0, 0, 0, 0, &lognamedsc)) & 1))
	os_error(NO_CARET, "sys$crembx failed");

    if (!((vaxc$errno = lib$spawn(&pgmdsc, 0, &lognamedsc, &one)) & 1))
	os_error(NO_CARET, "lib$spawn failed");

    if ((f = fopen("PLOT$MAILBOX", "r")) == NULL)
	os_error(NO_CARET, "mailbox open failed");
# else	/* everyone else */
    restrict_popen();
    if ((f = popen(cmd, "r")) == NULL)
	os_error(NO_CARET, "popen failed");
# endif	/* everyone else */

    /* get output */
    result_pos = 0;
    result_allocated = MAX_LINE_LEN;
    result = gp_alloc(MAX_LINE_LEN, "do_system_func");
    result[0] = NUL;
    while (1) {
	if ((c = getc(f)) == EOF)
	    break;
	/* result <- c */
	result[result_pos++] = c;
	if ( result_pos == result_allocated ) {
	    if ( result_pos >= MAX_TOTAL_LINE_LEN ) {
		result_pos--;
		int_warn(NO_CARET,
			 "*very* long system call output has been truncated");
		break;
	    } else {
		result = gp_realloc(result, result_allocated + MAX_LINE_LEN,
				    "extend in do_system_func");
		result_allocated += MAX_LINE_LEN;
	    }
	}
    }
    result[result_pos] = NUL;

    /* close stream */
    ierr = pclose(f);

    result = gp_realloc(result, strlen(result)+1, "do_system_func");
    *output = result;
    return ierr;

#else /* VMS || PIPES */

    int_warn(NO_CARET, "system evaluation not supported by %s", OS);
    *output = gp_strdup("");
    return 0;

#endif /* VMS || PIPES */

}
