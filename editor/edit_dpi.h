#ifndef EDIT_DPI_H
#define EDIT_DPI_H

/*
 * edit_dpi.h -- display-scale support for the FLTK editors (SoobEditor,
 * codeedit).
 *
 * Problem: FLTK 1.3 knows nothing about DPI. On Windows 8.1+ at a display
 * scale other than 100% a DPI-unaware process is bitmap-stretched by the
 * compositor -- the right size, but blurry. Merely declaring the process
 * DPI-aware fixes the blur but leaves every hard-coded pixel layout (and the
 * 12/14 px fonts) physically tiny at 150%.
 *
 * Fix, in two halves:
 *   1. editDpiInit() declares the process DPI-aware and derives
 *      editDpiScale = system DPI / 96.
 *   2. The UI is scaled by that factor: font sizes at the source
 *      (FL_NORMAL_SIZE, scrollbar/tooltip sizes, explicit sizes via
 *      editDpi()), geometry by walking the finished widget tree once with
 *      editDpiScaleTree(). Existing layout code keeps its 96-DPI constants.
 *
 * SYSTEM-aware, deliberately not per-monitor: FLTK 1.3 cannot re-layout on
 * WM_DPICHANGED, so on a second monitor with a different scale we would rather
 * let Windows stretch us than show a wrongly-sized UI. (The game uses
 * per-monitor via SOOB-Core's dpi.h -- it re-sizes itself; the editors cannot.)
 *
 * Old-OS safety: every API is resolved at runtime via GetProcAddress. On
 * Win98 / 2000 / XP none exist, the process stays unaware, and editDpiScale
 * stays 1.0 -- the editors look exactly as before. No-op on non-Win32.
 *
 * Call editDpiInit() first thing in main(), before any FLTK call that opens
 * the display: awareness is locked in at the first window / DPI query.
 */

#include <FL/Fl.H>
#include <FL/Fl_Widget.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Image.H>

#ifdef _WIN32
#include <windows.h>
#endif

static float editDpiScale = 1.0f;
/* 1 once the process is DPI-aware -- which also means Vista+, so the Vista
   fonts (Segoe UI, Consolas) are present. Always 0 on Win98/2000/XP. */
static int   editDpiAware = 0;

/* Scale a 96-DPI pixel length to the current display. */
static int editDpi(int v)
{
    return (int)((float)v * editDpiScale + (v >= 0 ? 0.5f : -0.5f));
}

#ifdef _WIN32
/* Returns 1 if the process is now DPI-aware (set by us or already set). */
static int editDpiSetAware(void)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");

    /* Win10 1607+: DPI_AWARENESS_CONTEXT_SYSTEM_AWARE == (HANDLE)-2. */
    if (user32) {
        typedef BOOL (WINAPI *SetCtxFn)(HANDLE);
        SetCtxFn setCtx =
            (SetCtxFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setCtx && setCtx((HANDLE)-2)) return 1;
    }

    /* Win8.1+: shcore!SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE == 1). */
    HMODULE shcore = LoadLibraryA("shcore.dll");
    if (shcore) {
        typedef HRESULT (WINAPI *SetAwarenessFn)(int);
        SetAwarenessFn setAwareness =
            (SetAwarenessFn)GetProcAddress(shcore, "SetProcessDpiAwareness");
        HRESULT hr = setAwareness ? setAwareness(1) : (HRESULT)-1;
        FreeLibrary(shcore);
        /* E_ACCESSDENIED = already set (e.g. by a manifest): still aware. */
        if (hr == S_OK || hr == E_ACCESSDENIED) return 1;
    }

    /* Vista..Win8: user32!SetProcessDPIAware (system-aware). */
    if (user32) {
        typedef BOOL (WINAPI *SetDpiAwareFn)(void);
        SetDpiAwareFn setDpiAware =
            (SetDpiAwareFn)GetProcAddress(user32, "SetProcessDPIAware");
        if (setDpiAware && setDpiAware()) return 1;
    }
    return 0;                           /* Win98 / 2000 / XP */
}
#endif

/* Declare DPI awareness, compute editDpiScale, switch the UI face to Segoe UI
   (Vista+ only), and scale FLTK's global sizes. Pass the FL_NORMAL_SIZE the
   app wants at 96 DPI. */
static void editDpiInit(int normalSize)
{
#ifdef _WIN32
    if (editDpiSetAware()) {
        editDpiAware = 1;
        HDC dc = GetDC(NULL);
        int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
        if (dc) ReleaseDC(NULL, dc);
        if (dpi > 96) editDpiScale = (float)dpi / 96.0f;

        /* Native UI face. FLTK maps FL_HELVETICA to Arial, which reads larger
           and wider than the Segoe UI every Vista+ app uses at the same pixel
           size. Awareness succeeding implies Vista+, where Segoe UI ships;
           Win98/2000/XP never get here and keep Arial. The first character
           is FLTK's Win32 style code: ' ' regular, B bold, I italic, P both. */
        Fl::set_font(FL_HELVETICA,             " Segoe UI");
        Fl::set_font(FL_HELVETICA_BOLD,        "BSegoe UI");
        Fl::set_font(FL_HELVETICA_ITALIC,      "ISegoe UI");
        Fl::set_font(FL_HELVETICA_BOLD_ITALIC, "PSegoe UI");
    }
#endif
    /* Widgets, menus, tooltips and fl_ask dialogs all take their default font
       size from FL_NORMAL_SIZE at construction, so this must run before any
       widget is created. */
    FL_NORMAL_SIZE = editDpi(normalSize);
    Fl::scrollbar_size(editDpi(Fl::scrollbar_size()));
}

/* Scale the geometry of a finished widget tree (positions and sizes; fonts
   are already scaled via FL_NORMAL_SIZE). Call once per window, after end()
   and before show(). Groups are resized shallowly via Fl_Widget::resize so
   they do not drag their children around, then their children are scaled
   individually and the group's resize memory is reset with init_sizes().
   Not for trees holding self-laying-out groups (Fl_Text_Display,
   Fl_Scroll): add those afterwards, at already-scaled coordinates. */
static void editDpiScaleTree(Fl_Widget *w)
{
    if (editDpiScale == 1.0f) return;

    Fl_Group *g = w->as_group();
    int isTop = w->as_window() && !w->parent();
    /* A top-level window's x/y is its screen position: leave it alone. */
    int X = isTop ? w->x() : editDpi(w->x());
    int Y = isTop ? w->y() : editDpi(w->y());
    int W = editDpi(w->w());
    int H = editDpi(w->h());

    if (g) {
        w->Fl_Widget::resize(X, Y, W, H);
        for (int i = 0; i < g->children(); i++)
            editDpiScaleTree(g->child(i));
        g->init_sizes();
    } else {
        w->resize(X, Y, W, H);          /* virtual: e.g. Fl_Value_Input moves its inner input */
    }
}

/* Scale an image for the current display (nearest-neighbour -- these are
   small XPM icons). Takes ownership: returns img itself at 100%, otherwise a
   scaled copy with img deleted. */
static Fl_Image *editDpiImage(Fl_Image *img)
{
    if (editDpiScale == 1.0f || !img) return img;
    Fl_Image *c = img->copy(editDpi(img->w()), editDpi(img->h()));
    delete img;
    return c;
}

#endif /* EDIT_DPI_H */
