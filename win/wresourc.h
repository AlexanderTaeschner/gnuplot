/*
 * $Id: wresourc.h,v 1.5 1997/01/05 22:04:21 drd Exp $
 */

/* GNUPLOT - win/wresourc.h */
/*
 * Copyright (C) 1992   Maurice Castro, Russell Lang
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
 *   Maurice Castro
 *   Russell Lang
 * 
 * Send your comments or suggestions to 
 *  info-gnuplot@dartmouth.edu.
 * This is a mailing list; to join it send a note to 
 *  majordomo@dartmouth.edu.  
 * Send bug reports to
 *  bug-gnuplot@dartmouth.edu.
 */

#ifndef DS_3DLOOK
#define DS_3DLOOK 0x0004L
#endif

/* This contains items internal to wgnuplot.dll
   that are used by the Resource Compiler */

/* wmenu.c */
#define ID_PROMPT 300
#define ID_ANSWER 302
#define NUMMENU 256

/* wpause.c */

/* wtext.c */
#define AB_ICON 250
#define AB_TEXT1 251
#define AB_TEXT2 252
#define AB_TEXT3 253

/* wgraph.c */
#define M_GRAPH_TO_TOP NUMMENU+1
#define M_CHOOSE_FONT  NUMMENU+2
#define M_BACKGROUND   NUMMENU+3
#define M_COLOR        NUMMENU+4
#define M_COPY_CLIP    NUMMENU+5
#define M_LINESTYLE    NUMMENU+6
#define M_PRINT        NUMMENU+7
#define M_WRITEINI     NUMMENU+8
#define M_PASTE        NUMMENU+9
#define M_ABOUT        NUMMENU+10
#define M_REBUILDTOOLS NUMMENU+11
#define M_COMMANDLINE  NUMMENU+12
/* wtext.c */
#define M_SYSCOLORS    NUMMENU+13

/* wprinter.c */
#define PSIZE_SBOX 100
#define PSIZE_DEF 101
#define PSIZE_OTHER 102
#define PSIZE_DEFX 103
#define PSIZE_DEFY 104
#define PSIZE_X 105
#define PSIZE_Y 106
#define PSIZE_OFFBOX 107
#define PSIZE_OFFX 108
#define PSIZE_OFFY 109
#define CANCEL_PCDONE 120
#define SPOOL_PORT 121

/* wgraph.c */
/* line style dialog box */
#define LS_LINENUM 200
#define LS_MONOBOX 201
#define LS_COLORBOX 202
#define LS_MONOSTYLE 203
#define LS_MONOWIDTH 204
#define LS_CHOOSECOLOR 205
#define LS_COLORSAMPLE 206
#define LS_COLORSTYLE 207
#define LS_COLORWIDTH 208
#define LS_DEFAULT 209

