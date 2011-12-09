#ifndef lint
static char *RCSid() { return RCSid("$Id: plot.c,v 1.128 2011/09/04 11:08:33 markisch Exp $"); }
#endif

/* GNUPLOT - plot.c */

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

#include "syscfg.h"
#include "plot.h"

#include "alloc.h"
#include "command.h"
#include "eval.h"
#include "fit.h"
#include "gp_hist.h"
#include "misc.h"
#include "readline.h"
#include "setshow.h"
#include "term_api.h"
#include "util.h"
#include "variable.h"
#include "version.h"

#include <signal.h>
#include <setjmp.h>

#ifdef OS2 /* os2.h required for gpexecute.h */
# define INCL_DOS
# define INCL_REXXSAA
# ifdef OS2_IPC
#  define INCL_DOSSEMAPHORES
# endif
# include <os2.h>
#endif /* OS2 */

#ifdef USE_MOUSE
# include "mouse.h" /* for mouse_setting */
# include "gpexecute.h"
#endif

/* on OS/2 this is needed even without USE_MOUSE */
#if defined(OS2_IPC) && !defined(USE_MOUSE)
# include "gpexecute.h"
#endif

/* Used nowhere else */
#ifdef HAVE_SYS_UTSNAME_H
# include <sys/utsname.h>
#endif

#if defined(MSDOS) || defined(__EMX__)
# include <io.h>
#endif

#ifdef VMS
# ifndef __GNUC__
#  include <unixio.h>
# endif
# include <smgdef.h>
# include <ssdef.h>
extern int vms_vkid;
extern smg$create_virtual_keyboard();
extern int vms_ktid;
extern smg$create_key_table();
#endif /* VMS */

#ifdef _Windows
# include <windows.h>
# ifndef SIGINT
#  define SIGINT 2		/* for MSC */
# endif
# include "win/wgnuplib.h"
# include "win/wcommon.h"
#endif /* _Windows */

/* GNU readline
 * Only required by two files directly,
 * so I don't put this into a header file. -lh
 */
#if defined(HAVE_LIBREADLINE) && !defined(MISSING_RL_TILDE_EXPANSION)
#  include <readline/tilde.h>
   extern int rl_complete_with_tilde_expansion;
#endif

/* BSD editline
*/
#ifdef HAVE_LIBEDITLINE
# include <editline/readline.h>
#endif

/* enable gnuplot history with readline */
#ifdef GNUPLOT_HISTORY
# ifndef GNUPLOT_HISTORY_FILE
#  define GNUPLOT_HISTORY_FILE "~/.gnuplot_history"
# endif
/*
 * The next variable is a pointer to the value returned from 'tilde_expand()'.
 * This function expands '~' to the user's home directory, or $HOME, with
 * UN*X, MSDOS.
 * Depending on your OS you have to make sure that the "$HOME" environment
 * variable exitsts.  You are responsible for valid values.
 */
static char *expanded_history_filename;

static void wrapper_for_write_history __PROTO((void));

#endif				/* GNUPLOT_HISTORY */

TBOOLEAN interactive = TRUE;	/* FALSE if stdin not a terminal */
static TBOOLEAN noinputfiles = TRUE; /* FALSE if there are script files */
TBOOLEAN persist_cl = FALSE; /* TRUE if -persist is parsed in the command line */

/* user home directory */
static const char *user_homedir = NULL;

/* user shell */
const char *user_shell = NULL;

#ifdef X11
extern int X11_args __PROTO((int, char **)); /* FIXME: defined in term/x11.trm */
#endif

/* patch to get home dir, see command.c */
#ifdef DJGPP
# include <dir.h>               /* MAXPATH */
char HelpFile[MAXPATH];
#endif /*   - DJL */

/* a longjmp buffer to get back to the command line */
/* FIXME HBB 20001103: should probably just use GPFAR, rather than
 * check for _Windows */
#ifdef _Windows
static JMP_BUF far command_line_env;
#else
static JMP_BUF command_line_env;
#endif

static void load_rcfile __PROTO((void));
static RETSIGTYPE inter __PROTO((int anint));
static void init_memory __PROTO((void));

static int exit_status = EXIT_SUCCESS;

#ifdef OS2
# include <process.h>
static ULONG RexxInterface(PRXSTRING, PUSHORT, PRXSTRING);
TBOOLEAN CallFromRexx = FALSE;
#endif /* OS2 */

static RETSIGTYPE
inter(int anint)
{
    (void) anint;		/* aovid -Wunused warning */
    (void) signal(SIGINT, (sigfunc) inter);
    (void) signal(SIGFPE, SIG_DFL);	/* turn off FPE trapping */

#ifdef OS2
    if (!strcmp(term->name,"pm")) {
	PM_intc_cleanup();
	/* ??
	  putc('\n', stderr);
	  LONGJMP(command_line_env, TRUE);
	 */
    } else
#endif
    {
    term_reset();
    (void) putc('\n', stderr);
    bail_to_command_line();	/* return to prompt */
    }
}

#ifdef LINUXVGA
/* utility functions to ensure that setuid gnuplot
 * assumes root privileges only for those parts
 * of the code which require root rights.
 *
 * By "Dr. Werner Fink" <werner@suse.de>
 */
static uid_t euid, ruid;
static gid_t egid, rgid;
static int asked_privi = 0;

void
drop_privilege()
{
    if (!asked_privi) {
	euid = geteuid();
	egid = getegid();
	ruid = getuid();
	rgid = getgid();
	asked_privi = 1;
    }
    if (setegid(rgid) == -1)
	(void) fprintf(stderr, "setegid(%d): %s\n",
		       (int) rgid, strerror(errno));
    if (seteuid(ruid) == -1)
	(void) fprintf(stderr, "seteuid(%d): %s\n",
		       (int) ruid, strerror(errno));
}

void
take_privilege()
{
    if (!asked_privi) {
	euid = geteuid();
	egid = getegid();
	ruid = getuid();
	rgid = getgid();
	asked_privi = 1;
    }
    if (setegid(egid) == -1)
	(void) fprintf(stderr, "setegid(%d): %s\n",
		       (int) egid, strerror(errno));
    if (seteuid(euid) == -1)
	(void) fprintf(stderr, "seteuid(%d): %s\n",
		       (int) euid, strerror(errno));
}

#endif /* LINUXVGA */

/* a wrapper for longjmp so we can keep everything local */
void
bail_to_command_line()
{
#ifdef _Windows
    call_kill_pending_Pause_dialog();
#endif
    LONGJMP(command_line_env, TRUE);
}

#if defined(_Windows) || defined(_Macintosh)
int
gnu_main(int argc, char **argv)
#else
int
main(int argc, char **argv)
#endif
{
    int i;

#ifdef LINUXVGA
    LINUX_setup();		/* setup VGA before dropping privilege DBT 4/5/99 */
    drop_privilege();
#endif
/* make sure that we really have revoked root access, this might happen if
   gnuplot is compiled without vga support but is installed suid by mistake */
#ifdef __linux__
    setuid(getuid());
#endif

#if defined(MSDOS) && !defined(_Windows) && !defined(__GNUC__)
    PC_setup();
#endif /* MSDOS !Windows */

/* HBB: Seems this isn't needed any more for DJGPP V2? */
/* HBB: disable all floating point exceptions, just keep running... */
#if defined(DJGPP) && (DJGPP!=2)
    _control87(MCW_EM, MCW_EM);
#endif

#if defined(OS2)
    int rc;
#ifdef OS2_IPC
    char semInputReadyName[40];
    sprintf( semInputReadyName, "\\SEM32\\GP%i_Input_Ready", getpid() );
    rc = DosCreateEventSem(semInputReadyName,&semInputReady,0,0);
    if (rc != 0)
      fputs("DosCreateEventSem error\n",stderr);
#endif
    rc = RexxRegisterSubcomExe("GNUPLOT", (PFN) RexxInterface, NULL);
#endif

/* malloc large blocks, otherwise problems with fragmented mem */
#ifdef MALLOCDEBUG
    malloc_debug(7);
#endif

/* get helpfile from home directory */
#ifdef __DJGPP__
    {
	char *s;
	strcpy(HelpFile, argv[0]);
	for (s = HelpFile; *s; s++)
	    if (*s == DIRSEP1)
		*s = DIRSEP2;	/* '\\' to '/' */
	strcpy(strrchr(HelpFile, DIRSEP2), "/gnuplot.gih");
    }			/* Add also some "paranoid" tests for '\\':  AP */
#endif /* DJGPP */

#ifdef VMS
    unsigned int status[2] = { 1, 0 };
#endif

#if defined(HAVE_LIBEDITLINE)
    rl_getc_function = getc_wrapper;
#endif

#if defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
    /* T.Walter 1999-06-24: 'rl_readline_name' must be this fix name.
     * It is used to parse a 'gnuplot' specific section in '~/.inputrc' 
     * or gnuplot specific commands in '.editrc' (when using editline
     * instead of readline) */
    rl_readline_name = "Gnuplot";
    rl_terminal_name = getenv("TERM");
    using_history();
#endif
#if defined(HAVE_LIBREADLINE) && !defined(MISSING_RL_TILDE_EXPANSION)
    rl_complete_with_tilde_expansion = 1;
#endif

    for (i = 1; i < argc; i++) {
	if (!argv[i])
	    continue;

	if (!strcmp(argv[i], "-V") || !strcmp(argv[i], "--version")) {
	    printf("gnuplot %s patchlevel %s\n",
		    gnuplot_version, gnuplot_patchlevel);
	    return 0;

	} else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
	    printf( "Usage: gnuplot [OPTION]... [FILE]\n"
#ifdef X11
		    "for X11 options see 'help X11->command-line-options'\n"
#endif
		    "  -V, --version\n"
		    "  -h, --help\n"
		    "  -p  --persist\n"
		    "  -e  \"command1; command2; ...\"\n"
		    "gnuplot %s patchlevel %s\n"
#ifdef DIST_CONTACT
		    "Report bugs to "DIST_CONTACT"\n"
		    "            or %s\n",
#else
		    "Report bugs to %s\n",
#endif
		    gnuplot_version, gnuplot_patchlevel, bug_email);
	    return 0;

	} else if (!strncmp(argv[i], "-persist", 2) || !strcmp(argv[i], "--persist")) {
	    persist_cl = TRUE;
	}
    }

#ifdef X11
    /* the X11 terminal removes tokens that it recognizes from argv. */
    {
	int n = X11_args(argc, argv);
	argv += n;
	argc -= n;
    }
#endif

    setbuf(stderr, (char *) NULL);

#ifdef HAVE_SETVBUF
    /* this was once setlinebuf(). Docs say this is
     * identical to setvbuf(,NULL,_IOLBF,0), but MS C
     * faults this (size out of range), so we try with
     * size of 1024 instead. [SAS/C does that, too. -lh]
     * Failing this, I propose we just make the call and
     * ignore the return : its probably not a big deal
     */
    if (setvbuf(stdout, (char *) NULL, _IOLBF, (size_t) 1024) != 0)
	(void) fputs("Could not linebuffer stdout\n", stderr);

#ifdef X11
    /* This call used to be in x11.trm, with the following comment:
     *   Multi-character inputs like escape sequences but also mouse-pasted
     *   text got buffered and therefore didn't trigger the select() function
     *   in X11_waitforinput(). Switching to unbuffered input solved this.
     *   23 Jan 2002 (joze)
     * But switching to unbuffered mode causes all characters in the input
     * buffer to be lost. So the only safe time to do it is on program entry.
     * The #ifdef X11 is probably unnecessary, but makes the change minimal.
     * Do any non-X platforms suffer from the same problem?
     * EAM - Jan 2004.
     */
    setvbuf(stdin, (char *) NULL, _IONBF, 0);
#endif

#endif

    gpoutfile = stdout;

    /* Initialize pre-loaded user variables */
    (void) Gcomplex(&udv_pi.udv_value, M_PI, 0.0);
    udv_NaN = add_udv_by_name("NaN");
    (void) Gcomplex(&(udv_NaN->udv_value), not_a_number(), 0.0);
    udv_NaN->udv_undef = FALSE;

    init_memory();

    interactive = FALSE;
    init_terminal();		/* can set term type if it likes */
    push_terminal(0);		/* remember the default terminal */

    /* reset the terminal when exiting */
    /* this is done through gp_atexit so that other terminal functions
     * can be registered to be executed before the terminal is reset. */
    GP_ATEXIT(term_reset);

# if defined(_Windows) && ! defined(WGP_CONSOLE)
    interactive = TRUE;
# else
    interactive = isatty(fileno(stdin));
# endif

    if (argc > 1)
	interactive = noinputfiles = FALSE;
    else
	noinputfiles = TRUE;

    /* Need this before show_version is called for the first time */

#ifdef HAVE_SYS_UTSNAME_H
    {
	struct utsname uts;

	/* something is fundamentally wrong if this fails ... */
	if (uname(&uts) > -1) {
# ifdef _AIX
	    strcpy(os_name, uts.sysname);
	    sprintf(os_name, "%s.%s", uts.version, uts.release);
# elif defined(SCO)
	    strcpy(os_name, "SCO");
	    strcpy(os_rel, uts.release);
# elif defined(DJGPP)
	    if (!strncmp(uts.sysname, "??Un", 4)) /* don't print ??Unknow" */
		strcpy(os_name, "Unknown");
	    else {
		strcpy(os_name, uts.sysname);
		strcpy(os_rel, uts.release);
	    }
# else
	    strcpy(os_name, uts.sysname);
	    strcpy(os_rel, uts.release);
# ifdef OS2
	    if (!strchr(os_rel,'.'))
		/* write either "2.40" or "4.0", or empty -- don't print "OS/2 1" */
		strcpy(os_rel, "");
# endif

# endif
	}
    }
#else /* ! HAVE_SYS_UTSNAME_H */

    strcpy(os_name, OS);
    strcpy(os_rel, "");

#endif /* HAVE_SYS_UTSNAME_H */

    if (interactive)
	show_version(stderr);
    else
	show_version(NULL); /* Only load GPVAL_COMPILE_OPTIONS */

#ifdef WGP_CONSOLE
#ifdef CONSOLE_SWITCH_CP
    if (cp_changed && interactive) {
	fprintf(stderr,
	    "\ngnuplot changed the codepage of this console from %i to %i to\n" \
	    "match the graph window. Some characters might only display correctly\n" \
	    "if you change the font to a non-raster type.\n", 
	    cp_input, GetConsoleCP());
    }
#else
    if ((GetConsoleCP() != GetACP()) && interactive) {
	fprintf(stderr,
	    "\nWarning: The codepage of the graph window (%i) and that of the\n" \
	    "console (%i) differ. Use `set encoding` or `!chcp` if extended\n" \
	    "characters don't display correctly.\n", 
	    GetACP(), GetConsoleCP());
    }
#endif
#endif

    update_gpval_variables(3);  /* update GPVAL_ variables available to user */

#ifdef VMS
    /* initialise screen management routines for command recall */
    if (status[1] = smg$create_virtual_keyboard(&vms_vkid) != SS$_NORMAL)
	done(status[1]);
    if (status[1] = smg$create_key_table(&vms_ktid) != SS$_NORMAL)
	done(status[1]);
#endif /* VMS */

    if (!SETJMP(command_line_env, 1)) {
	/* first time */
	interrupt_setup();
	/* should move this stuff another initialisation routine,
	 * something like init_set() maybe */
	get_user_env();
	init_loadpath();
	init_locale();
	/* HBB: make sure all variables start in the same mode 'reset'
	 * would set them to. Since the axis variables aren't in
	 * initialized arrays any more, this is now necessary... */
	reset_command();
	init_color();  /*  Initialization of color  */
	load_rcfile();
	init_fit();		/* Initialization of fitting module */

	if (interactive && term != 0) {		/* not unknown */
#ifdef GNUPLOT_HISTORY
	    FPRINTF((stderr, "Before read_history\n"));
#if defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
	    expanded_history_filename = tilde_expand(GNUPLOT_HISTORY_FILE);
#else
	    expanded_history_filename = gp_strdup(GNUPLOT_HISTORY_FILE);
	    gp_expand_tilde(&expanded_history_filename);
#endif
	    FPRINTF((stderr, "expanded_history_filename = %s\n", expanded_history_filename));
	    read_history(expanded_history_filename);
	    {
		/* BEGIN: Go local to get environment variable */
		const char *temp_env = getenv ("GNUPLOT_HISTORY_SIZE");
		if (temp_env)
		    gnuplot_history_size = strtol (temp_env, (char **) NULL, 10);
	    } /* END: Go local to get environment variable */

	    /*
	     * It is safe to ignore the return values of 'atexit()' and
	     * 'on_exit()'. In the worst case, there is no history of your
	     * currrent session and you have to type all again in your next
	     * session.
	     * This is the default behaviour (traditional reasons), too.
	     * In case you don't have one of these functions, or you don't
	     * want to use them, 'write_history()' is called directly.
	     */
	    GP_ATEXIT(wrapper_for_write_history);
#endif /* GNUPLOT_HISTORY */

	    fprintf(stderr, "\nTerminal type set to '%s'\n", term->name);
	}			/* if (interactive && term != 0) */
    } else {
	/* come back here from int_error() */
	if (interactive == FALSE)
	    exit_status = EXIT_FAILURE;
#ifdef HAVE_READLINE_RESET
	else
	{
	    /* reset properly readline after a SIGINT+longjmp */
	    rl_reset_after_signal ();
	}
#endif

	load_file_error();	/* if we were in load_file(), cleanup */
	SET_CURSOR_ARROW;

#ifdef VMS
	/* after catching interrupt */
	/* VAX stuffs up stdout on SIGINT while writing to stdout,
	   so reopen stdout. */
	if (gpoutfile == stdout) {
	    if ((stdout = freopen("SYS$OUTPUT", "w", stdout)) == NULL) {
		/* couldn't reopen it so try opening it instead */
		if ((stdout = fopen("SYS$OUTPUT", "w")) == NULL) {
		    /* don't use int_error here - causes infinite loop! */
		    fputs("Error opening SYS$OUTPUT as stdout\n", stderr);
		}
	    }
	    gpoutfile = stdout;
	}
#endif /* VMS */
	if (!interactive && !noinputfiles) {
	    term_reset();
	    exit(EXIT_FAILURE);	/* exit on non-interactive error */
	}
    }

    if (argc > 1) {
#ifdef _Windows
	TBOOLEAN noend = persist_cl;
#endif

	/* load filenames given as arguments */
	while (--argc > 0) {
	    ++argv;
	    c_token = 0;
#ifdef _Windows
	    if (stricmp(*argv, "-noend") == 0 || stricmp(*argv, "/noend") == 0
	       	|| stricmp(*argv, "-persist") == 0)
		noend = TRUE;
	    else
#endif
	    if (!strncmp(*argv, "-persist", 2) || !strcmp(*argv, "--persist")) {
		FPRINTF((stderr,"'persist' command line option recognized\n"));

	    } else if (strcmp(*argv, "-") == 0) {
		interactive = TRUE;
		while (!com_line());
		interactive = FALSE;

	    } else if (strcmp(*argv, "-e") == 0) {
		--argc; ++argv;
		if (argc <= 0) {
		    fprintf(stderr, "syntax:  gnuplot -e \"commands\"\n");
		    return 0;
		}
		do_string(*argv);

	    } else {
		load_file(loadpath_fopen(*argv, "r"), gp_strdup(*argv), FALSE);
	    }
	}
#ifdef _Windows
	if (noend) {
	    interactive = TRUE;
	    while (!com_line());
	}
#endif
    } else {
	/* take commands from stdin */
	while (!com_line());
    }

#if (defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)) && defined(GNUPLOT_HISTORY)
#if !defined(HAVE_ATEXIT) && !defined(HAVE_ON_EXIT)
    /* You should be here if you neither have 'atexit()' nor 'on_exit()' */
    wrapper_for_write_history();
#endif /* !HAVE_ATEXIT && !HAVE_ON_EXIT */
#endif /* (HAVE_LIBREADLINE || HAVE_LIBEDITLINE) && GNUPLOT_HISTORY */

#ifdef OS2
    RexxDeregisterSubcom("GNUPLOT", NULL);
#endif

    /* HBB 20040223: Not all compilers like exit() to end main() */
    /* exit(exit_status); */
    return exit_status;
}


/* Set up to catch interrupts */
void
interrupt_setup()
{
#if defined(WGP_CONSOLE)
    /* FIXME. CTRC+C crashes console mode gnuplot for windows.
       Failure of longjmp() is not easy to fix so that the signal
       of SIGINT is just ignored at the moment.
    */
    (void) signal(SIGINT, SIG_IGN);
#else
    (void) signal(SIGINT, (sigfunc) inter);
#endif

#ifdef SIGPIPE
    /* ignore pipe errors, this might happen with set output "|head" */
    (void) signal(SIGPIPE, SIG_IGN);
#endif /* SIGPIPE */
}


/* Look for a gnuplot init file in current or home directory */
static void
load_rcfile()
{
    FILE *plotrc = NULL;
    char *rcfile = NULL;

#ifdef USE_CWDRC
    /* Allow check for a .gnuplot init file in the current directory */
    /* This is a security risk, as someone might leave a malicious   */
    /* init file in a shared directory.                              */
    plotrc = fopen(PLOTRC, "r");
#endif /* !USE_CWDRC */

    if (plotrc == NULL) {
	if (user_homedir) {
	    /* len of homedir + directory separator + len of file name + \0 */
	    rcfile = (char *) gp_alloc((user_homedir ? strlen(user_homedir) : 0) + 1 + strlen(PLOTRC) + 1, "rcfile");
	    strcpy(rcfile, user_homedir);
	    PATH_CONCAT(rcfile, PLOTRC);
	    plotrc = fopen(rcfile, "r");
	}
    }
    if (plotrc) {
	char *rc = gp_strdup(rcfile ? rcfile : PLOTRC);
	load_file(plotrc, rc, FALSE);
	push_terminal(0); /* needed if terminal or its options were changed */
    }

    free(rcfile);
}

void
get_user_env()
{
    if (user_homedir == NULL) {
	const char *env_home;

	if ((env_home = getenv(HOME))
#ifdef WIN32
	    || (env_home = appdata_directory())
	    || (env_home = getenv("USERPROFILE"))
#endif
#ifndef VMS
	    || (env_home = getenv("HOME"))
#endif
	   )
	    user_homedir = (const char *) gp_strdup(env_home);
	else if (interactive)
	    int_warn(NO_CARET, "no HOME found");
    }
    /* Hhm ... what about VMS? */
    if (user_shell == NULL) {
	const char *env_shell;

	if ((env_shell = getenv("SHELL")) == NULL)
#if defined(MSDOS) || defined(_Windows) || defined(OS2)
	    if ((env_shell = getenv("COMSPEC")) == NULL)
#endif
		env_shell = SHELL;

	user_shell = (const char *) gp_strdup(env_shell);
    }
}

/* expand tilde in path
 * path cannot be a static array!
 * tilde must be the first character in *pathp;
 * we may change that later
 */
void
gp_expand_tilde(char **pathp)
{
    if (!*pathp)
	int_error(NO_CARET, "Cannot expand empty path");

    if ((*pathp)[0] == '~' && (*pathp)[1] == DIRSEP1) {
	if (user_homedir) {
	    size_t n = strlen(*pathp);

	    *pathp = gp_realloc(*pathp, n + strlen(user_homedir), "tilde expansion");
	    /* include null at the end ... */
	    memmove(*pathp + strlen(user_homedir) - 1, *pathp, n + 1);
	    memcpy(*pathp, user_homedir, strlen(user_homedir));
	} else
	    int_warn(NO_CARET, "HOME not set - cannot expand tilde");
    }
}


static void
init_memory()
{
    extend_input_line();
    extend_token_table();
    replot_line = gp_alloc(1, "string");
    *replot_line = NUL;
}


#ifdef OS2

int
ExecuteMacro(char *argv, int namelength)
{
    RXSTRING rxRc;
    RXSTRING rxArg[2];
    int rxArgCount = 0;
    char pszName[CCHMAXPATH];
    char *rxArgStr;
    short sRc;
    long rc;

    if (namelength >= sizeof(pszName))
	return 1;
    /* FIXME HBB 20010121: 3rd argument doesn't make sense. Either
     * this should be sizeof(pszName), or it shouldn't use
     * safe_strncpy(), here */
    safe_strncpy(pszName, argv, namelength + 1);
    rxArgStr = &argv[namelength];
    RXSTRPTR(rxRc) = NULL;

#if 0
    /*
       C-like calling of function: program name is first
       parameter.
       In REXX you would have to use
       Parse Arg param0, param1
       to get the program name in param0 and the arguments in param1.

       Some versions before gnuplot 3.7pl1 used a similar approach but
       passed program name and arguments in a single string:
       (==> Parse Arg param0 param1)
     */

    MAKERXSTRING(rxArg[0], pszName, strlen(pszName));
    rxArgCount++;
    if (*rxArgStr) {
	MAKERXSTRING(rxArg[1], rxArgStr, strlen(rxArgStr));
	rxArgCount++;
    }
#else
    /*
       REXX standard calling (gnuplot 3.7pl1 and above):
       The program name is not supplied and so all actual arguments
       are in a single string:
       Parse Arg param
       We even handle blanks like cmd.exe when calling REXX programs.
     */

    if (*rxArgStr) {
	MAKERXSTRING(rxArg[0], rxArgStr, strlen(rxArgStr));
	rxArgCount++;
    }
#endif

    CallFromRexx = TRUE;
    rc = RexxStart(
		      rxArgCount,
		      rxArg,
		      pszName,
		      NULL,
		      "GNUPLOT",
		      RXCOMMAND,
		      NULL,
		      &sRc,
		      &rxRc);
    CallFromRexx = FALSE;

   /* am: a word WRT errors codes:
      the negative ones don't seem to have symbolic names, you can get
      them from the OREXX reference, they're not in REXX Programming Guide -
      no idea where to retrieve them from a Warp 3 reference ??
      The positive ones are somehow referenced in REXXPG
   */
    if (rc < 0) {
	/* REXX error */
    } else if (rc > 0) {
	/* Interpreter couldn't be started */
	if (rc == -4)
	   /* run was cancelled, but don't give error message */
	    rc = 0;
    } else if (rc==0) {
	/* all was fine */
    }

/* We don't we try to use rxRc ?
   BTW, don't use free() instead since it's allocated inside RexxStart()
   and not in our executable using the EMX libraries */
   if (RXSTRPTR(rxRc))
       /* I guess it's NULL if something major went wrong,
	  NULL strings are usually not part of the REXX language ... */
       DosFreeMem(rxRc.strptr);

   return rc;
}

/* Rexx command line interface */
ULONG
RexxInterface(PRXSTRING rxCmd, PUSHORT pusErr, PRXSTRING rxRc)
{
    int rc;
    static JMP_BUF keepenv;
    int cmdlen;

    memcpy(keepenv, command_line_env, sizeof(JMP_BUF));
    if (!SETJMP(command_line_env, 1)) {
	/* Set variable gp_input_line.
	   Watch out for line length of NOT_ZERO_TERMINATED strings ! */
	cmdlen = rxCmd->strlength + 1;
	/* FIXME HBB 20010121: 3rd argument doesn't make sense. Either
	 * this should be gp_input_line_len, or it shouldn't use
	 * safe_strncpy(), here */
	safe_strncpy(gp_input_line, rxCmd->strptr, cmdlen);
	gp_input_line[cmdlen] = NUL;
	rc = do_line();
	*pusErr = RXSUBCOM_OK;
	rxRc->strptr[0] = rc + '0';
	rxRc->strptr[1] = NUL;
	rxRc->strlength = strlen(rxRc->strptr);
    } else {
/*
   We end up here when bail_to_command_line() is called.
   Therefore sometimes this call should be avoided when
   executing a REXX program (e.g. 'Cancel' from
   PM GUI after a 'pause -1' command)
*/
	*pusErr = RXSUBCOM_ERROR;
	RexxSetHalt(getpid(), 1);
    }
    memcpy(command_line_env, keepenv, sizeof(JMP_BUF));
    return 0;
}
#endif /* OS2 */

#ifdef GNUPLOT_HISTORY
# if defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)

static void
wrapper_for_write_history()
{
#if 1
    /* Alternative code, saves one disk access */
    if (history_is_stifled())
	unstifle_history();
    if (gnuplot_history_size >= 0)
	stifle_history (gnuplot_history_size);

    /* returns 0 on success */
    if (write_history(expanded_history_filename))
	fprintf (stderr, "Warning:  Could not write history file!!!\n");

    unstifle_history();
#else
    /* if writing was successful, truncate history
     *  to gnuplot_history_size lines
     */
    if (write_history(expanded_history_filename)) {
	if (gnuplot_history_size >= 0)
	    history_truncate_file(expanded_history_filename, gnuplot_history_size);
    }
#endif
}

# else /* HAVE_LIBREADLINE || HAVE_LIBEDITLINE */

/* version for gnuplot's own write_history */
static void
wrapper_for_write_history()
{
    /* What we really want to do is truncate(expanded_history_filename),
       but this is only available on BSD compatible systems */
    remove(expanded_history_filename);
    if (gnuplot_history_size < 0)
    	write_history(expanded_history_filename);
    else
	write_history_n(gnuplot_history_size, expanded_history_filename, "w");
}

# endif /* HAVE_LIBREADLINE || HAVE_LIBEDITLINE */
#endif /* GNUPLOT_HISTORY */

