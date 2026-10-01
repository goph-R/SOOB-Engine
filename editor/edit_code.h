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
/* Inactive tab caption. Fl_Tabs draws the SELECTED tab's label in the
 * Fl_Tabs' own labelcolor() and every other tab's in that child's labelcolor()
 * (Fl_Tabs.cxx draw_tab), so setting both once is enough -- no tab-change
 * callback needed. 3.67:1 against the inactive tab background, against 13.36:1
 * for the active one, so it reads as clearly subordinate but stays legible. */
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

/* Runtime code font size -- CODE_FONTSIZE scaled for the display. Set it with
 * codeSetFontSize() BEFORE the first CodeEditor is constructed; existing
 * editors keep the size they were built with. */
static int codeFontSize = CODE_FONTSIZE;

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
        wrap_mode(WRAP_NONE, 0);
        textfont(CODE_FONT);
        textsize(codeFontSize);

        box(FL_FLAT_BOX);
        color(CODE_COL_BG);
        textcolor(CODE_COL_FG);
        cursor_color(CODE_COL_FG);
        selection_color(CODE_COL_SEL);

        linenumber_width(48 * codeFontSize / CODE_FONTSIZE);   /* 48 px at the default size */
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

    /* Load a file and pick the language from its extension.
     * Returns 0 on success, like Fl_Text_Buffer::loadfile(). */
    int loadFile(const char *path)
    {
        int r;
        mLang = lexLangFromPath(path);
        r = mTextBuf->loadfile(path);
        rehighlightAll();
        clearUndo();
        mDirty = 0;
        return r;
    }

    int saveFile(const char *path)
    {
        int r = mTextBuf->savefile(path);
        if (r == 0) mDirty = 0;
        return r;
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
     * key bindings so its kf_undo (which drives FLTK's dead undo) never runs. */
    int handle(int e)
    {
        if (e == FL_KEYBOARD) {
            int st = Fl::event_state();
            int k  = Fl::event_key();
            if (st & FL_CTRL) {
                if (k == 'z') { if (st & FL_SHIFT) redo(); else undo(); return 1; }
                if (k == 'y') { redo(); return 1; }
            }
        }
        return Fl_Text_Editor::handle(e);
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
