
/*
 * $Id: term.h,v 1.41 1997/07/22 23:20:50 drd Exp $
 *
 */

/* GNUPLOT - term.h */
/*
 * Copyright (C) 1986 - 1993, 1997   Thomas Williams, Colin Kelley
 *
 * Permission to use, copy, and distribute this software and its
 * documentation for any purpose with or without fee is hereby granted, 
 * provided that the above copyright notice appear in all copies and 
 * that both that copyright notice and this permission notice appear 
 * in supporting documentation.
 *
 * Permission to modify the software is granted, but not the right to
 * distribute the modified code.  Modifications are to be distributed 
 * as patches to released version.
 *  
 * This software is provided "as is" without express or implied warranty.
 * 
 *
 * AUTHORS
 * 
 *   Original Software:
 *     Thomas Williams,  Colin Kelley.
 * 
 *   Gnuplot 2.0 additions:
 *       Russell Lang, Dave Kotz, John Campbell.
 *
 *   Gnuplot 3.0 additions:
 *       Gershon Elber and many others.
 * 
 * There is a mailing list for gnuplot users. Note, however, that the
 * newsgroup 
 *	comp.graphics.apps.gnuplot 
 * is identical to the mailing list (they
 * both carry the same set of messages). We prefer that you read the
 * messages through that newsgroup, to subscribing to the mailing list.
 * (If you can read that newsgroup, and are already on the mailing list,
 * please send a message to majordomo@dartmouth.edu, asking to be
 * removed from the mailing list.)
 *
 * The address for mailing to list members is
 *	   info-gnuplot@dartmouth.edu
 * and for mailing administrative requests is 
 *	   majordomo@dartmouth.edu
 * The mailing list for bug reports is 
 *	   bug-gnuplot@dartmouth.edu
 * The list of those interested in beta-test versions is
 *	   info-gnuplot-beta@dartmouth.edu
 */

/*
 * term.h: terminal support definitions
 *   Edit this file depending on the set of terminals you wish to support.
 * Comment out the terminal types that you don't want or don't have, and
 * uncomment those that you want included. Be aware that some terminal 
 * types (eg, SUN, UNIXPLOT) will require changes in the makefile 
 * LIBS definition. 
 */

/*
 * first draft after all terminals are converted to new layout
 * Stefan Bodewig Dec. 1995
 */


/* Platform dependent part */

#if defined(AMIGA_SC_6_1) || defined(AMIGA_AC_5) || defined(__amigaos__)
#include "amiga.trm"
#endif

#ifdef LINUXVGA 
#include "linux.trm"   /* linux vga */
#endif

#ifdef OS2
#include "pm.trm"  /* presentation manager */
#ifdef EMXVESA
#include "emxvga.trm" /* works with DOS and OS/2 (windowed/full screen) */
#endif
#endif

#if defined(ATARI) || defined(MTOS)	/* ATARI-ST */
#include "atarivdi.trm"
#ifdef MTOS
#include "multitos.trm"
#endif
#include "atariaes.trm"
#endif

/******************************* MS-Dos Section **************************/
#if defined(MSDOS) && defined(__EMX__) /* MsDos with emx-gcc compiler */
#define EMXVESA           /* Vesa-Cards */
#include "emxvga.trm"
#endif

/* HBB: as an aside: I think the several / * ... / * ... * / later in this file are _bad_ style */
#ifdef DJGPP
#include "djsvga.trm"               /* MsDos with djgpp compiler */
#endif

#ifdef __ZTC__
#include "fg.trm"     /* MsDos with Zortech-C++ Compiler */
#endif

/* All other Compilers */
#ifndef _Windows     
#ifdef PC
/* uncomment the next line to include SuperVGA support */
/* #define BGI_NAME "svga256" /* the name of the SVGA.BGI for Borland C */
/* this also triggers the inclusion of Super VGA support */
#include "pc.trm"          /* all PC types except MS WINDOWS*/
#endif
#else
#include "win.trm"         /* MS-Windows */
#endif
/********************** End of MS-Dos Section ****************************/

#ifdef UNIXPC          /* AT&T Unix-PC */
#include "unixpc.trm"
#endif

#ifdef SUN             /* Sunview */
#include "sun.trm"
#endif

#ifdef IRIS          /* Iris */
#include "iris4d.trm"
#include "vws.trm"
#endif

#ifdef SCO             /* SCO CGI drivers */
#include "cgi.trm"
#endif

#ifdef APOLLO
#include "apollo.trm"  /* Apollo Graphics Primitive Resource */ 
			  /* with resizeable windows */
#include "gpr.h"       /* Apollo Graphics Primitive Resource fixed windows */ 
#endif

#ifdef NEXT
#include "next.trm"
#endif

#ifdef _Macintosh
#include "mac.trm"
#endif
/* These terminals are not relevant for MSDOS, OS2, MS-Windows, ATARI or Amiga */
#if !defined(MSDOS) && !defined(_Windows) && !defined(ATARI) && !defined(MTOS) && !defined(AMIGA_SC_6_1) && !defined(AMIGA_AC_5) && !defined(__amigaos__)
#include "aed.trm"     /* AED 512 and AED 767 */
#ifdef UNIXPLOT
#ifdef GNUGRAPH
#include "gnugraph.trm"
#else
#include "unixplot.trm"
#endif
#endif
#include "gpic.trm"    /* gpic for groff */
/* #include "mgr.trm"     /* MGR Window Manager */
#include "regis.trm"   /* regis graphics */
/* #include "rgip.trm"    /* Metafile, requires POSIX */
                       /* Redwood Graphics Interface Protocol UNIPLEX */ 
#include "t410x.trm"   /* Tektronix 4106, 4107, 4109 and 420x terminals */
#include "tek.trm"     /* a Tek 4010 and others including VT-style */
#ifdef X11
#include "x11.trm"     /* x windows */
#endif

#include "xlib.trm"
#endif /* !MSDOS && !OS2 && !_Windows && !_ATARI && !_MTOS && !AMIGA */

/* These terminals can be used on any system */

#include "ai.trm"	    /* Adobe Illustrator Format */
#include "cgm.trm"      /* Computer Graphics Metafile (eg ms office) */
#include "corel.trm"    /* CorelDraw! eps format */
/* #include "debug.trm" /* debugging terminal */
#include "dumb.trm"     /* dumb terminal */
#include "dxf.trm"	    /* DXF format for use with AutoCad (Release 10.x) */
#include "dxy.trm"      /* Roland DXY800A plotter */
#include "excl.trm"	    /* QMS/EXCL laserprinter (Talaris 1590 and others) */
#include "fig.trm"      /* fig graphics */

/* NOTE THAT GIF REQUIRES A SEPARATE LIBRARY : see term/gif.trm */
#ifdef HAVE_LIBGD /* autoconf */
#include "gif.trm"   /* GIF format. */
#endif

/* #include "grass.trm" /* geographical info system */
#include "hp26.trm"     /* HP2623A and probably others */
#include "hp2648.trm"   /* HP2647 and 2648 */
#include "hp500c.trm"   /* HP DeskJet 500 C */
#include "hpgl.trm"	    /* HP7475, HP7220 plotters, and (hopefully) lots of others */
#include "hpljii.trm"   /* HP Laserjet II */
#include "hppj.trm"     /* HP PrintJet */
#include "imagen.trm"   /* Imagen laser printers */
#include "kyo.trm"      /* Kyocera Prescribe printer */
#include "mif.trm"      /* Frame Maker MIF 3.00 format driver */
#include "pbm.trm"      /* portable bit map */

/* NOTE THAT PNG REQUIRES A SEPARATE LIBRARY : see term/png.trm */
#ifdef HAVE_LIBPNG /* autoconf */
#include "png.trm"   /* png */
#endif

#include "post.trm"     /* postscript */
#include "qms.trm"      /* QMS laser printers */
#include "table.trm"    /* built-in, but used for the documentation */
#include "tgif.trm"     /* x11 tgif tool */
#include "tkcanvas.trm" /* tcl/tk */
#include "v384.trm"     /* Vectrix 384 printer, also Tandy colour */

/* wire printers */
#define EPSONP          /* Epson LX-800, Star NL-10, NX-1000 and lots of others */
#define EPS60           /* Epson-style 60-dot per inch printers */
#define EPS180          /* Epson-style 180-dot per inch (24 pin) printers */
#define NEC
#define OKIDATA
#define STARC
#define TANDY60         /* Tandy DMP-130 series 60-dot per inch graphics */
#include "epson.trm"

/* TeX related terminals */
#define EMTEX
#include "latex.trm"    /* latex and emtex */
#include "pslatex.trm"  /* latex/tex with picture in postscript */
#include "eepic.trm"    /* EEPIC-extended LaTeX driver, for EEPIC users */
#include "tpic.trm"     /* TPIC specials for TeX */
#include "pstricks.trm" /* LaTeX picture environment with PSTricks macros */
#include "texdraw.trm"  /* TeXDraw drawing package for LaTeX */
#include "metafont.trm" /* METAFONT */

