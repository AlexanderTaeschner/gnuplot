/* GNUPLOT - readline.h */

/*[
 * Copyright 1999, 2004   Thomas Williams, Colin Kelley
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

#ifndef GNUPLOT_READLINE_H
# define GNUPLOT_READLINE_H

#include "syscfg.h"

#if defined(HAVE_LIBREADLINE)
# include "stdfn.h"	/* <readline/readline.h> needs stdio.h... */
# include <readline/readline.h>

#elif defined(HAVE_LIBEDITLINE)
# include <editline/readline.h>
# include <histedit.h>
#endif

/* Variables of readline.c needed by other modules: */

#if defined (HAVE_LIBEDITLINE)
    /* In order to multiplex terminal and mouse input via term->waitforinput()
     * we need to be able to dive into libedit's internals to read a UTF-8
     * byte sequence into an int which libedit holds as a wchar and/or some
     * complicated internal structure.  The waitforinput() routines will call
     *		el_wgetc(el, &nextchar)
     * instead of
     *		nextchar = getchar()
     */
    extern EditLine *el;
#endif

/* Work-around for editline's use of wchar_t rather than UTF-8 */
#if defined (HAVE_LIBEDITLINE)
    #define read_and_return_character() {	\
	int ierr;				\
	wchar_t nextchar;			\
	ierr = el_wgetc(el, &nextchar);		\
	return (ierr==1 ? (int)nextchar : 0);	\
    }
#else
    #define read_and_return_character() 	\
	return getchar()
#endif


/* Prototypes of functions exported by readline.c */

#if defined(READLINE)
char *readline(const char *);
void set_termio(void);
void reset_termio(void);

#elif defined(HAVE_LIBREADLINE) || defined(HAVE_LIBEDITLINE)
int getc_wrapper(FILE* fp);
#endif

#if defined(HAVE_LIBREADLINE) && defined(HAVE_READLINE_SIGNAL_HANDLER)
void wrap_readline_signal_handler(void);
#else
#define wrap_readline_signal_handler()
#endif

#endif /* GNUPLOT_READLINE_H */
