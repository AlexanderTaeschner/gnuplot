/*
 * $Id: gp_types.h,v 1.12 2002/01/26 17:55:07 joze Exp $
 */

/* GNUPLOT - gp_types.h */

/*[
 * Copyright 2000   Thomas Williams, Colin Kelley
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

#ifndef GNUPLOT_GPTYPES_H
#define GNUPLOT_GPTYPES_H

#include "syscfg.h"

#define MAX_ID_LEN 50		/* max length of an identifier */
#define MAX_LINE_LEN 1024	/* maximum number of chars allowed on line */

#define DEG2RAD (M_PI / 180.0)

/* These used to be #defines in plot.h. For easier debugging, I've
 * converted most of them into enum's. */
enum DATA_TYPES {
	INTGR, CMPLX
};

enum PLOT_TYPE {
	FUNC, DATA, FUNC3D, DATA3D
};

/* we explicitly assign values to the types, such that we can
 * perform bit tests to see if the style involves points and/or lines
 * bit 0 (val 1) = line, bit 1 (val 2) = point, bit 2 (val 4)= error
 * This allows rapid decisions about the sample drawn into the key,
 * for example.
 */
/* HBB 20010610: new enum, to make mnemonic names for these flags
 * accessible everywhere */
typedef enum e_PLOT_STYLE_FLAGS {
    PLOT_STYLE_HAS_LINE      = (1<<0),
    PLOT_STYLE_HAS_POINT     = (1<<1),
    PLOT_STYLE_HAS_ERRORBAR  = (1<<2)
} PLOT_STYLE_FLAGS;

typedef enum PLOT_STYLE {
    LINES        =  0*(1<<3) + PLOT_STYLE_HAS_LINE,
    POINTSTYLE   =  1*(1<<3) + PLOT_STYLE_HAS_POINT,
    IMPULSES     =  2*(1<<3) + PLOT_STYLE_HAS_LINE,
    LINESPOINTS  =  3*(1<<3) + (PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_LINE),
    DOTS         =  4*(1<<3) + 0,
    XERRORBARS   =  5*(1<<3) + (PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR),
    YERRORBARS   =  6*(1<<3) + (PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR),
    XYERRORBARS  =  7*(1<<3) + (PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR),
    BOXXYERROR   =  8*(1<<3) + PLOT_STYLE_HAS_LINE,
    BOXES        =  9*(1<<3) + PLOT_STYLE_HAS_LINE,
    BOXERROR     = 10*(1<<3) + PLOT_STYLE_HAS_LINE,
    STEPS        = 11*(1<<3) + PLOT_STYLE_HAS_LINE,
    FSTEPS       = 12*(1<<3) + PLOT_STYLE_HAS_LINE,
    HISTEPS      = 13*(1<<3) + PLOT_STYLE_HAS_LINE,
    VECTOR       = 14*(1<<3) + PLOT_STYLE_HAS_LINE,
    CANDLESTICKS = 15*(1<<3) + PLOT_STYLE_HAS_ERRORBAR,
    /* FIXME HBB 20010214: shouldn't fincancebars have ERRORBARS set?
     * They behave very much like errorbars, with the sole
     * exception of the key entry ... */
    FINANCEBARS  = 16*(1<<3) + PLOT_STYLE_HAS_LINE,
    XERRORLINES  = 17*(1<<3) + (PLOT_STYLE_HAS_LINE | PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR),
    YERRORLINES  = 18*(1<<3) + (PLOT_STYLE_HAS_LINE | PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR),
    XYERRORLINES = 19*(1<<3) + (PLOT_STYLE_HAS_LINE | PLOT_STYLE_HAS_POINT | PLOT_STYLE_HAS_ERRORBAR)
#if USE_ULIG_FILLEDBOXES
    , FILLEDBOXES  = 20*(1<<3) + PLOT_STYLE_HAS_LINE  /* like BOXES (ULIG) */
#endif /* USE_ULIG_FILLEDBOXES */
#ifdef PM3D
    , FILLEDCURVES = 21*(1<<3) + PLOT_STYLE_HAS_LINE
    , PM3DSURFACE  = 22*(1<<3) + 0
#endif
} PLOT_STYLE;

typedef enum PLOT_SMOOTH { 
    SMOOTH_NONE,
    SMOOTH_ACSPLINES,
    SMOOTH_BEZIER,
    SMOOTH_CSPLINES,
    SMOOTH_SBEZIER,
    SMOOTH_UNIQUE,
    SMOOTH_FREQUENCY
} PLOT_SMOOTH;

/* FIXME HBB 20000521: 'struct value' and its part, 'cmplx', should go
 * into one of scanner/internal/standard/util .h, but I've yet to
 * decide which of them */

#if !(defined(ATARI)&&defined(__GNUC__)&&defined(_MATH_H)) &&  !(defined(MTOS)&&defined(__GNUC__)&&defined(_MATH_H)) /* FF's math.h has the type already */
struct cmplx {
	double real, imag;
};
#endif

typedef struct value {
    enum DATA_TYPES type;
    union {
	int int_val;
	struct cmplx cmplx_val;
    } v;
} t_value;

/* Defines the type of a coordinate */
/* INRANGE and OUTRANGE points have an x,y point associated with them */
typedef enum coord_type {
    INRANGE,			/* inside plot boundary */
    OUTRANGE,			/* outside plot boundary, but defined */
    UNDEFINED			/* not defined at all */
} coord_type;


typedef struct coordinate {
    enum coord_type type;	/* see above */
    coordval x, y, z;
    coordval ylow, yhigh;	/* ignored in 3d */
    coordval xlow, xhigh;	/* also ignored in 3d */
    coordval color;		/* PM3D's color value to be used */
#if defined(WIN16) || (defined(MSDOS) && defined(__TURBOC__))
    /* FIXME HBB 20020301: addition of 'color' probably broke this */
    char pad[2];		/* pad to 32 byte boundary */
#endif
} coordinate;

#endif /* GNUPLOT_GPTYPES_H */
