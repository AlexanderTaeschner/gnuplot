/* GNUPLOT - wxt_gui.h */

/*[
 * Copyright 2005,2006   Timothee Lecomte
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
 *
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * GNU General Public License Version 2 or later (the "GPL"), in which case the
 * provisions of GPL are applicable instead of those above. If you wish to allow
 * use of your version of this file only under the terms of the GPL and not
 * to allow others to use your version of this file under the above gnuplot
 * license, indicate your decision by deleting the provisions above and replace
 * them with the notice and other provisions required by the GPL. If you do not
 * delete the provisions above, a recipient may use your version of this file
 * under either the GPL or the gnuplot license.
]*/

/* -----------------------------------------------------
 * The following code uses the wxWidgets library, which is
 * distributed under its own licence (derivated from the LGPL).
 *
 * You can read it at the following address :
 * http://www.wxwidgets.org/licence.htm
 * -----------------------------------------------------*/

/* ------------------------------------------------------
 * This file is the C++ header dedicated to wxt_gui.cpp
 * Everything here is static.
 * ------------------------------------------------------*/

/* ===========================================================
 * includes
 * =========================================================*/

#ifndef GNUPLOT_WXT_H
# define GNUPLOT_WXT_H

/* NOTE : Order of headers inclusion :
 * - wxWidgets headers must be included before Windows.h
 * to avoid conflicts on Unicode macros,
 * - then stdfn.h must be included, to obtain definitions from config.h
 * - then the rest */

/* main wxWidgets header */
#include <wx/wxprec.h>
#ifndef WX_PRECOMP
# include <wx/wx.h>
#endif /* WX_PRECOMP */

/* clipboard functionnality */
#include <wx/dataobj.h>
#include <wx/clipbrd.h>

/* wxImage facility */
#include <wx/image.h>

/* double buffering facility for the drawing context */
#include <wx/dcbuffer.h>

/* path methods to look for pngs */
#include <wx/stdpaths.h>

/* system options used with wxMSW to workaround PNG problem in the toolbar */
#include <wx/sysopt.h>

/* wxConfig stuff */
#include <wx/config.h>

/* wxGenericValidator */
#include <wx/valgen.h>

/* c++ vectors and lists, used to store gnuplot commands */
#include <vector>
#include <list>

extern "C" {
# include "stdfn.h"
}

/* if the gtk headers are available, use them to tweak some behaviours */
#if defined(__WXGTK__)&&defined(HAVE_GTK)
# define USE_GTK
#endif

/* With wxGTK, we use a different cairo surface starting from gtk28 */
#if defined(__WXGTK__)&&defined(HAVE_GTK28)
# define GTK_SURFACE
#endif
#if defined(__WXGTK__)&&!defined(HAVE_GTK28)
# define IMAGE_SURFACE
#endif

/* temporarly undef GTK_SURFACE for two reasons :
 * - because of a CAIRO_OPERATOR_SATURATE bug,
 * - because as for now, it is slower than the pure image surface,
 * (multiple copies between video memory and main memory for operations that are
 * not supported by the X server) */
#ifdef GTK_SURFACE
# undef GTK_SURFACE
# define IMAGE_SURFACE
#endif

extern "C" {
/* for JUSTIFY, encoding, *term definition, color.h */
# include "term_api.h"
/* for do_event declaration */
# include "mouse.h"
/* for rgb functions */
# include "getcolor.h"
/* for paused_for_mouse, PAUSE_BUTTON1 and friends */
# include "command.h"
/* for interactive, and bail_to_command_line declaration */
# include "plot.h"
/* for int_error declaration */
# include "util.h"

/* Windows native backend,
 * redefinition of fprintf, getch...
 * console window */
# ifdef _Windows
#  include "Windows.h"
#  include "win/wtext.h"
#  include "win/winmain.h"
# endif

/* for cairo_t */
# include <cairo.h>

# ifdef USE_GTK
#  include <gdk/gdk.h>
#  include <gtk/gtk.h>
# endif

# ifdef _Windows
#  include <cairo-win32.h>
# endif

/* to avoid to receive SIGINT in wxWidgets threads,
 * already included unconditionally in plot.c,
 * only needed here when using WXGTK
 * (or at least not needed on Windows) */
# include <signal.h>
}

/* interaction with wxt.trm(wxt_options) : plot number, enhanced state.
 * Communication with gnuplot (wxt_exec_event)
 * Initialization of the library, and checks */
#include "wxt_term.h"
/* drawing facility */
#include "gp_cairo.h"
/* 'persist' */
#include "wxt_plot.h"

/* ======================================================================
 * declarations
 * ====================================================================*/

#ifdef __WXGTK__
/* thread class, where the gui loop runs.
 * Not needed with Windows, where the main loop
 * already processes the gui messages */
class wxtThread : public wxThread
{
public:
	wxtThread() : wxThread(wxTHREAD_JOINABLE) {};

	/* thread execution starts in the following */
	void *Entry();
};

/* instance of the thread */
static wxtThread * thread;
#elif defined(__WXMSW__)
#else
# error "Not implemented"
#endif /* __WXGTK__ */

/* Define a new application type, each gui should derive a class from wxApp */
class wxtApp : public wxApp
{
public:
	/* This one is called just after wxWidgets initialization */
	bool OnInit();
	/* cleanup on exit */
	int OnExit();

	/* data path used to look for png icons in the toolbar
	 * displayed in the "about" dialog */
	wxString datapath;
private:
	/* load a toolbar icon, given its path and number */
	bool LoadPngIcon(wxString path, int icon_number);
	/* load a cursor */
	void LoadCursor(wxCursor &cursor, char* xpm_bits[], int hotspot_x, int hotspot_y);
};

/* IDs for gnuplot commands */
typedef enum wxt_gp_command_t {
	command_color = 1,
	command_linetype,
	command_linestyle,
	command_move,
	command_vector,
	command_put_text,
	command_enhanced_put_text,
	command_set_font,
	command_justify,
	command_point,
	command_pointsize,
	command_linewidth,
	command_text_angle,
	command_fillbox,
	command_filled_polygon,
	command_image
} wxt_gp_command_t;

/* base structure for storing gnuplot commands */
typedef struct gp_command {
	enum wxt_gp_command_t command;
	unsigned int x1;
	unsigned int y1;
	unsigned int x2;
	unsigned int y2;
	unsigned int x3;
	unsigned int y3;
	unsigned int x4;
	unsigned int y4;
	int integer_value;
	int integer_value2;
	t_imagecolor color_mode;
	double double_value;
	char *string;
	gpiPoint *corners;
	enum JUSTIFY mode;
	coordval * image;
	rgb_color color;
} gp_command;

/* declare a type for our list of gnuplot commands */
typedef std::list<gp_command> command_list_t;

/* panel class : this is the space between the toolbar
 * and the status bar, where the plot is actually drawn. */
class wxtPanel : public wxPanel
{
public :
	/* constructor*/
	wxtPanel( wxWindow* parent, wxWindowID id, const wxSize& size );

	/* event handlers (these functions should _not_ be virtual)*/
	void OnPaint( wxPaintEvent &event );
	void OnEraseBackground( wxEraseEvent &event );
	void OnSize( wxSizeEvent& event );
	void OnMotion( wxMouseEvent& event );
	void OnLeftDown( wxMouseEvent& event );
	void OnLeftUp( wxMouseEvent& event );
	void OnMiddleDown( wxMouseEvent& event );
	void OnMiddleUp( wxMouseEvent& event );
	void OnRightDown( wxMouseEvent& event );
	void OnRightUp( wxMouseEvent& event );
	void OnKeyDownModifier( wxKeyEvent& event );
	void OnKeyUpModifier( wxKeyEvent& event );
	void OnKeyDownChar( wxKeyEvent& event );
	void RaiseConsoleWindow();
	void DrawToDC(wxWindowDC &dc, wxRegion& region);
	void Draw();

	/* list of commands sent by gnuplot */
	command_list_t command_list;
	/* mutex protecting this list */
	wxMutex command_list_mutex;
	/* method to clear the command list, free the allocated memory */
	void ClearCommandlist();

	/* mouse and zoom events datas */
	bool wxt_zoombox;
	int mouse_x, mouse_y;
	int zoom_x1, zoom_y1;
	wxString zoom_string1, zoom_string2;
	bool wxt_ruler;
	double wxt_ruler_x, wxt_ruler_y;
	/* modifier_mask for wxKeyEvents */
	int modifier_mask;

	/* cairo context creation */
	void wxt_cairo_create_context();
	void wxt_cairo_free_context();
	/* platform-dependant cairo context creation */
	int wxt_cairo_create_platform_context();
	void wxt_cairo_free_platform_context();

#ifdef IMAGE_SURFACE
	void wxt_cairo_create_bitmap();
#endif

	/* functions used to process the command list */
	void wxt_cairo_refresh();
	void wxt_cairo_exec_command(gp_command command);

	/* the plot structure, defined in gp_cairo.h */
	plot_struct plot;

	/* destructor*/
	~wxtPanel();

private:
	/* any class wishing to process wxWidgets events must use this macro */
	DECLARE_EVENT_TABLE()

	/* watches for time between mouse clicks */
	wxStopWatch left_button_sw;
	wxStopWatch right_button_sw;
	wxStopWatch middle_button_sw;

	/* cairo surfaces, which depends on the implementation */
#if defined(GTK_SURFACE)
	GdkPixmap *gdkpixmap;
#elif defined(__WXMSW__)
	HDC hdc;
	HBITMAP hbm;
#else /* generic 'image' surface */
	unsigned int *data32;
	wxBitmap* cairo_bitmap;
#endif
};


/* class implementing the configuration dialog */
class wxtConfigDialog : public wxDialog
{
  public :
	/* constructor*/
	wxtConfigDialog(wxWindow* parent);

	void OnRendering( wxCommandEvent& event );
	void OnButton( wxCommandEvent& event );
	void OnClose( wxCloseEvent& event );

	/* destructor*/
	~wxtConfigDialog() {};
  private:
	/* any class wishing to process wxWidgets events must use this macro */
	DECLARE_EVENT_TABLE()

	/* these two elements are enabled/disabled dynamically */
	wxSlider *slider;
	wxStaticText *text_hinting;

	/* settings */
	bool raise_setting;
	bool persist_setting;
	/* rendering_setting :
	 * 0 = no antialiasing, no oversampling
	 * 1 = antialiasing, no oversampling
	 * 2 = antialiasing and oversampling
	 * Note that oversampling without antialiasing makes no sense */
	int rendering_setting;
	int hinting_setting;
};


/* Define a new frame type: this is our main frame */
class wxtFrame : public wxFrame
{
public:
	/* constructor*/
	wxtFrame( const wxString& title, wxWindowID id, int xpos, int ypos, int width, int height );

	/* event handlers (these functions should _not_ be virtual)*/
	void OnClose( wxCloseEvent& event );
	void OnSize( wxSizeEvent& event );
	void OnCopy( wxCommandEvent& event );
	void OnReplot( wxCommandEvent& event );
	void OnToggleGrid( wxCommandEvent& event );
	void OnZoomPrevious( wxCommandEvent& event );
	void OnZoomNext( wxCommandEvent& event );
	void OnAutoscale( wxCommandEvent& event );
	void OnConfig( wxCommandEvent& event );
	void OnHelp( wxCommandEvent& event );

	/* destructor*/
	~wxtFrame() {};

	wxtPanel * panel;
	bool config_displayed;
private:
	wxtConfigDialog * config_dialog;

	/* any class wishing to process wxWidgets events must use this macro */
	DECLARE_EVENT_TABLE()
};

/* IDs for the controls and the menu commands, and for wxEvents */
enum {
/* start at wxID_HIGHEST to avoid collisions */
Toolbar_CopyToClipboard = wxID_HIGHEST,
Toolbar_Replot,
Toolbar_ToggleGrid,
Toolbar_ZoomPrevious,
Toolbar_ZoomNext,
Toolbar_Autoscale,
Toolbar_Config,
Toolbar_Help,
Config_Rendering,
Config_OK,
Config_APPLY,
Config_CANCEL
};


/* array of toolbar icons */
#define ICON_NUMBER 8
static wxBitmap* toolBarBitmaps[ICON_NUMBER];
/* array of toolbar icons location */
static wxString icon_file[ICON_NUMBER] = {
wxT("/clipboard.png"),
wxT("/replot.png"),
wxT("/grid.png"),
wxT("/previouszoom.png"),
wxT("/nextzoom.png"),
wxT("/autoscale.png"),
wxT("/config.png"),
wxT("/help.png")
};

/* frames icons in the window manager */
static wxIconBundle icon;

/* mouse cursors */
static wxCursor wxt_cursor_cross;
static wxCursor wxt_cursor_right;
static wxCursor wxt_cursor_rotate;
static wxCursor wxt_cursor_size;

#ifdef DEBUG
 /* performance watch */
 static wxStopWatch sw;
#endif

/* wxt_abort_init is set to true if there is an error when
 * wxWidgets is initialized, for example if the X server is unreachable.
 * If there has been an error, we should not try to initialize again,
 * because the following try-out will return that it succeeded,
 * although this is false. */
static bool wxt_abort_init = false;

/* Sometimes, terminal functions are called although term->init() has not been called before.
 * It's the case when you hit 'set terminal wxt' twice.
 * So, we check for earlier initialisation.
 * External module, such as cairo, can check for status!=0, instead of using the enums defined here.
 * This is used to process interrupt (ctrl-c) */
enum {
STATUS_OK = 0,
STATUS_UNINITIALIZED,
STATUS_INCONSISTENT,
STATUS_INTERRUPT_ON_NEXT_CHECK,
STATUS_INTERRUPT
};
static int wxt_status = STATUS_UNINITIALIZED;

/* structure to store windows and their ID */
typedef struct wxt_window_t {
	wxWindowID id;
	wxtFrame * frame;
} wxt_window_t;

/* list of already created windows */
static std::vector<wxt_window_t> wxt_window_list;

/* given a window number, return the window structure */
static wxt_window_t* wxt_findwindowbyid(wxWindowID);

/* pointers to currently active instances */
static wxt_window_t *wxt_current_window;
static command_list_t *wxt_current_command_list;
static wxtPanel *wxt_current_panel;
static plot_struct *wxt_current_plot;

/* push a command to the commands list */
static void wxt_command_push(gp_command command);

/* routine to send an event to gnuplot */
static void wxt_exec_event(int type, int mx, int my, int par1, int par2, wxWindowID id);

/* process the event list */
static int wxt_process_events();

/* event queue and its mutex */
static wxMutex mutexProtectingEventList;
static std::list<gp_event_t> EventList;
static bool wxt_check_eventlist_empty();
static void wxt_clear_event_list();

/* state of the main thread, and its mutex, used to know when and how to wake it up */
typedef enum wxt_thread_state_t {
	RUNNING = 0,
	WAITING_FOR_STDIN
} wxt_thread_state_t;
static wxt_thread_state_t wxt_thread_state;
static wxMutex mutexProtectingThreadState;
static wxt_thread_state_t wxt_check_thread_state();
static void wxt_change_thread_state(wxt_thread_state_t state);

/* returns true if at least one plot window is opened.
 * Used to handle 'persist' */
static bool wxt_window_opened();

/* helpers to handle the issues of the default Raise() and Lower() methods */
static void wxt_raise_window(wxt_window_t* window, bool force);
static void wxt_lower_window(wxt_window_t* window);

/* cleanup  on exit : close all created windows, delete thread if necessary */
static void wxt_cleanup();

/* helpers for gui mutex handling : they do nothing in WXMSW */
static void wxt_MutexGuiEnter();
static void wxt_MutexGuiLeave();

/* interrupt stuff */
static void (*original_siginthandler) (int);
static void wxt_sigint_handler(int WXUNUSED(sig));
static void wxt_sigint_return();
static void wxt_sigint_check();
static void wxt_sigint_init();
static void wxt_sigint_restore();
static int wxt_sigint_counter = 0;

#endif /*gnuplot_wxt_h*/
