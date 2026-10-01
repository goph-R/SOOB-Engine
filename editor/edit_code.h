/*
 * edit_code.h -- dark, line-numbered, syntax-highlighting code editor widget.
 *
 * An Fl_Text_Editor subclass wearing the Dpress dark palette (Dracula), with
 * FLTK 1.3's built-in line-number margin and the Pascal / Lua / Markdown
 * grammars from edit_lex.h.  Intended as the embedded script editor in the
 * SOOB level editor, so it stays a plain widget with no engine dependencies.
 *
 * WHY IT IS FAST ENOUGH FOR A PENTIUM II
 *
 * The naive FLTK editor-demo approach re-parses from the edit to end-of-buffer
 * on every keystroke.  Instead, each line's carry-out lexer state is stored in
 * the style byte of that line's '\n'.  That byte is free real estate:
 * Fl_Text_Display::position_style() returns FILL_MASK for any index at or past
 * the line length, so a newline's style byte is never read for drawing, and
 * out-of-range style indices are clamped (Fl_Text_Display.cxx:2215, :2491)
 * rather than read out of bounds.
 *
 * So styleUpdate() relexes forward from the edited line and STOPS as soon as a
 * recomputed carry state matches the one already stored.  Typing normal code
 * relexes one line.  Typing "{" in Pascal relexes down to the closing brace and
 * stops there.  Only that range is pushed to the style buffer and redisplayed.
 *
 * Wrapping is OFF by default, and that is a measured decision rather than a
 * preference (wrapEnable()/wrapColumn() turn it on).
 * With mContinuousWrap set, a keystroke that changes how many display rows a
 * paragraph occupies makes Fl_Text_Display take its linesInserted !=
 * linesDeleted branch and set endDispPos = mLastChar (Fl_Text_Display.cxx
 * :1793) -- i.e. repaint the whole widget, scrollbars included, on every
 * character typed.  On a Pentium II that is plainly visible.  Reading and
 * scrolling are unaffected, which is why it is offered at all: long prose lines
 * in a document you are not editing cost nothing.
 *
 * USAGE
 *     CodeEditor *ed = new CodeEditor(0, 0, 640, 480);
 *     ed->language(LEX_LANG_LUA);
 *     ed->loadFile("scripts/main.lua");
 */
#ifndef EDIT_CODE_H
#define EDIT_CODE_H

#include <FL/Fl.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/fl_draw.H>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "edit_lex.h"
#include "edit_fileio.h"

/* ---- crash-localising trace -------------------------------------------
 * Appends and CLOSES on every call, so a hard crash on the target still leaves
 * a complete log behind -- buffered stdout would lose the last and most
 * interesting line. Off unless codeTraceOn is set; codeedit.cpp turns it on
 * with -trace. This found the mixed-ABI libfltk.a crash by showing that
 * ~CodeEditor completed and the fault was in a base destructor. */
static int codeTraceOn = 0;

static void codeTrace(const char *fmt, ...)
{
    FILE *f;
    va_list ap;
    if (!codeTraceOn) return;
    f = fopen("codeedit.log", "a");
    if (!f) return;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* Fl_Color already encodes a literal RGB when the low byte is zero, so this is
 * a compile-time constant and usable in a static initialiser (fl_rgb_color()
 * is not, being a function). */
#define CODE_RGB(r, g, b) \
    ((Fl_Color)(((unsigned)(r) << 24) | ((unsigned)(g) << 16) | ((unsigned)(b) << 8)))

/* Dpress dark theme (Dracula). Values lifted from assets/admin.css :root. */
#define CODE_COL_BG        CODE_RGB(0x28, 0x2a, 0x36)
#define CODE_COL_GUTTER    CODE_RGB(0x38, 0x3b, 0x4c)  /* line-number margin: a clear
                                                        * step up from the canvas
                                                        * (1.29:1) without shouting */
#define CODE_COL_LINENO    CODE_RGB(0x9a, 0xa3, 0xc9)  /* a lighter tint of the comment
                                                        * blue; 4.46:1 on the margin, so
                                                        * it reads on a CRT yet stays
                                                        * subordinate to the code */
#define CODE_COL_FG        CODE_RGB(0xf8, 0xf8, 0xf2)
#define CODE_COL_SEL       CODE_RGB(0x44, 0x47, 0x5a)
#define CODE_COL_COMMENT   CODE_RGB(0x62, 0x72, 0xa4)
#define CODE_COL_KEYWORD   CODE_RGB(0xff, 0x79, 0xc6)
#define CODE_COL_STRING    CODE_RGB(0xf1, 0xfa, 0x8c)
#define CODE_COL_NUMBER    CODE_RGB(0xff, 0xb8, 0x6c)
#define CODE_COL_TYPE      CODE_RGB(0xbd, 0x93, 0xf9)
#define CODE_COL_IDENT     CODE_RGB(0x8b, 0xe9, 0xfd)
#define CODE_COL_CODE      CODE_RGB(0x50, 0xfa, 0x7b)
#define CODE_COL_ERROR     CODE_RGB(0xff, 0x55, 0x55)
/* Tab row strip (CodeTabs, edit_tabs.h): half-way between the canvas and the
 * line-number margin, so the row reads as chrome without competing with the
 * gutter. The active tab is drawn in CODE_COL_BG and merges into the page. */
#define CODE_COL_TABROW    CODE_RGB(0x30, 0x32, 0x41)
/* Inactive tab caption. CodeTabs, like Fl_Tabs, draws the SELECTED tab's label
 * in the tabs widget's own labelcolor() and every other tab's in that child's
 * labelcolor(), so setting both once is enough -- no tab-change callback
 * needed. Inactive captions sit straight on CODE_COL_TABROW. */
#define CODE_COL_TAB_OFF   CODE_RGB(0xa0, 0xa4, 0xb0)
/* Column ruler. Dpress draws its margin guide in the marker colour, dotted. */
#define CODE_COL_RULER     CODE_RGB(0x62, 0x72, 0xa4)

#ifndef CODE_FONTSIZE
#define CODE_FONTSIZE 14
#endif
/* Must stay fixed-pitch: the column-wrap maths and the line-number margin both
 * assume every glyph is the same width. */
#ifndef CODE_FONT
#define CODE_FONT FL_COURIER
#endif
/* Caret blink half-period in seconds (Windows' default caret blink is 530 ms). */
#ifndef CODE_CARET_BLINK
#define CODE_CARET_BLINK 0.53
#endif

/* Order MUST match the LEX_* slot enum in edit_lex.h.  Courier New's bold and
 * italic faces share the regular advance width, so mixing them here does not
 * break Fl_Text_Display's column maths. */
static Fl_Text_Display::Style_Table_Entry codeStyleTable[LEX_NSTYLES] = {
    /* A PLAIN     */ { CODE_COL_FG,      CODE_FONT,         CODE_FONTSIZE, 0 },
    /* B COMMENT   */ { CODE_COL_COMMENT, CODE_FONT,         CODE_FONTSIZE, 0 },
    /* C KEYWORD   */ { CODE_COL_KEYWORD, CODE_FONT,         CODE_FONTSIZE, 0 },
    /* D STRING    */ { CODE_COL_STRING,  CODE_FONT,         CODE_FONTSIZE, 0 },
    /* E NUMBER    */ { CODE_COL_NUMBER,  CODE_FONT,         CODE_FONTSIZE, 0 },
    /* F TYPE      */ { CODE_COL_TYPE,    CODE_FONT,         CODE_FONTSIZE, 0 },
    /* G IDENT     */ { CODE_COL_IDENT,   CODE_FONT,         CODE_FONTSIZE, 0 },
    /* H CODE      */ { CODE_COL_CODE,    CODE_FONT,         CODE_FONTSIZE, 0 },
    /* I MARKER    */ { CODE_COL_COMMENT, CODE_FONT,         CODE_FONTSIZE, 0 },
    /* J ERROR     */ { CODE_COL_ERROR,   CODE_FONT,         CODE_FONTSIZE, 0 },
    /* K DIRECTIVE */ { CODE_COL_NUMBER,  CODE_FONT,         CODE_FONTSIZE, 0 },
    /* L STRONG    */ { CODE_COL_NUMBER,  FL_COURIER_BOLD,   CODE_FONTSIZE, 0 },
    /* M EM        */ { CODE_COL_STRING,  FL_COURIER_ITALIC, CODE_FONTSIZE, 0 }
};

/* Indent width in spaces: what Tab inserts, Shift+Tab removes, and a '{' at
 * the end of a line adds. Also the display width of a literal tab character.
 * Runtime so a settings window can change it later; set it before the first
 * CodeEditor is constructed. */
#ifndef CODE_INDENT
#define CODE_INDENT 4
#endif
static int codeIndent = CODE_INDENT;
/* Tab / auto-indent insert tab characters instead of spaces. */
static int codeUseTabs = 0;
/* Show the line-number gutter. */
static int codeLineNumbers = 1;

/* Runtime code font size -- CODE_FONTSIZE scaled for the display. Set it with
 * codeSetFontSize() BEFORE the first CodeEditor is constructed; existing
 * editors keep the size they were built with. */
static int codeFontSize = CODE_FONTSIZE;

/* Line-number gutter width: 48 px at the default size, scaled with the font. */
static int codeGutterWidth(void) { return 48 * codeFontSize / CODE_FONTSIZE; }
static void codeSetFontSize(int px)
{
    int i;
    codeFontSize = px;
    for (i = 0; i < LEX_NSTYLES; i++) codeStyleTable[i].size = px;
}

/* ---- model layer -------------------------------------------------------
 * These operate on a text buffer + its parallel style buffer and nothing
 * else.  Fl_Text_Buffer is pure model (it never opens a display), so this is
 * the part that can be unit-tested headlessly -- which matters, because the
 * incremental rule below is the only genuinely subtle code here.
 */

/* Re-lex the whole buffer.  O(file); only for load or a language switch. */
static void codeStyleAll(Fl_Text_Buffer *textBuf, Fl_Text_Buffer *styleBuf, int lang)
{
    int total = textBuf->length();
    char *txt = textBuf->text();
    char *sty = (char *)malloc(total + 1);
    int p = 0, st = LS_NORMAL;

    if (!txt || !sty) { free(txt); free(sty); return; }
    while (p <= total) {
        int e = p;
        while (e < total && txt[e] != '\n') e++;
        st = lexLine(lang, st, txt + p, e - p, sty + p);
        if (e < total) { sty[e] = (char)('A' + st); p = e + 1; }
        else { p = e; break; }
    }
    sty[total] = '\0';
    styleBuf->text(sty);
    free(sty);
    free(txt);
}

/* Incremental restyle after an edit at `pos`.  Keeps styleBuf the same length
 * as textBuf, then relexes forward from the edited line and STOPS as soon as a
 * recomputed carry state matches the one already stored on a line's newline.
 *
 * Returns 1 and fills outStart and outEnd with the restyled range, or 0 if
 * nothing needed doing.  The result must always be identical to what
 * codeStyleAll() would produce -- edit_code_model_test.cpp asserts exactly
 * that after every kind of edit.
 */
static int codeStyleUpdate(Fl_Text_Buffer *textBuf, Fl_Text_Buffer *styleBuf,
                           int lang, int pos, int nInserted, int nDeleted,
                           int *outStart, int *outEnd)
{
    int start, st, changedEnd, total, p, cap, len;
    char *out;

    if (nInserted == 0 && nDeleted == 0) { styleBuf->unselect(); return 0; }

    /* 1. Keep the style buffer byte-for-byte as long as the text. */
    if (nDeleted > 0) styleBuf->remove(pos, pos + nDeleted);
    if (nInserted > 0) {
        char *fill = (char *)malloc(nInserted + 1);
        if (!fill) return 0;
        memset(fill, LEX_CH(LEX_PLAIN), nInserted);
        fill[nInserted] = '\0';
        styleBuf->insert(pos, fill);
        free(fill);
    }
    if (styleBuf->length() != textBuf->length()) {   /* should not happen */
        codeStyleAll(textBuf, styleBuf, lang);
        *outStart = 0;
        *outEnd = textBuf->length();
        return 1;
    }

    /* 2. Relex from the edited line until the carry state reconverges. */
    total      = textBuf->length();
    changedEnd = pos + nInserted;
    start      = textBuf->line_start(pos);
    st = (start > 0) ? (int)(unsigned char)styleBuf->byte_at(start - 1) - 'A'
                     : LS_NORMAL;
    if (st < 0 || st >= LS_MAX) st = LS_NORMAL;

    cap = 512;
    len = 0;
    out = (char *)malloc(cap);
    if (!out) return 0;

    p = start;
    while (p <= total) {
        int e = textBuf->line_end(p);
        int n = e - p;

        /* Past the edit and the stored carry state still agrees?  Then every
         * following line already has correct styles. */
        if (p > changedEnd) {
            int old = (p > 0) ? (int)(unsigned char)styleBuf->byte_at(p - 1) - 'A'
                              : LS_NORMAL;
            if (old == st) break;
        }

        if (len + n + 2 > cap) {
            char *grown;
            while (len + n + 2 > cap) cap *= 2;
            grown = (char *)realloc(out, cap);
            if (!grown) { free(out); return 0; }
            out = grown;
        }

        if (n > 0) {
            char *txt = textBuf->text_range(p, e);
            if (!txt) { free(out); return 0; }
            st = lexLine(lang, st, txt, n, out + len);
            free(txt);
            len += n;
        } else {
            st = lexLine(lang, st, "", 0, out + len);
        }

        if (e < total) { out[len++] = (char)('A' + st); p = e + 1; }
        else           { p = e; break; }
    }

    if (len > 0) {
        out[len] = '\0';
        styleBuf->replace(start, start + len, out);
        *outStart = start;
        *outEnd   = start + len;
        free(out);
        return 1;
    }
    free(out);
    return 0;
}

/* ---- undo stack --------------------------------------------------------
 * FLTK's own undo is unusable for a tabbed editor: Fl_Text_Buffer's undo state
 * is a set of FILE-SCOPE STATICS in Fl_Text_Buffer.cxx -- one undobuffer and a
 * single `undowidget` naming its owner -- so it is single-level AND global, and
 * undo() opens with `if (undowidget != this ...) return 0`, meaning switching
 * tabs silently discards the undo history of the tab you left.
 *
 * So we keep our own. Like the styling layer above this operates on an
 * Fl_Text_Buffer and nothing else, which keeps it unit-testable with no display
 * (edit_code_model_test.cpp).
 *
 * Replaying goes back through Fl_Text_Buffer::insert/remove, so it fires the
 * same modify callback as typing and codeStyleUpdate() re-highlights the
 * affected range for free -- no special casing. The `replaying` flag is what
 * stops the replay being recorded as a fresh edit.
 */

#define CODE_UNDO_MAX 256          /* records; oldest is dropped past this */

typedef struct CodeUndoRec {
    int   pos;
    char *deleted;                 /* text removed at pos, or NULL */
    char *inserted;                /* text added at pos, or NULL   */
    int   group;                   /* 0 = standalone; else undo/redo as a unit */
} CodeUndoRec;

typedef struct CodeUndo {
    CodeUndoRec *rec;
    int count;                     /* records held                          */
    int at;                        /* done records; redo is [at, count)     */
    int cap;
    int replaying;                 /* re-entrancy guard, see above          */
    int noCoalesce;                /* set after undo/redo to break a run    */
    int curGroup;                  /* nonzero while inside begin/endGroup   */
    int groupSeq;                  /* hands out group ids                   */
} CodeUndo;

static char *codeStrDup(const char *s, int n)
{
    char *d;
    if (!s || n < 0) return 0;
    d = (char *)malloc((size_t)n + 1);
    if (!d) return 0;
    if (n) memcpy(d, s, (size_t)n);
    d[n] = '\0';
    return d;
}

static void codeUndoInit(CodeUndo *u)
{
    u->rec = 0; u->count = 0; u->at = 0; u->cap = 0;
    u->replaying = 0; u->noCoalesce = 0;
    u->curGroup = 0; u->groupSeq = 0;
}

static void codeUndoClear(CodeUndo *u)
{
    int i;
    for (i = 0; i < u->count; i++) {
        free(u->rec[i].deleted);
        free(u->rec[i].inserted);
    }
    u->count = 0; u->at = 0; u->noCoalesce = 0; u->curGroup = 0;
}

/* Bracket a compound operation -- Replace All, say -- so the whole thing is a
 * single Ctrl+Z rather than one step per match. */
static void codeUndoBeginGroup(CodeUndo *u)
{
    u->curGroup = ++u->groupSeq;
    u->noCoalesce = 1;
}
static void codeUndoEndGroup(CodeUndo *u)
{
    u->curGroup = 0;
    u->noCoalesce = 1;
}

static void codeUndoFree(CodeUndo *u)
{
    codeUndoClear(u);
    free(u->rec);
    u->rec = 0; u->cap = 0;
}

/* Record one buffer modification. `deletedText` is FLTK's callback pointer and
 * does NOT outlive the call, so it is copied. The inserted text is not handed
 * to the callback at all -- it is read back out of the buffer. */
static void codeUndoRecord(CodeUndo *u, Fl_Text_Buffer *buf, int pos,
                           int nInserted, int nDeleted, const char *deletedText)
{
    CodeUndoRec *r;
    char *ins = 0, *del = 0;
    int i;

    if (u->replaying) return;                       /* replay is not an edit */
    if (nInserted == 0 && nDeleted == 0) return;

    if (nInserted > 0) ins = buf->text_range(pos, pos + nInserted);
    if (nDeleted  > 0) del = codeStrDup(deletedText, nDeleted);

    /* A new edit invalidates anything that was redoable. */
    for (i = u->at; i < u->count; i++) {
        free(u->rec[i].deleted);
        free(u->rec[i].inserted);
    }
    u->count = u->at;

    /* Coalesce, so a burst of typing is one undo step rather than one per key.
     * Three runs are worth merging: forward typing, backspace, and Delete. */
    if (!u->noCoalesce && u->count > 0) {
        r = &u->rec[u->count - 1];

        /* typing: single char continuing a pure-insert run, not past a newline */
        if (ins && !del && nInserted == 1 && r->inserted && !r->deleted) {
            int rl = (int)strlen(r->inserted);
            if (r->pos + rl == pos && ins[0] != '\n' && (rl == 0 || r->inserted[rl-1] != '\n')) {
                char *grown = (char *)realloc(r->inserted, (size_t)rl + 2);
                if (grown) {
                    grown[rl] = ins[0]; grown[rl + 1] = '\0';
                    r->inserted = grown;
                    free(ins); free(del);
                    return;
                }
            }
        }
        /* backspace: single char removed immediately before a pure-delete run */
        if (del && !ins && nDeleted == 1 && r->deleted && !r->inserted) {
            int rl = (int)strlen(r->deleted);
            if (pos + 1 == r->pos) {
                char *grown = (char *)malloc((size_t)rl + 2);
                if (grown) {
                    grown[0] = del[0];
                    memcpy(grown + 1, r->deleted, (size_t)rl + 1);
                    free(r->deleted);
                    r->deleted = grown;
                    r->pos = pos;
                    free(ins); free(del);
                    return;
                }
            }
            /* Delete key: removes at the same position repeatedly */
            if (pos == r->pos) {
                char *grown = (char *)realloc(r->deleted, (size_t)rl + 2);
                if (grown) {
                    grown[rl] = del[0]; grown[rl + 1] = '\0';
                    r->deleted = grown;
                    free(ins); free(del);
                    return;
                }
            }
        }
    }
    u->noCoalesce = 0;

    if (u->count == u->cap) {
        int ncap = u->cap ? u->cap * 2 : 32;
        CodeUndoRec *nr;
        if (ncap > CODE_UNDO_MAX) ncap = CODE_UNDO_MAX;
        if (u->count == ncap) {                     /* full: drop the oldest */
            free(u->rec[0].deleted);
            free(u->rec[0].inserted);
            memmove(u->rec, u->rec + 1, sizeof(CodeUndoRec) * (size_t)(u->count - 1));
            u->count--;
            if (u->at > 0) u->at--;
        } else {
            nr = (CodeUndoRec *)realloc(u->rec, sizeof(CodeUndoRec) * (size_t)ncap);
            if (!nr) { free(ins); free(del); return; }
            u->rec = nr; u->cap = ncap;
        }
    }

    r = &u->rec[u->count++];
    r->pos = pos;
    r->inserted = ins;
    r->deleted  = del;
    r->group    = u->curGroup;
    u->at = u->count;
}

/* Undo one record. Returns 1 and writes where the caret belongs, else 0. */
static int codeUndoUndo(CodeUndo *u, Fl_Text_Buffer *buf, int *caret)
{
    CodeUndoRec *r;
    int g;
    if (u->at <= 0) return 0;
    g = u->rec[u->at - 1].group;
    u->replaying = 1;
    do {
        r = &u->rec[--u->at];
        if (r->inserted) buf->remove(r->pos, r->pos + (int)strlen(r->inserted));
        if (r->deleted)  buf->insert(r->pos, r->deleted);
        if (caret) *caret = r->deleted ? r->pos + (int)strlen(r->deleted) : r->pos;
    } while (g != 0 && u->at > 0 && u->rec[u->at - 1].group == g);
    u->replaying = 0;
    u->noCoalesce = 1;
    return 1;
}

/* Redo one record. Returns 1 and writes where the caret belongs, else 0. */
static int codeUndoRedo(CodeUndo *u, Fl_Text_Buffer *buf, int *caret)
{
    CodeUndoRec *r;
    int g;
    if (u->at >= u->count) return 0;
    g = u->rec[u->at].group;
    u->replaying = 1;
    do {
        r = &u->rec[u->at++];
        if (r->deleted)  buf->remove(r->pos, r->pos + (int)strlen(r->deleted));
        if (r->inserted) buf->insert(r->pos, r->inserted);
        if (caret) *caret = r->inserted ? r->pos + (int)strlen(r->inserted) : r->pos;
    } while (g != 0 && u->at < u->count && u->rec[u->at].group == g);
    u->replaying = 0;
    u->noCoalesce = 1;
    return 1;
}

class CodeEditor : public Fl_Text_Editor {
public:
    CodeEditor(int X, int Y, int W, int H, const char *l = 0)
        : Fl_Text_Editor(X, Y, W, H, l)
    {
        mLang     = LEX_LANG_TEXT;
        mWrapCol  = 0;
        mWrapPx   = 0;
        mWrapOn   = 0;
        mTextBuf  = new Fl_Text_Buffer();
        mStyleBuf = new Fl_Text_Buffer();

        buffer(mTextBuf);
        mTextBuf->tab_distance(codeIndent);
        wrap_mode(WRAP_NONE, 0);
        textfont(CODE_FONT);
        textsize(codeFontSize);

        box(FL_FLAT_BOX);
        color(CODE_COL_BG);
        textcolor(CODE_COL_FG);
        cursor_color(CODE_COL_FG);
        selection_color(CODE_COL_SEL);
        show_cursor(0);             /* FLTK's caret off -- draw() paints ours */
        mCaretOn = 0;
        mLineNumLines = -1;
        mMaxCols = 0;
        mCharPx  = 0;
        mEnc     = CODE_ENC_UTF8;
        mCrlf    = CODE_DEFAULT_CRLF;
        fl_text_display_longest_line = longestLineHook;   /* see trackLongest() */

        linenumber_width(codeLineNumbers ? codeGutterWidth() : 0);
        linenumber_font(CODE_FONT);
        linenumber_size(codeFontSize);
        linenumber_bgcolor(CODE_COL_GUTTER);
        linenumber_fgcolor(CODE_COL_LINENO);
        linenumber_align(FL_ALIGN_RIGHT);

        /* Scrollbars deliberately left at the FLTK default -- they read as
         * part of the window chrome, not the document. */

        /* No "unfinished style" mechanism: we keep the style buffer complete at
         * all times, so pass slot 0 (never emitted -- LEX_PLAIN is 'A') and a
         * NULL callback, which position_style() checks for. */
        highlight_data(mStyleBuf, codeStyleTable, LEX_NSTYLES, 0, 0, 0);

        /* Our own undo replaces FLTK's; switch theirs off so the two cannot
         * fight over the same Ctrl+Z. See the CodeUndo comment above. */
        mTextBuf->canUndo(0);
        codeUndoInit(&mUndo);
        mDirty = 0;

        mTextBuf->add_modify_callback(staticModifyCb, this);
    }

    ~CodeEditor()
    {
        codeTrace("  ~CodeEditor %p enter", (void *)this);
        Fl::remove_timeout(caretBlinkCb, this);   /* it holds a raw `this` */
        mTextBuf->remove_modify_callback(staticModifyCb, this);
        codeTrace("  ~CodeEditor removed modify cb");
        /* buffer(0) is NULL-safe and detaches the display's own callbacks.
         * highlight_data(0, ...) is NOT -- it does mStyleBuffer->canUndo(0)
         * unconditionally (Fl_Text_Display.cxx) -- so never call it with NULL.
         * ~Fl_Text_Display touches mBuffer only, so leaving mStyleBuffer
         * dangling here is safe; nothing draws after this point. */
        buffer(0);
        codeTrace("  ~CodeEditor buffer(0) done");
        codeUndoFree(&mUndo);
        codeTrace("  ~CodeEditor undo freed");
        delete mTextBuf;
        codeTrace("  ~CodeEditor text buffer deleted");
        delete mStyleBuf;
        codeTrace("  ~CodeEditor exit; base dtors next");
    }

    void language(int lang)
    {
        if (lang == mLang) return;
        mLang = lang;
        rehighlightAll();
    }
    int language() const { return mLang; }

    /* ---- word wrap ----------------------------------------------------
     * Off by default: see the note at the top of this file -- with wrapping on,
     * FLTK repaints the whole widget on any keystroke that re-flows a
     * paragraph, and the line-number margin stops being 1:1 with screen rows.
     *
     * The wrap COLUMN is remembered independently of whether wrapping is
     * currently on, so toggling off and back on keeps your column. 0 means
     * "wrap to the window", FLTK's WRAP_AT_BOUNDS.
     *
     * NOT WRAP_AT_COLUMN: that converts the column with col_to_x(), which
     * returns col * mColumnScale, and mColumnScale is a lazily-measured font
     * width that textfont(), textsize() and highlight_data() all reset to 0.
     * When it measures as 0 the margin becomes 0 -- which wrapped_line_counter
     * reads as "no margin" and falls back to text_area.w, i.e. silently wraps
     * to the window and ignores the column entirely.
     *
     * So measure the character width here and use WRAP_AT_PIXEL, which takes
     * the value verbatim. Exact for CODE_FONT, which is fixed-pitch Courier. */
    void wrapEnable(int on)
    {
        mWrapOn = on ? 1 : 0;
        if (!mWrapOn)          wrap_mode(WRAP_NONE, 0);
        else if (mWrapCol > 0) wrap_mode(WRAP_AT_PIXEL, wrapPixels());
        else                   wrap_mode(WRAP_AT_BOUNDS, 0);
        codeTrace("wrap: on=%d col=%d px=%d continuous=%d marginPix=%d",
                  mWrapOn, mWrapCol, mWrapPx, mContinuousWrap, mWrapMarginPix);
    }
    void wrapColumn(int col)              /* 0 = wrap to the window width */
    {
        mWrapCol = col > 0 ? col : 0;
        mWrapPx  = 0;                     /* force a re-measure */
        if (mWrapOn) wrapEnable(1);       /* re-apply immediately */
        redraw();                         /* the ruler moves with it */
    }
    int  wrapped()    const { return mContinuousWrap; }
    int  wrapColumn() const { return mWrapCol; }

    /* Pixel offset of the wrap column, measured once and cached -- draw() would
     * otherwise do a font measurement on every repaint. */
    int wrapPixels()
    {
        if (mWrapCol <= 0) return 0;
        if (mWrapPx <= 0) {
            fl_font(CODE_FONT, codeFontSize);
            mWrapPx = (int)(fl_width("M") * (double)mWrapCol + 0.5);
            if (mWrapPx < 16) mWrapPx = 16;       /* never collapse to nothing */
        }
        return mWrapPx;
    }

    /* A dotted guide at the wrap column, like the one in Dpress.
     *
     * Drawn after the text so it sits on top, and clipped to text_area so it
     * cannot bleed into the line-number margin or the scrollbars. It follows
     * the horizontal scroll, and is shown whenever a column is set -- wrapping
     * on or off -- since it is a useful margin guide either way. */
    void draw()
    {
        int x;
        Fl_Text_Editor::draw();
        /* Our Win98 patch in Fl_Text_Display::draw() repaints the line-number
         * margin only when the top visible line changes (or on a full redraw).
         * That misses Enter / Backspace in a document shorter than the view:
         * a number appears or disappears with no scroll, and the margin stayed
         * stale until the next scroll. Repaint it whenever the line count
         * changes too -- still never on ordinary typing within a line. */
        if (mNBufferLines != mLineNumLines) {
            mLineNumLines = mNBufferLines;
            draw_line_numbers(true);
        }
        caretDraw();
        if (mWrapCol <= 0) return;

        x = text_area.x + wrapPixels() - mHorizOffset;
        if (x < text_area.x || x >= text_area.x + text_area.w) return;

        fl_push_clip(text_area.x, text_area.y, text_area.w, text_area.h);
        fl_color(CODE_COL_RULER);
        fl_line_style(FL_DOT, 1);
        fl_line(x, text_area.y, x, text_area.y + text_area.h - 1);
        fl_line_style(0);          /* MUST reset -- it is global GDI pen state */
        fl_pop_clip();
    }

    /* Load a file and pick the language from its extension. The encoding and
     * line ending are detected and kept for saving (edit_fileio.h).
     * Returns 0 on success, like Fl_Text_Buffer::loadfile(). */
    int loadFile(const char *path)
    {
        int enc, crlf;
        char *t = codeReadText(path, &enc, &crlf);
        if (!t) return 1;
        mLang = lexLangFromPath(path);
        mTextBuf->text(t);
        free(t);
        rehighlightAll();
        clearUndo();
        mDirty = 0;
        mEnc   = enc;
        mCrlf  = crlf;
        return 0;
    }

    int saveFile(const char *path)
    {
        char *t = mTextBuf->text();
        int r = codeWriteText(path, t, (int)strlen(t), mEnc, mCrlf);
        free(t);
        if (r == 0) mDirty = 0;
        return r;
    }

    /* Encoding / line ending used on save. Changing either marks the file
     * modified, as the next save converts it. */
    int  encoding() const { return mEnc; }
    void encoding(int e)  { if (e != mEnc) { mEnc = e; mDirty = 1; } }
    int  crlf() const     { return mCrlf; }
    void crlf(int c)      { c = c ? 1 : 0; if (c != mCrlf) { mCrlf = c; mDirty = 1; } }
    int  ansiLossy()
    {
        char *t = mTextBuf->text();
        int r = codeAnsiIsLossy(t, (int)strlen(t));
        free(t);
        return r;
    }

    /* Re-apply the look after the settings changed (font size, tab width,
     * line numbers). resize() is where FLTK recomputes the line height. */
    void applySettings()
    {
        textsize(codeFontSize);
        linenumber_size(codeFontSize);
        linenumber_width(codeLineNumbers ? codeGutterWidth() : 0);
        mTextBuf->tab_distance(codeIndent);
        mCharPx = 0;
        mWrapPx = 0;
        rescanLongest();
        if (mWrapOn) wrapEnable(1);
        resize(x(), y(), w(), h());
        redraw();
    }

    /* Caret line (1-based) and display column (1-based, tabs expanded).
     * Counts from the first visible line when it can, so it stays cheap on a
     * long file -- the status bar asks after every event. */
    void caretLineCol(int *line, int *col)
    {
        int pos = insert_position();
        int top = get_absolute_top_line_number();
        *col = mTextBuf->count_displayed_characters(mTextBuf->line_start(pos), pos) + 1;
        if (top > 0 && pos >= mFirstChar)
            *line = top + mTextBuf->count_lines(mFirstChar, pos);
        else
            *line = mTextBuf->count_lines(0, pos) + 1;
    }

    void text(const char *s)
    {
        mTextBuf->text(s ? s : "");
        rehighlightAll();
        clearUndo();
        mDirty = 0;
    }
    char *text() const { return mTextBuf->text(); }   /* caller free()s */

    /* The parallel style buffer as a string, one 'A'+slot byte per text byte
     * (newline positions carry 'A'+state instead). Caller free()s. Mainly for
     * tests: the incremental path must always agree with rehighlightAll(). */
    char *styleText() const { return mStyleBuf->text(); }

    Fl_Text_Buffer *textBuffer() const { return mTextBuf; }

    /* ---- undo / redo (ours, not FLTK's -- see the CodeUndo comment) ---- */
    int canUndo() const { return mUndo.at > 0; }
    int canRedo() const { return mUndo.at < mUndo.count; }
    void clearUndo() { codeUndoClear(&mUndo); }
    void beginUndoGroup() { codeUndoBeginGroup(&mUndo); }
    void endUndoGroup()   { codeUndoEndGroup(&mUndo); }

    void undo()
    {
        int caret = 0;
        if (!codeUndoUndo(&mUndo, mTextBuf, &caret)) return;
        insert_position(caret);
        show_insert_position();
    }
    void redo()
    {
        int caret = 0;
        if (!codeUndoRedo(&mUndo, mTextBuf, &caret)) return;
        insert_position(caret);
        show_insert_position();
    }

    /* ---- dirty tracking, for tab captions and close prompts ---- */
    int  dirty() const { return mDirty; }
    void clearDirty() { mDirty = 0; }

    /* Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z, intercepted before Fl_Text_Editor's own
     * key bindings so its kf_undo (which drives FLTK's dead undo) never runs.
     * Any key or click also restarts the caret blink, so the caret is always
     * solid while you type or move it -- the way native editors behave. */
    int handle(int e)
    {
        int r;
        if (e == FL_KEYBOARD) {
            int st = Fl::event_state();
            int k  = Fl::event_key();
            if (st & FL_CTRL) {
                if (k == 'z') { if (st & FL_SHIFT) redo(); else undo(); caretRestart(); return 1; }
                if (k == 'y') { redo(); caretRestart(); return 1; }
            }
            if (!(st & (FL_CTRL | FL_ALT | FL_META))) {
                if (k == FL_Enter || k == FL_KP_Enter) {
                    indentNewline(); caretRestart(); return 1;
                }
                if (k == FL_Tab) {
                    if (st & FL_SHIFT) shiftLines(-1); else indentTab();
                    caretRestart(); return 1;
                }
            }
            /* By the character, not the key: '}' is AltGr+B on a Hungarian
             * layout, which Windows reports as Ctrl+Alt. */
            if (Fl::event_length() == 1 && Fl::event_text()[0] == '}' &&
                closeBraceIndent()) {
                r = Fl_Text_Editor::handle(e);      /* types the brace */
                endUndoGroup();
                caretRestart();
                return r;
            }
        }
        r = Fl_Text_Editor::handle(e);
        switch (e) {
        case FL_FOCUS: case FL_KEYBOARD: case FL_PUSH: case FL_DRAG:
            caretRestart();
            break;
        case FL_UNFOCUS:
            caretStop();
            break;
        }
        return r;
    }

    /* ---- indentation -----------------------------------------------------
     * Enter keeps the current line's indent, plus one level after a line
     * ending in '{'. Tab inserts spaces to the next indent stop; with a
     * selection spanning lines, Tab / Shift+Tab indent / outdent every line.
     * Typing '}' on an otherwise blank line re-indents it to the line holding
     * the matching '{'. Braces inside comments and strings (per the style
     * buffer) are ignored -- which also keeps Pascal's { comments } inert.
     * Every edit is one undo group. */
    char ch(int p) const
    {
        return (p >= 0 && p < mTextBuf->length()) ? mTextBuf->byte_at(p) : 0;
    }
    int inCommentOrString(int p) const
    {
        int slot = mStyleBuf->byte_at(p) - 'A';
        return slot == LEX_COMMENT || slot == LEX_STRING || slot == LEX_DIRECTIVE;
    }
    int indentEnd(int ls) const       /* first non-blank at/after line start */
    {
        while (ch(ls) == ' ' || ch(ls) == '\t') ls++;
        return ls;
    }
    void killSelection()
    {
        int s, e;
        if (mTextBuf->selection_position(&s, &e)) {
            mTextBuf->remove_selection();
            insert_position(s);
        }
    }
    static void spaces(char *buf, int n)   /* buf: at least 64 bytes */
    {
        if (n > 63) n = 63;
        memset(buf, ' ', n);
        buf[n] = '\0';
    }

    void indentNewline()
    {
        int pos, ls, we, p, open;
        char *ind, pad[64];
        beginUndoGroup();
        killSelection();
        pos = insert_position();
        ls  = mTextBuf->line_start(pos);
        we  = indentEnd(ls);
        if (we > pos) we = pos;           /* Enter inside the indent keeps what is left of it */
        p = pos;
        while (p > ls && (ch(p - 1) == ' ' || ch(p - 1) == '\t')) p--;
        open = p > ls && ch(p - 1) == '{' && !inCommentOrString(p - 1);
        ind = mTextBuf->text_range(ls, we);
        insert("\n");
        insert(ind);
        free(ind);
        if (open) { if (codeUseTabs) insert("\t"); else { spaces(pad, codeIndent); insert(pad); } }
        endUndoGroup();
        show_insert_position();
    }

    void indentTab()
    {
        int s, e, pos, col;
        char pad[64];
        if (mTextBuf->selection_position(&s, &e) &&
            mTextBuf->line_start(s) != mTextBuf->line_start(e)) {
            shiftLines(1);
            return;
        }
        beginUndoGroup();
        killSelection();
        pos = insert_position();
        col = mTextBuf->count_displayed_characters(mTextBuf->line_start(pos), pos);
        if (codeUseTabs) insert("\t");
        else { spaces(pad, codeIndent - col % codeIndent); insert(pad); }
        endUndoGroup();
        show_insert_position();
    }

    /* dir > 0: indent, dir < 0: outdent -- the selected lines, or the caret's
     * line without a selection. A selection ending at column 0 does not take
     * that last line along. Outdent removes one tab or up to codeIndent spaces. */
    void shiftLines(int dir)
    {
        int s, e, sel, first, last, n, i, ls, k, pos, firstCut = 0;
        char pad[64];
        sel = mTextBuf->selection_position(&s, &e);
        pos = insert_position();
        if (!sel) s = e = pos;
        first = mTextBuf->line_start(s);
        last  = mTextBuf->line_start(e);
        if (sel && last == e && last > first) last = mTextBuf->line_start(e - 1);
        n = mTextBuf->count_lines(first, last) + 1;

        beginUndoGroup();
        for (i = 0, ls = first; i < n; i++) {
            if (dir > 0) {
                if (mTextBuf->line_end(ls) > ls) {        /* leave blank lines blank */
                    if (codeUseTabs) mTextBuf->insert(ls, "\t");
                    else { spaces(pad, codeIndent); mTextBuf->insert(ls, pad); }
                }
            } else {
                k = 0;
                if (ch(ls) == '\t') k = 1;
                else while (k < codeIndent && ch(ls + k) == ' ') k++;
                if (k) mTextBuf->remove(ls, ls + k);
                if (i == 0) firstCut = k;
            }
            ls = mTextBuf->line_end(ls) + 1;
        }
        endUndoGroup();

        if (sel && n > 1) {                /* keep the block selected, whole lines */
            int end = mTextBuf->line_end(mTextBuf->line_start(ls - 1));
            mTextBuf->select(first, end);
            insert_position(end);
        } else if (dir < 0) {              /* single line: caret follows its text */
            mTextBuf->unselect();
            insert_position(pos - first >= firstCut ? pos - firstCut : first);
        }
        show_insert_position();
    }

    /* '}' about to be typed: if the line is blank, give it the indent of the
     * line with the matching '{'. Returns 1 with an undo group OPEN (the
     * caller closes it after the brace is inserted), 0 if nothing changed. */
    int closeBraceIndent()
    {
        int pos, ls, le, p, depth = 0, target = -1, tls;
        char *ind, *cur;
        if (mTextBuf->selected()) return 0;
        pos = insert_position();
        ls  = mTextBuf->line_start(pos);
        le  = mTextBuf->line_end(pos);
        for (p = ls; p < le; p++)
            if (ch(p) != ' ' && ch(p) != '\t') return 0;
        for (p = ls - 1; p >= 0; p--) {
            char c = ch(p);
            if ((c != '{' && c != '}') || inCommentOrString(p)) continue;
            if (c == '}') depth++;
            else if (depth == 0) { target = p; break; }
            else depth--;
        }
        if (target < 0) return 0;
        tls = mTextBuf->line_start(target);
        ind = mTextBuf->text_range(tls, indentEnd(tls));
        cur = mTextBuf->text_range(ls, le);
        if (strcmp(ind, cur) == 0) { free(ind); free(cur); return 0; }
        beginUndoGroup();
        mTextBuf->replace(ls, le, ind);
        insert_position(ls + (int)strlen(ind));
        free(ind); free(cur);
        return 1;
    }

    /* ---- caret ----------------------------------------------------------
     * FLTK 1.3's cursors cannot blink and have no underscore shape, and
     * Fl_Text_Display::draw_cursor() is not virtual. So FLTK's own caret stays
     * off (show_cursor(0) in the constructor; Fl_Text_Editor only ever
     * re-applies that stored state) and draw() paints ours on top of the text:
     * a thin bar in insert mode, an underscore in overwrite mode (the Insert
     * key toggles Fl_Text_Editor::insert_mode()).
     *
     * Blinking repaints only the caret's line: redisplay_range() makes
     * Fl_Text_Display redraw that line -- wiping the old caret -- and draw()
     * then paints it again if it is in its "on" phase. Moving the caret is
     * covered already: insert_position() redisplays the old and new lines. */
    void caretRedisplay()
    {
        int p = insert_position();
        redisplay_range(mTextBuf->prev_char_clipped(p), mTextBuf->next_char(p));
    }
    void caretRestart()           /* solid now, blink from here */
    {
        Fl::remove_timeout(caretBlinkCb, this);
        mCaretOn = 1;
        if (Fl::focus() == this) Fl::add_timeout(CODE_CARET_BLINK, caretBlinkCb, this);
        caretRedisplay();
    }
    void caretStop()
    {
        Fl::remove_timeout(caretBlinkCb, this);
        mCaretOn = 0;
        caretRedisplay();
    }
    static void caretBlinkCb(void *v)
    {
        CodeEditor *ed = (CodeEditor *)v;
        ed->mCaretOn = !ed->mCaretOn;
        ed->caretRedisplay();
        Fl::repeat_timeout(CODE_CARET_BLINK, caretBlinkCb, v);
    }
    void caretDraw()
    {
        int cx, cy, t;
        if (!mCaretOn || Fl::focus() != this) return;
        if (!position_to_xy(insert_position(), &cx, &cy)) return;   /* off-screen */
        t = mMaxsize / 8;                       /* stroke: 2 px at 14 px, grows with DPI */
        if (t < 1) t = 1;
        fl_push_clip(text_area.x, text_area.y, text_area.w, text_area.h);
        fl_color(cursor_color());
        if (insert_mode()) {
            fl_rectf(cx, cy, t, mMaxsize);      /* bar */
        } else {
            fl_font(CODE_FONT, codeFontSize);   /* monospaced: every cell is an M */
            fl_rectf(cx, cy + mMaxsize - t, (int)fl_width("M"), t);   /* underscore */
        }
        fl_pop_clip();
    }

    /* Re-lex the whole buffer. Only needed on load or a language switch. */
    void rehighlightAll()
    {
        codeStyleAll(mTextBuf, mStyleBuf, mLang);
        redisplay_range(0, mTextBuf->length());
    }

private:
    Fl_Text_Buffer *mTextBuf;
    Fl_Text_Buffer *mStyleBuf;
    int mLang;
    int mWrapCol;                 /* 0 = window width; >0 = fixed column */
    int mWrapPx;                  /* cached pixel offset of mWrapCol, 0 = stale */
    int mWrapOn;
    CodeUndo mUndo;
    int mDirty;
    int mCaretOn;                 /* blink phase; drawn only while focused */
    int mLineNumLines;            /* mNBufferLines at the last margin repaint */
    int mMaxCols;                 /* longest line in the buffer, in columns */
    int mEnc;                     /* CODE_ENC_*: how the file is saved */
    int mCrlf;                    /* 1 = CRLF line ends on disk, 0 = LF */
    int mCharPx;                  /* one monospaced cell, px; 0 = not measured yet */

    static void staticModifyCb(int pos, int nInserted, int nDeleted,
                               int nRestyled, const char *deletedText, void *arg)
    {
        (void)nRestyled;
        CodeEditor *ed = (CodeEditor *)arg;
        /* Record BEFORE restyling: codeUndoRecord reads the inserted text back
         * out of the buffer, and styling does not touch the text buffer, so the
         * order is not load-bearing -- but recording first keeps the undo stack
         * consistent even if a styling failure bails out early. */
        codeUndoRecord(&ed->mUndo, ed->mTextBuf, pos, nInserted, nDeleted, deletedText);
        if (nInserted || nDeleted) ed->mDirty = 1;
        ed->styleUpdate(pos, nInserted, nDeleted);
        ed->trackLongest(pos, nInserted, nDeleted, deletedText);
    }

    /* ---- longest line, for the horizontal scrollbar ---------------------
     * Stock FLTK sizes the horizontal scrollbar from the longest VISIBLE line
     * and shows it whenever the vertical one is up -- so it appears on files
     * with no long lines, and its range jumps as you scroll. Our FLTK patch
     * (fl_text_display_longest_line) lets us answer for the whole buffer
     * instead. The code font is monospaced, so a line's width is just its
     * column count times one cell: mMaxCols is kept up to date incrementally
     * -- inserts measure only the touched lines; a delete rescans only when it
     * could have shortened the longest line. */
    static int longestLineHook(const Fl_Text_Display *d)
    {
        CodeEditor *ed = dynamic_cast<CodeEditor *>(const_cast<Fl_Text_Display *>(d));
        if (!ed || ed->mContinuousWrap) return -1;      /* wrapped: stock rules */
        return ed->mMaxCols * ed->charPx();
    }
    int charPx()
    {
        if (mCharPx <= 0) {
            fl_font(CODE_FONT, codeFontSize);
            mCharPx = (int)(fl_width("M") + 0.5);
            if (mCharPx < 1) mCharPx = 1;
        }
        return mCharPx;
    }
    /* Display columns of the line starting at ls (tabs expanded, UTF-8
     * continuation bytes not counted). */
    int lineCols(int ls) const
    {
        int col = 0, n = mTextBuf->length();
        for (; ls < n; ls++) {
            unsigned char c = (unsigned char)mTextBuf->byte_at(ls);
            if (c == '\n') break;
            if (c == '\t') col += codeIndent - col % codeIndent;
            else if ((c & 0xC0) != 0x80) col++;
        }
        return col;
    }
    void rescanLongest()
    {
        int p = 0, n = mTextBuf->length();
        mMaxCols = 0;
        while (p <= n) {
            int c = lineCols(p);
            if (c > mMaxCols) mMaxCols = c;
            p = mTextBuf->line_end(p) + 1;
        }
    }
    void trackLongest(int pos, int nInserted, int nDeleted, const char *deleted)
    {
        int old = mMaxCols, stale = 0, ls, end;
        if (nDeleted > 0) {
            if (!deleted || memchr(deleted, '\n', nDeleted)) {
                stale = 1;                     /* whole lines went: maybe the longest */
            } else {
                /* one line got shorter -- by at most this many columns */
                int k, lost = 0;
                for (k = 0; k < nDeleted; k++)
                    lost += (deleted[k] == '\t') ? codeIndent : 1;
                if (lineCols(mTextBuf->line_start(pos)) + lost >= mMaxCols) stale = 1;
            }
        }
        if (stale) {
            rescanLongest();
        } else if (nInserted > 0) {
            ls  = mTextBuf->line_start(pos);
            end = mTextBuf->line_end(pos + nInserted);
            while (ls <= end) {
                int c = lineCols(ls);
                if (c > mMaxCols) mMaxCols = c;
                ls = mTextBuf->line_end(ls) + 1;
            }
        }
        /* Let FLTK re-decide the scrollbar and its range. resize() is what
         * Fl_Text_Display itself calls after edits that change the layout. */
        if (mMaxCols != old) resize(x(), y(), w(), h());
    }

    void styleUpdate(int pos, int nInserted, int nDeleted)
    {
        int lo = 0, hi = 0;
        if (codeStyleUpdate(mTextBuf, mStyleBuf, mLang,
                            pos, nInserted, nDeleted, &lo, &hi))
            redisplay_range(lo, hi);
    }
};

#endif /* EDIT_CODE_H */
