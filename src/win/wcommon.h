/*
 * $Id: wcommon.h,v 1.8 2005/08/04 16:44:58 mikulik Exp $
 */

/* GNUPLOT - wcommon.h */

/*[
 * Copyright 1992 - 1993, 1998, 2004 Maurice Castro, Russell Lang
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
 * AUTHORS
 *
 *   Maurice Castro
 *   Russell Lang
 */

#if WINVER >= 0x030a
#ifndef CYGWIN
#include <shellapi.h>
#endif
#endif
/* this file contains items that are internal to wgnuplot.dll */

#ifdef WIN32

  /* Enable new directory dialogs on Win32 */
# define WITH_ADV_DIR_DIALOG

  /* Choose between the directory dialog of the windows shell and
     a modified version of the "file open" dialog */
# define SHELL_DIR_DIALOG 

#endif
  

/* winmain.c */
#ifdef WIN32
# define PACKVERSION(major,minor) MAKELONG(minor,major)
extern DWORD GetDllVersion(LPCTSTR lpszDllName);
extern char *appdata_directory(void);
#endif

/* wgnuplib.c */
extern HINSTANCE hdllInstance;
extern LPSTR szParentClass;
extern LPSTR szTextClass;
extern LPSTR szPauseClass;
extern LPSTR szGraphClass;
extern LPSTR szAboutClass;

void NEAR * LocalAllocPtr(UINT flags, UINT size);
void NEAR * LocalReAllocPtr(void NEAR * ptr, UINT flags, UINT size);
void LocalFreePtr(void NEAR *ptr);
LPSTR GetInt(LPSTR str, LPINT pval);

/* wtext.c */
void UpdateText(LPTW, int);
void CreateTextClass(LPTW lptw);
void NewLine(LPTW);
void TextPutStr(LPTW lptw, LPSTR str);
void WriteTextIni(LPTW lptw);
void ReadTextIni(LPTW lptw);
#if WINVER >= 0x030a
void DragFunc(LPTW lptw, HDROP hdrop);
#endif

/* wmenu.c - Menu */
void SendMacro(LPTW lptw, UINT m);
void LoadMacros(LPTW lptw);
void CloseMacros(LPTW lptw);

/* wprinter.c - Printer setup and dump */
BOOL PrintSize(HDC printer, HWND hwnd, LPRECT lprect);
void PrintRegister(GP_LPPRINT lpr);
void PrintUnregister(GP_LPPRINT lpr);
#if WINVER >= 0x030a
BOOL CALLBACK WINEXPORT PrintAbortProc(HDC hdcPrn, int code);
BOOL CALLBACK WINEXPORT PrintDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
#endif

/* wgraph.c */

