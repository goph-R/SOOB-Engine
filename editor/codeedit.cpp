/*
 * codeedit.cpp -- the SOOB code editor: tabs, File and Edit menus, dark theme.
 *
 * Phases 1 and 2 of docs/editor-code-editor-plan.md. Built by e98.bat.
 *
 *   e98
 *   codeedit                      (one empty untitled tab)
 *   codeedit scripts\main.lua ... (opens each file in its own tab)
 *
 * NOTES FOR WIN98
 *
 * Fl_Double_Window, not Fl_Window. FLTK's plain window is single-buffered, so
 * every partial repaint lands straight on the screen and flickers visibly on
 * period hardware. Double buffering blits only the damaged region, so it costs
 * little and removes the flicker.
 *
 * File dialogs come from edit_filedlg.h, which calls the ANSI comdlg32 entry
 * points directly -- FLTK's Fl_Native_File_Chooser is wide-API only and dead on
 * 9x, and its own drawn chooser does not look like Windows. See
 * docs/editor-fltk-win98.md.
 *
 * Fl_Text_Display derives from Fl_Group, so a CodeEditor is a DIRECT child of
 * Fl_Tabs -- no wrapper group is needed, and the child's label is the tab
 * caption. It must be copy_label(), never label(): Fl_Widget stores the pointer
 * without copying, and our captions are built at runtime.
 */
#include <FL/Fl.H>
#include <FL/Fl_Double_Window.H>
#include <FL/Fl_Menu_Bar.H>
#include <FL/Fl_Tabs.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Button.H>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "edit_dpi.h"
#include "edit_menupad.h"
#include "edit_tabs.h"
#include "edit_code.h"
#include "edit_filedlg.h"
#include "edit_find.h"

/* Bump on a feature change. The __DATE__/__TIME__ stamp beside it is the one
 * that cannot lie: the compiler writes it, so a title showing an old timestamp
 * means the running exe is not the one you just built. */
#define CODEEDIT_VERSION "0.7"

#define MAX_DOCS   16
#define MENU_H     25
#define TABROW_H   30         /* tab row incl. the strip above the tabs */

typedef struct CodeDoc {
    CodeEditor *ed;
    char path[512];            /* empty => untitled */
    char label[160];           /* what is currently on the tab */
    int  shownDirty;           /* so labels are only rebuilt when they change */
} CodeDoc;

static CodeDoc          gDocs[MAX_DOCS];
static int              gDocCount = 0;
static Fl_Double_Window *gWin;
static CodeTabs         *gTabs;
static int              gUntitledSeq = 1;
static Fl_Double_Window *gFindWin;      /* defined with the Find UI below */

static void msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* ---- helpers ----------------------------------------------------------- */

/* strncpy that always terminates, so call sites do not have to remember. */
static void copyStr(char *dst, int dstlen, const char *src)
{
    int n;
    if (!dst || dstlen <= 0) return;
    if (!src) { dst[0] = '\0'; return; }
    n = (int)strlen(src);
    if (n > dstlen - 1) n = dstlen - 1;
    memcpy(dst, src, (size_t)n);
    dst[n] = '\0';
}

static const char *baseName(const char *p)
{
    const char *a = strrchr(p, '/');
    const char *b = strrchr(p, '\\');
    const char *s = a > b ? a : b;
    return s ? s + 1 : p;
}

/* Directory of the current document, for the file dialog's starting point. */
static const char *currentDir(char *buf, int len)
{
    int i, n;
    buf[0] = '\0';
    for (i = 0; i < gDocCount; i++) {
        if (gTabs->value() == (Fl_Widget *)gDocs[i].ed && gDocs[i].path[0]) {
            const char *b = baseName(gDocs[i].path);
            n = (int)(b - gDocs[i].path);
            if (n > 0 && n < len) { memcpy(buf, gDocs[i].path, (size_t)n); buf[n] = '\0'; }
            break;
        }
    }
    return buf[0] ? buf : ".";
}

static int docIndexOf(Fl_Widget *w)
{
    int i;
    for (i = 0; i < gDocCount; i++) if ((Fl_Widget *)gDocs[i].ed == w) return i;
    return -1;
}

static int currentIndex(void)
{
    return gTabs ? docIndexOf(gTabs->value()) : -1;
}

static void docArea(int *X, int *Y, int *W, int *H)
{
    *X = gTabs->x();
    *Y = gTabs->y() + editDpi(TABROW_H);
    *W = gTabs->w();
    *H = gTabs->h() - editDpi(TABROW_H);
}

/* Rebuild a tab caption, but only when it would actually change -- copy_label()
 * reallocates and redraws, and this is called from a timer. */
static void refreshLabel(int i)
{
    CodeDoc *d = &gDocs[i];
    char want[160];
    int dirty = d->ed->dirty();

    sprintf(want, "%.120s%s",
            d->path[0] ? baseName(d->path) : "untitled",
            dirty ? " *" : "");
    if (strcmp(want, d->label) == 0) return;

    strcpy(d->label, want);
    d->shownDirty = dirty;
    d->ed->copy_label(d->label);     /* copy_label, not label -- see file header */
    gTabs->redraw();
}

static void syncWrapItem(void);
static void syncLangItems(void);

static void labelTimer(void *)
{
    int i;
    for (i = 0; i < gDocCount; i++) refreshLabel(i);
    syncWrapItem();               /* the Word Wrap check follows the active tab */
    syncLangItems();              /* ...and so does the Language check */
    Fl::repeat_timeout(0.4, labelTimer);
}

/* ---- documents --------------------------------------------------------- */

static void retireEditor(CodeEditor *ed);

static CodeEditor *addDoc(const char *path)
{
    int X, Y, W, H;
    CodeDoc *d;

    if (gDocCount >= MAX_DOCS) {
        fl_alert("Too many open files (limit %d).", MAX_DOCS);
        return 0;
    }
    d = &gDocs[gDocCount];
    memset(d, 0, sizeof(*d));

    codeTrace(" addDoc: enter path=%s count=%d", path ? path : "(new)", gDocCount);
    docArea(&X, &Y, &W, &H);
    gTabs->begin();
    d->ed = new CodeEditor(X, Y, W, H);
    codeTrace(" addDoc: widget created %p", (void *)d->ed);
    d->ed->labelcolor(CODE_COL_TAB_OFF);      /* used only while NOT selected */
    gTabs->end();

    if (path && *path) {
        if (d->ed->loadFile(path) != 0) {
            fl_alert("Could not open\n%s", path);
            retireEditor(d->ed);      /* same three hazards as closeDoc */
            d->ed = 0;
            return 0;
        }
        copyStr(d->path, (int)sizeof(d->path), path);
    } else {
        gUntitledSeq++;
        d->ed->language(LEX_LANG_LUA);
        d->ed->text("");
    }

    gDocCount++;
    codeTrace(" addDoc: content set, count=%d", gDocCount);
    refreshLabel(gDocCount - 1);
    gTabs->value(d->ed);
    codeTrace(" addDoc: value ok");
    gTabs->resizable(gDocs[0].ed);   /* always a live child; see retireEditor */
    d->ed->take_focus();
    codeTrace(" addDoc: EXIT CLEAN");
    return d->ed;
}

/* Save doc i. `forcePrompt` turns Save into Save As. Returns 1 if written. */
static int saveDoc(int i, int forcePrompt)
{
    CodeDoc *d;
    char dirbuf[512];

    if (i < 0 || i >= gDocCount) return 0;
    d = &gDocs[i];

    if (forcePrompt || !d->path[0]) {
        char picked[512];
        const char *dir = currentDir(dirbuf, (int)sizeof(dirbuf));
        const char *suggest = d->path[0] ? baseName(d->path) : "untitled.lua";
        if (!codeFileSaveDialog(picked, (int)sizeof(picked), dir, suggest)) return 0;
        copyStr(d->path, (int)sizeof(d->path), picked);
        /* the extension may have changed the language */
        d->ed->language(lexLangFromPath(d->path));
    }
    if (d->ed->saveFile(d->path) != 0) {
        fl_alert("Could not save\n%s", d->path);
        return 0;
    }
    refreshLabel(i);
    msg("saved %s\n", d->path);
    return 1;
}

/* Returns 1 if it is OK to discard doc i (saved, or the user said so). */
static int confirmDiscard(int i)
{
    int c;
    if (i < 0 || i >= gDocCount || !gDocs[i].ed->dirty()) return 1;
    gTabs->value(gDocs[i].ed);            /* show what is being asked about */
    gTabs->redraw();
    c = fl_choice("Save changes to\n%s?",
                  "Cancel", "Save", "Don't save",
                  gDocs[i].path[0] ? baseName(gDocs[i].path) : "untitled");
    if (c == 0) return 0;                 /* Cancel */
    if (c == 1) return saveDoc(i, 0);     /* Save (may itself be cancelled) */
    return 1;                             /* Don't save */
}

/* Retire an editor widget safely.
 *
 * THREE separate hazards, all of which bite when closing a tab from the File
 * menu, because the callback runs inside the menu's own event loop:
 *
 * 1. Fl::delete_widget(), NOT delete. FLTK still holds references to a widget
 *    while a callback is running; deleting outright is documented as unsafe and
 *    is what crashed File / New -> Close. delete_widget() queues it for
 *    Fl::do_widget_deletion() once the loop unwinds.
 * 2. Fl_Group::remove() clears savedfocus_ but NOT resizable_ (Fl_Group.cxx),
 *    so removing the resizable child leaves a dangling pointer that the next
 *    Fl_Group::sizes() happily dereferences.
 * 3. Fl_Tabs::push_ is a bare Fl_Widget* for the tab under the mouse button. It
 *    is normally cleared on release, but clearing it costs nothing.
 */
static void retireEditor(CodeEditor *ed)
{
    if (!ed) return;
    codeTrace(" retire: enter ed=%p children=%d", (void *)ed, gTabs->children());
    gTabs->push(0);                                            /* hazard 3 */
    codeTrace(" retire: push(0) ok");
    if (gTabs->resizable() == (Fl_Widget *)ed)
        gTabs->resizable((Fl_Widget *)0);                      /* hazard 2 */
    codeTrace(" retire: resizable ok");
    gTabs->remove(ed);
    codeTrace(" retire: removed, children=%d", gTabs->children());
    Fl::delete_widget(ed);                                     /* hazard 1 */
    codeTrace(" retire: queued for deletion -- exit");
}

static void closeDoc(int i)
{
    int j;
    CodeEditor *dying;

    codeTrace("closeDoc: enter i=%d count=%d", i, gDocCount);
    if (i < 0 || i >= gDocCount) { codeTrace("closeDoc: bad index -- return"); return; }
    if (!confirmDiscard(i)) { codeTrace("closeDoc: user cancelled"); return; }

    codeTrace("closeDoc: confirmed, dying=%p", (void *)gDocs[i].ed);
    dying = gDocs[i].ed;

    /* Drop it from the array BEFORE retiring the widget, so nothing reachable
     * (the label timer, a redraw) can still find it. */
    for (j = i; j < gDocCount - 1; j++) gDocs[j] = gDocs[j + 1];
    gDocCount--;
    gDocs[gDocCount].ed = 0;

    codeTrace("closeDoc: array shifted, count=%d", gDocCount);
    retireEditor(dying);

    if (gDocCount == 0) {
        codeTrace("closeDoc: was last doc -> addDoc(0)");
        addDoc(0);
        codeTrace("closeDoc: addDoc returned");
    } else {
        int sel = i < gDocCount ? i : gDocCount - 1;
        codeTrace("closeDoc: selecting %d", sel);
        gTabs->value(gDocs[sel].ed);
        codeTrace("closeDoc: value ok");
        gTabs->resizable(gDocs[0].ed);      /* restore a LIVE resizable child */
        codeTrace("closeDoc: resizable ok");
        gDocs[sel].ed->take_focus();
        codeTrace("closeDoc: focus ok");
    }
    gTabs->redraw();
    codeTrace("closeDoc: EXIT CLEAN");
}

static int quitAll(void)
{
    int i;
    for (i = 0; i < gDocCount; i++)
        if (!confirmDiscard(i)) return 0;
    return 1;
}

/* ---- menu callbacks ---------------------------------------------------- */

static void cbNew (Fl_Widget *, void *) { addDoc(0); }

static void cbOpen(Fl_Widget *, void *)
{
    char picked[512], dirbuf[512];
    if (codeFileOpenDialog(picked, (int)sizeof(picked),
                           currentDir(dirbuf, (int)sizeof(dirbuf))))
        addDoc(picked);
}

static void cbSave  (Fl_Widget *, void *) { saveDoc(currentIndex(), 0); }
static void cbSaveAs(Fl_Widget *, void *) { saveDoc(currentIndex(), 1); }
static void cbClose (Fl_Widget *, void *)
{
    int i = currentIndex();
    codeTrace("cbClose: currentIndex=%d", i);
    closeDoc(i);
    codeTrace("cbClose: returned");
}

static void cbExit(Fl_Widget *, void *)
{
    if (quitAll()) {
        if (gFindWin) gFindWin->hide();   /* else Fl::run() never returns */
        gWin->hide();
    }
}

static void cbWinClose(Fl_Widget *, void *) { cbExit(0, 0); }

static void cbUndo(Fl_Widget *, void *) { int i = currentIndex(); if (i >= 0) gDocs[i].ed->undo(); }
static void cbRedo(Fl_Widget *, void *) { int i = currentIndex(); if (i >= 0) gDocs[i].ed->redo(); }

/* Word wrap is per document, so the menu check has to follow the active tab --
 * syncWrapItem() is called from the label timer, which is already running.
 *
 * The cost of wrapping is real but narrow: FLTK repaints the whole widget on a
 * keystroke that re-flows a paragraph (see the note in edit_code.h). Reading
 * and scrolling are unaffected, which is exactly the case this is for -- long
 * prose lines in a .md you are not editing. */
static Fl_Menu_Bar *gMenuBar;
static void cbWrap(Fl_Widget *, void *);   /* found by address below */

static void syncWrapItem(void)
{
    Fl_Menu_Item *it;
    int i = currentIndex();
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbWrap);
    if (!it) return;
    editMenuCheck(it, i >= 0 && gDocs[i].ed->wrapped());
}

static void cbWrap(Fl_Widget *, void *)
{
    int i = currentIndex();
    if (i < 0) return;
    gDocs[i].ed->wrapEnable(!gDocs[i].ed->wrapped());
    gDocs[i].ed->redraw();
    syncWrapItem();
}

static void cbWrapCol(Fl_Widget *, void *)
{
    CodeEditor *ed;
    char def[32];
    const char *ans;
    int i = currentIndex();

    if (i < 0) return;
    ed = gDocs[i].ed;
    sprintf(def, "%d", ed->wrapColumn());
    ans = fl_input("Wrap at column  (0 = wrap to window width):", def);
    if (!ans) return;
    ed->wrapColumn(atoi(ans));
    ed->wrapEnable(1);
    ed->redraw();
    syncWrapItem();
}

static void cbLang(Fl_Widget *w, void *v)
{
    int i = currentIndex();
    (void)w;
    if (i >= 0) gDocs[i].ed->language((int)(long)v);
    syncLangItems();
}

/* Language is per document too: check the active tab's language. The four
 * items are one contiguous run with user_data = LEX_LANG_*. Also catches
 * language changes the menu never saw (Save As with a new suffix). */
static void syncLangItems(void)
{
    Fl_Menu_Item *it;
    int i = currentIndex();
    int lang = i >= 0 ? gDocs[i].ed->language() : -1;
    if (!gMenuBar) return;
    it = (Fl_Menu_Item *)gMenuBar->find_item(cbLang);   /* first of the run */
    for (; it && it->text && it->callback() == cbLang; it = it->next()) {
        editMenuCheck(it, (int)(long)it->user_data() == lang);
    }
}

/* ---- Find / Replace ----------------------------------------------------
 * Modeless, so you can keep hunting while editing -- which means it must look
 * up the CURRENT editor on every click rather than holding one, since the user
 * can change tabs while it is open.
 */
static Fl_Input  *gFindIn, *gReplIn;
static Fl_Check_Button *gCaseChk, *gWrapChk;

static CodeEditor *curEd(void)
{
    int i = currentIndex();
    return i >= 0 ? gDocs[i].ed : 0;
}

/* Select [at, at+len) and scroll it into view. */
static void selectMatch(CodeEditor *ed, int at, int len)
{
    ed->insert_position(at + len);
    ed->buffer()->select(at, at + len);
    ed->show_insert_position();
    ed->redraw();
}

static void doFind(int backwards)
{
    CodeEditor *ed = curEd();
    const char *needle;
    int at, from, mc, wrap;

    if (!ed || !gFindIn) return;
    needle = gFindIn->value();
    if (!needle || !*needle) return;
    mc   = gCaseChk->value();
    wrap = gWrapChk->value();

    /* Search from the edge of the current selection, so repeated Find Next
     * walks forward instead of re-finding what is already highlighted. */
    if (backwards) {
        int selStart, selEnd;
        from = ed->buffer()->selection_position(&selStart, &selEnd)
             ? selStart : ed->insert_position();
        if (codeFindPrev(ed->buffer(), needle, from, mc, wrap, &at))
            selectMatch(ed, at, (int)strlen(needle));
        else
            fl_beep();
    } else {
        from = ed->insert_position();
        if (codeFindNext(ed->buffer(), needle, from, mc, wrap, &at))
            selectMatch(ed, at, (int)strlen(needle));
        else
            fl_beep();
    }
}

static void cbFindNext(Fl_Widget *, void *) { doFind(0); }
static void cbFindPrev(Fl_Widget *, void *) { doFind(1); }

static void cbReplaceOne(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    const char *needle, *repl;
    int selStart, selEnd;

    if (!ed) return;
    needle = gFindIn->value();
    repl   = gReplIn->value();
    if (!needle || !*needle) return;

    /* Only replace when the selection IS the match -- otherwise Replace would
     * clobber whatever happens to be selected. */
    if (ed->buffer()->selection_position(&selStart, &selEnd)) {
        int n = codeReplaceAt(ed->buffer(), selStart, needle, repl,
                              gCaseChk->value());
        if (n >= 0) {
            ed->insert_position(selStart + n);
            ed->buffer()->unselect();
        }
    }
    doFind(0);                       /* then move to the next one */
}

static void cbReplaceAll(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    const char *needle, *repl;
    int n;

    if (!ed) return;
    needle = gFindIn->value();
    repl   = gReplIn->value();
    if (!needle || !*needle) return;

    /* One undo step for the whole sweep, not one per match. */
    ed->beginUndoGroup();
    n = codeReplaceAll(ed->buffer(), needle, repl, gCaseChk->value());
    ed->endUndoGroup();

    ed->buffer()->unselect();
    ed->redraw();
    fl_message("Replaced %d occurrence%s.", n, n == 1 ? "" : "s");
}

static void cbFindClose(Fl_Widget *, void *) { if (gFindWin) gFindWin->hide(); }

static void openFindWindow(int withReplace)
{
    if (!gFindWin) {
        gFindWin = new Fl_Double_Window(380, 150, "Find / Replace");
        gFindWin->begin();
        gFindIn  = new Fl_Input(75, 10, 290, 24, "Find:");
        gReplIn  = new Fl_Input(75, 40, 290, 24, "Replace:");
        gCaseChk = new Fl_Check_Button(75, 68, 110, 22, "Match case");
        gWrapChk = new Fl_Check_Button(190, 68, 110, 22, "Wrap around");
        gWrapChk->value(1);
        { Fl_Button *b;
          b = new Fl_Button( 10, 96,  80, 24, "Find Next");   b->callback(cbFindNext);
          b = new Fl_Button( 95, 96,  80, 24, "Find Prev");   b->callback(cbFindPrev);
          b = new Fl_Button(180, 96,  80, 24, "Replace");     b->callback(cbReplaceOne);
          b = new Fl_Button(265, 96, 100, 24, "Replace All"); b->callback(cbReplaceAll);
          b = new Fl_Button(285, 122, 80, 24, "Close");       b->callback(cbFindClose);
        }
        gFindWin->end();
        editDpiScaleTree(gFindWin);
        gFindWin->callback(cbFindClose);
    }
    gReplIn->activate();
    if (!withReplace) gFindIn->take_focus();
    gFindWin->show();
    gFindIn->take_focus();
}

static void cbFind   (Fl_Widget *, void *) { openFindWindow(0); }
static void cbReplace(Fl_Widget *, void *) { openFindWindow(1); }

static void cbGotoLine(Fl_Widget *, void *)
{
    CodeEditor *ed = curEd();
    char def[32];
    const char *ans;
    int line, pos;

    if (!ed) return;
    sprintf(def, "%d", codeLineOfPos(ed->buffer(), ed->insert_position()));
    ans = fl_input("Go to line:", def);
    if (!ans) return;
    line = atoi(ans);
    if (line < 1) return;
    pos = codeGotoLinePos(ed->buffer(), line);
    ed->insert_position(pos);
    ed->buffer()->unselect();
    ed->show_insert_position();
    ed->take_focus();
    ed->redraw();
}

/* ---- menu -------------------------------------------------------------- */

static Fl_Menu_Item gMenu[] = {
    { "&File", 0, 0, 0, FL_SUBMENU },
        { "&New",        FL_CTRL + 'n', cbNew    },
        { "&Open...",    FL_CTRL + 'o', cbOpen   },
        { "&Save",       FL_CTRL + 's', cbSave   },
        { "Save &As...", 0,             cbSaveAs, 0, FL_MENU_DIVIDER },
        { "&Close",      FL_CTRL + 'w', cbClose  },
        { "E&xit",       FL_CTRL + 'q', cbExit   },
        { 0 },
    { "&Edit", 0, 0, 0, FL_SUBMENU },
        /* CodeEditor::handle() intercepts these first; the accelerators are
         * here so the menu documents them. Both routes call the same undo(). */
        { "&Undo", FL_CTRL + 'z', cbUndo },
        { "&Redo", FL_CTRL + 'y', cbRedo, 0, FL_MENU_DIVIDER },
        { "&Find...",     FL_CTRL + 'f', cbFind     },
        { "Find &Next",   FL_F + 3,      cbFindNext },
        { "&Replace...",  FL_CTRL + 'h', cbReplace  },
        { "&Go to line...", FL_CTRL + 'g', cbGotoLine },
        { 0 },
    { "&View", 0, 0, 0, FL_SUBMENU },
        /* Alt+Z, not Ctrl+Shift+W: Fl::test_shortcut() only requires META/ALT/
         * CTRL to match exactly and treats SHIFT loosely, so a Ctrl+Shift+W
         * binding lives dangerously next to Ctrl+W (Close). Alt is strict. */
        { "&Word Wrap", FL_ALT + 'z', cbWrap },
        { "Wrap at &Column...", 0, cbWrapCol },
        { 0 },
    { "&Language", 0, 0, 0, FL_SUBMENU },
        { "Plain text", 0, cbLang, (void *)LEX_LANG_TEXT },
        { "Pascal",     0, cbLang, (void *)LEX_LANG_PASCAL },
        { "Lua",        0, cbLang, (void *)LEX_LANG_LUA },
        { "Markdown",   0, cbLang, (void *)LEX_LANG_MARKDOWN },
        { 0 },
    { 0 }
};

/* ---- main -------------------------------------------------------------- */

static void flMsg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    msg("FLTK: %s\n", buf);
}

int main(int argc, char **argv)
{
    int i;

    /* First: DPI awareness must be declared before FLTK opens the display.
     * Widget font is 12 px at 96 DPI (FLTK default is 14), scaled for the
     * display -- same as the level editor. */
    editDpiInit(12);
    codeSetFontSize(editDpi(CODE_FONTSIZE));
#ifdef _WIN32
    /* Code face: Consolas on Vista+ -- heavier and clearer than Courier New at
     * the same size. CODE_FONT is FL_COURIER, so remapping that slot switches
     * the text, the gutter and the wrap-column maths together. Monospaced in
     * all four styles, as the style table requires. Win98 keeps Courier New. */
    if (editDpiAware) {
        Fl::set_font(FL_COURIER,             " Consolas");
        Fl::set_font(FL_COURIER_BOLD,        "BConsolas");
        Fl::set_font(FL_COURIER_ITALIC,      "IConsolas");
        Fl::set_font(FL_COURIER_BOLD_ITALIC, "PConsolas");
    }
#endif

    Fl::error = flMsg;
    Fl::warning = flMsg;
    Fl::scheme("none");                 /* plain FLTK drawing: cheapest on a PII */

    /* -trace writes a step-by-step codeedit.log next to the exe, flushed per
     * line so a hard crash still leaves the last one. Off by default. */
    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-trace") == 0) codeTraceOn = 1;
    if (codeTraceOn) {
        remove("codeedit.log");
        codeTrace("codeedit: start");
    }
    /* ABI sanity. fltk98.bat skips any source whose .o already exists, so a
     * libfltk.a built before the FL_ABI_VERSION 10304 change would disagree
     * with this object about sizeof(Fl_Text_Display) -- silent memory
     * corruption that typically only bites on destruction. */
    codeTrace("abi: codeedit.o sees FL_ABI_VERSION=%d sizeof(Fl_Text_Display)=%d"
              " sizeof(CodeEditor)=%d",
              (int)FL_ABI_VERSION, (int)sizeof(Fl_Text_Display), (int)sizeof(CodeEditor));

    /* Static: Fl_Window::label() stores the pointer without copying. */
    static char winTitle[160];
    sprintf(winTitle, "SOOB Code Editor  v%s  (built %s %s)",
            CODEEDIT_VERSION, __DATE__, __TIME__);

    gWin = new Fl_Double_Window(760, 560, winTitle);
    gWin->color(CODE_COL_BG);
    gWin->begin();
    {
        gMenuBar = new Fl_Menu_Bar(0, 0, 760, MENU_H);
        gMenuBar->menu(gMenu);
        editMenuPad(gMenuBar);

        gTabs = new CodeTabs(0, MENU_H, 760, 560 - MENU_H);
        /* Flat, borderless tabs (edit_tabs.h): the row strip in color(), the
         * active tab in selection_color() -- the editor background, so it
         * merges into the page below -- with a white label. Inactive tabs are
         * bare captions in the child's labelcolor(). */
        gTabs->color(CODE_COL_TABROW);
        gTabs->selection_color(CODE_COL_BG);
        gTabs->labelcolor(CODE_COL_FG);
        gTabs->end();
    }
    gWin->end();
    editDpiScaleTree(gWin);             /* before addDoc: editors are added pre-scaled */
    gWin->resizable(gTabs);
    gWin->callback(cbWinClose);         /* the X button must ask about unsaved work */

    for (i = 1; i < argc; i++)
        if (strcmp(argv[i], "-trace") != 0) addDoc(argv[i]);
    if (gDocCount == 0) addDoc(0);

    /* If libfltk.a is stale (ABI 10300) its linenumber_bgcolor() setter is a
     * no-op and the getter returns a hard-coded 53. Our value surviving proves
     * the library really was rebuilt at 10304. */
    if (gDocCount > 0) {
        /* Runs even without -trace: a mixed-ABI libfltk.a is silent until it
         * crashes in a destructor, so it is worth checking every run. */
        unsigned got = (unsigned)gDocs[0].ed->linenumber_bgcolor();
        codeTrace("abi: linenumber_bgcolor()=0x%08x expected=0x%08x -> libfltk is %s",
                  got, (unsigned)CODE_COL_GUTTER,
                  got == (unsigned)CODE_COL_GUTTER ? "10304 (good)"
                                                   : "STALE 10300 -- CLEAN REBUILD NEEDED");
        if (got != (unsigned)CODE_COL_GUTTER)
            fl_alert("libfltk.a is STALE.\n\n"
                     "It was built before FL_ABI_VERSION became 10304, so it\n"
                     "disagrees with this program about sizeof(Fl_Text_Display).\n\n"
                     "Fix:\n"
                     "  rmdir /s /q vendor\\fltk-1.3\\FL\\lib\n"
                     "  rmdir /s /q raw\\obj\\fltk\n"
                     "  fltk98\n"
                     "  e98");
    }

    Fl::add_timeout(0.4, labelTimer);   /* keeps the " *" dirty marks current */

    /* Build stamp + the actual menu the RUNNING binary has. If "&View" is not
     * in this list, the exe is stale -- e98.bat always recompiles, so that
     * means it was not run, or it failed and the old exe is still there. */
    msg("codeedit: v%s built %s %s\n", CODEEDIT_VERSION, __DATE__, __TIME__);
    {
        int mi, n = gMenuBar->size();
        msg("codeedit: menu has %d entries:", n);
        for (mi = 0; mi < n; mi++)
            msg(" %s", gMenu[mi].text ? gMenu[mi].text : "|");
        msg("\n");
    }

    gWin->show();
    msg("codeedit: %d document(s) open\n", gDocCount);
    return Fl::run();
}
