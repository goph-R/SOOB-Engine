/*
 * edit_code_model_test.cpp -- headless differential check for the incremental
 * re-highlighter in edit_code.h.  Uses Fl_Text_Buffer (pure model, never opens
 * a display) but constructs no widget, so it runs with no X server.
 *
 *   g++ editor/edit_code_model_test.cpp -o /tmp/ecmtest \
 *       $(fltk-config --cxxflags) -I. -Ieditor $(fltk-config --ldflags)
 *
 * THE INVARIANT
 *
 * codeStyleUpdate() relexes only from the edited line until the carry state
 * reconverges.  That is safe only if the result is byte-identical to lexing
 * the whole buffer from scratch.  So after every edit we compare against
 * codeStyleAll().  A divergence would show on screen as stale colours that
 * only correct themselves when you scroll or retype -- exactly the class of
 * bug that is miserable to chase on the target machine.
 *
 * The last block asserts the *performance* claim too: a local edit must
 * restyle a local range, not the rest of the file.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <FL/Fl_Text_Buffer.H>
#include "edit_code.h"
#include "edit_find.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

static Fl_Text_Buffer *T;      /* text  */
static Fl_Text_Buffer *S;      /* style, maintained incrementally */
static int LANG;
static int lastLo, lastHi;     /* range the last update restyled */

static void reset(int lang, const char *text)
{
    LANG = lang;
    T->text(text);
    codeStyleAll(T, S, LANG);
    lastLo = lastHi = 0;
}

static void edInsert(int pos, const char *txt)
{
    T->insert(pos, txt);
    lastLo = lastHi = 0;
    codeStyleUpdate(T, S, LANG, pos, (int)strlen(txt), 0, &lastLo, &lastHi);
}

static void edRemove(int a, int b)
{
    T->remove(a, b);
    lastLo = lastHi = 0;
    codeStyleUpdate(T, S, LANG, a, 0, b - a, &lastLo, &lastHi);
}

/* incremental style buffer == full re-lex? */
static int agrees(const char *what)
{
    Fl_Text_Buffer ref;
    char *inc, *full;
    int ok;

    codeStyleAll(T, &ref, LANG);
    inc  = S->text();
    full = ref.text();
    ok = (inc && full && strcmp(inc, full) == 0);
    if (!ok) {
        printf("DIVERGED after %s\n", what);
        if (inc && full) {
            int i = 0;
            while (inc[i] && full[i] && inc[i] == full[i]) i++;
            printf("  first difference at byte %d: incremental='%c' full='%c'\n",
                   i, inc[i] ? inc[i] : '?', full[i] ? full[i] : '?');
            printf("  incremental: %.70s\n  full re-lex: %.70s\n", inc + i, full + i);
        }
    }
    free(inc);
    free(full);
    return ok;
}

/* byte offset of the first occurrence of needle in the current text */
static int posOf(const char *needle)
{
    char *t = T->text();
    const char *q = strstr(t, needle);
    int r = q ? (int)(q - t) : -1;
    free(t);
    return r;
}

/* style slot at the first occurrence of needle in the current text */
static int slotAt(const char *needle)
{
    char *t = T->text(), *s = S->text();
    const char *q = strstr(t, needle);
    int r = q ? s[q - t] - 'A' : -1;
    free(t); free(s);
    return r;
}

int main(void)
{
    T = new Fl_Text_Buffer();
    S = new Fl_Text_Buffer();

    /* ---- Pascal: opening and closing a block comment from above ---- */
    reset(LEX_LANG_PASCAL, "program P;\nvar i: Integer;\nbegin\n  i := 1;\nend.\n");
    CHECK(agrees("initial pascal load"));
    CHECK(slotAt("begin") == LEX_KEYWORD);

    edInsert(11, "{ ");                     /* unterminated: swallows the rest */
    CHECK(agrees("insert unterminated '{'"));
    CHECK(slotAt("begin") == LEX_COMMENT);  /* no longer a keyword */

    {   /* close it again BEFORE 'begin', so begin returns to being code */
        int at = posOf("\nbegin");
        CHECK(at > 0);
        edInsert(at, " }");
        CHECK(agrees("close the brace comment"));
        CHECK(slotAt("begin") == LEX_KEYWORD);   /* keyword again */

        edRemove(at, at + 2);                    /* re-open by deleting the '}' */
        CHECK(agrees("re-open by deleting '}'"));
        CHECK(slotAt("begin") == LEX_COMMENT);
    }

    /* ---- Lua: levelled long brackets ---- */
    reset(LEX_LANG_LUA, "local a = 1\nlocal b = 2\nprint(a + b)\nreturn a\n");
    CHECK(agrees("initial lua load"));
    CHECK(slotAt("print") == LEX_IDENT);

    edInsert(11, " --[==[");
    CHECK(agrees("open a level-2 long comment"));
    CHECK(slotAt("print") == LEX_COMMENT);

    edInsert(30, "]] ");                    /* wrong level: must NOT close */
    CHECK(agrees("insert a level-0 close (must not close)"));
    CHECK(slotAt("print") == LEX_COMMENT);

    /* ---- Markdown: a fence swallowing then releasing following lines ---- */
    reset(LEX_LANG_MARKDOWN, "# Title\n\ntext here\n\nmore text\n\nlast line\n");
    CHECK(agrees("initial markdown load"));

    edInsert(9, "```lua\n");
    CHECK(agrees("open a lua fence"));

    edInsert(40, "```\n");
    CHECK(agrees("close the fence"));

    /* ---- bulk edits and degenerate buffers ---- */
    reset(LEX_LANG_PASCAL, "begin\n  A;\n  B;\n  C;\nend.\n");
    edInsert(6, "  (* multi\n  line\n  comment *)\n");
    CHECK(agrees("paste a multi-line comment block"));

    edRemove(6, 20);                        /* chop across a comment boundary */
    CHECK(agrees("delete across a comment boundary"));

    reset(LEX_LANG_PASCAL, "");
    CHECK(agrees("empty buffer"));
    edInsert(0, "x");                       /* single char, no trailing newline */
    CHECK(agrees("single char, no trailing newline"));
    edRemove(0, 1);
    CHECK(agrees("back to empty"));

    reset(LEX_LANG_LUA, "a\n");
    edInsert(2, "b");                       /* append after the final newline */
    CHECK(agrees("append past the last newline"));

    /* ---- character-by-character typing, the real-world case ---- */
    reset(LEX_LANG_LUA, "local t = {}\n");
    {
        const char *typed = "function t.f(n) return n * 2 end\n";
        int i;
        for (i = 0; typed[i]; i++) {
            char one[2]; one[0] = typed[i]; one[1] = '\0';
            edInsert(12 + i, one);
        }
    }
    CHECK(agrees("typing 32 characters one at a time"));
    CHECK(slotAt("function") == LEX_KEYWORD);
    CHECK(slotAt("return")   == LEX_KEYWORD);

    /* ---- the performance claim: a local edit restyles a local range ----
     * This is what makes it viable on a Pentium II. Without the convergence
     * rule, every keystroke would restyle to end-of-buffer. */
    {
        char big[40000];
        int i, n = 0;
        for (i = 0; i < 500; i++)
            n += sprintf(big + n, "  x%d := %d; // line %d\n", i, i, i);
        reset(LEX_LANG_PASCAL, big);
        CHECK(agrees("500-line pascal file"));

        edInsert(5, "y");                   /* type one char on line 1 */
        CHECK(agrees("one char typed in a 500-line file"));
        CHECK(lastHi - lastLo < 60);        /* one line, not 14000 bytes */
        printf("  local edit restyled %d bytes of %d\n", lastHi - lastLo, T->length());

        /* opening a comment SHOULD cascade -- correctness beats laziness */
        edInsert(5, "{");
        CHECK(agrees("open a comment at the top of a 500-line file"));
        CHECK(lastHi - lastLo > 10000);     /* cascaded to end of file, correctly */
        printf("  comment-open restyled %d bytes of %d\n", lastHi - lastLo, T->length());

        /* Closing it again cascades too, and must: every line below changes
         * colour back. The property that matters is that things SETTLE -- once
         * no block is open, ordinary typing is local again. */
        edInsert(6, "}");
        CHECK(agrees("close it again"));
        CHECK(lastHi - lastLo > 10000);
        printf("  comment-close restyled %d bytes of %d\n", lastHi - lastLo, T->length());

        edInsert(T->length() - 30, "z");    /* ordinary typing, far from the top */
        CHECK(agrees("ordinary edit after the cascade settled"));
        CHECK(lastHi - lastLo < 60);
        printf("  settled edit restyled %d bytes of %d\n", lastHi - lastLo, T->length());
    }

    delete T;
    delete S;

    /* ================= undo stack ================= */
    {
        CodeUndo u;
        Fl_Text_Buffer b;
        int caret;

        /* A tiny harness mirroring how CodeEditor wires it: every buffer change
         * is offered to codeUndoRecord, exactly as the modify callback does. */
        #define UNDO_INS(at, str) do { \
            int p_ = (at); const char *t_ = (str); \
            b.insert(p_, t_); \
            codeUndoRecord(&u, &b, p_, (int)strlen(t_), 0, 0); } while (0)
        #define UNDO_DEL(a, z) do { \
            int a_ = (a), z_ = (z); char *d_ = b.text_range(a_, z_); \
            b.remove(a_, z_); \
            codeUndoRecord(&u, &b, a_, 0, z_ - a_, d_); free(d_); } while (0)

        codeUndoInit(&u);
        b.text("hello\n");

        /* --- a single insert round-trips --- */
        UNDO_INS(5, " world");
        { char *t = b.text(); CHECK(strcmp(t, "hello world\n") == 0); free(t); }
        CHECK(u.at == 1);
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "hello\n") == 0); free(t); }
        CHECK(codeUndoUndo(&u, &b, &caret) == 0);          /* nothing left */
        CHECK(codeUndoRedo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "hello world\n") == 0); free(t); }
        CHECK(codeUndoRedo(&u, &b, &caret) == 0);

        /* --- a delete round-trips, restoring the exact text --- */
        UNDO_DEL(0, 5);
        { char *t = b.text(); CHECK(strcmp(t, " world\n") == 0); free(t); }
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "hello world\n") == 0); free(t); }

        /* --- typing coalesces into ONE undo step, but a newline breaks it --- */
        codeUndoClear(&u);
        b.text("");
        {
            const char *typed = "abc\ndef";
            int i;
            for (i = 0; typed[i]; i++) {
                char one[2]; one[0] = typed[i]; one[1] = 0;
                UNDO_INS(b.length(), one);
            }
        }
        { char *t = b.text(); CHECK(strcmp(t, "abc\ndef") == 0); free(t); }
        /* "abc" + "\n" + "def" -> three records, not seven */
        CHECK(u.count == 3);
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "abc\n") == 0); free(t); }
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "abc") == 0); free(t); }
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "") == 0); free(t); }

        /* --- backspace runs coalesce and restore in the right ORDER --- */
        codeUndoClear(&u);
        b.text("abcdef");
        UNDO_DEL(5, 6);                 /* f */
        UNDO_DEL(4, 5);                 /* e */
        UNDO_DEL(3, 4);                 /* d */
        { char *t = b.text(); CHECK(strcmp(t, "abc") == 0); free(t); }
        CHECK(u.count == 1);            /* one run, not three */
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "abcdef") == 0); free(t); }

        /* --- Delete-key runs (same position repeatedly) also coalesce --- */
        codeUndoClear(&u);
        b.text("abcdef");
        UNDO_DEL(3, 4); UNDO_DEL(3, 4); UNDO_DEL(3, 4);
        { char *t = b.text(); CHECK(strcmp(t, "abc") == 0); free(t); }
        CHECK(u.count == 1);
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "abcdef") == 0); free(t); }

        /* --- a new edit after undo discards the redo tail --- */
        codeUndoClear(&u);
        b.text("");
        UNDO_INS(0, "one");
        UNDO_INS(3, "two");
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        CHECK(u.at == 1 && u.count == 2);
        UNDO_INS(3, "XXX");                        /* diverge */
        CHECK(u.count == 2);                       /* the "two" record is gone */
        CHECK(codeUndoRedo(&u, &b, &caret) == 0);  /* nothing to redo */
        { char *t = b.text(); CHECK(strcmp(t, "oneXXX") == 0); free(t); }

        /* --- undo must not coalesce into the record it just walked past ---
         * Needs TWO records, so that undoing leaves at > 0 and there is still a
         * previous record for a careless implementation to merge into. (With a
         * single record, discarding the redo tail empties the stack and hides
         * the bug -- which is exactly what an earlier version of this test did.)
         * Multi-char inserts do not coalesce, so these stay separate. */
        codeUndoClear(&u);
        b.text("");
        UNDO_INS(0, "ab");
        UNDO_INS(2, "cd");
        CHECK(u.count == 2);
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);        /* back to "ab" */
        { char *t = b.text(); CHECK(strcmp(t, "ab") == 0); free(t); }
        CHECK(u.at == 1 && u.count == 2);
        UNDO_INS(2, "x");                                /* one char, would merge */
        CHECK(u.count == 2);                             /* tail dropped, new record */
        { char *t = b.text(); CHECK(strcmp(t, "abx") == 0); free(t); }
        /* The point: undoing the "x" must return to "ab", NOT to "". */
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "ab") == 0); free(t); }

        /* --- replaying is not recorded (the re-entrancy guard) --- */
        codeUndoClear(&u);
        b.text("");
        UNDO_INS(0, "zz");
        CHECK(u.count == 1);
        codeUndoUndo(&u, &b, &caret);
        codeUndoRedo(&u, &b, &caret);
        CHECK(u.count == 1);                       /* still one, not three */

        /* --- deep history: undo everything gets back to the start --- */
        codeUndoClear(&u);
        b.text("start\n");
        {
            int i;
            for (i = 0; i < 40; i++) {
                char line[32];
                sprintf(line, "line %d\n", i);
                UNDO_INS(b.length(), line);
            }
            CHECK(u.count == 40);
            while (codeUndoUndo(&u, &b, &caret)) { }
            { char *t = b.text(); CHECK(strcmp(t, "start\n") == 0); free(t); }
            while (codeUndoRedo(&u, &b, &caret)) { }
            { char *t = b.text(); CHECK(b.length() > 200); free(t); }
        }

        /* --- the oldest record is dropped past the cap, without corruption --- */
        codeUndoClear(&u);
        b.text("");
        {
            int i;
            for (i = 0; i < CODE_UNDO_MAX + 50; i++) {
                char one[8];
                sprintf(one, "%d\n", i % 10);      /* newline breaks coalescing */
                UNDO_INS(b.length(), one);
            }
            CHECK(u.count == CODE_UNDO_MAX);
            CHECK(u.at == CODE_UNDO_MAX);
            while (codeUndoUndo(&u, &b, &caret)) { }
            CHECK(b.length() > 0);                 /* the dropped prefix survives */
        }
        codeUndoFree(&u);
        #undef UNDO_INS
        #undef UNDO_DEL
    }

    /* ================= find / replace / goto ================= */
    {
        Fl_Text_Buffer b;
        int at;

        b.text("alpha beta\nAlpha gamma\nbeta ALPHA\n");

        /* --- case sensitivity --- */
        CHECK(codeFindNext(&b, "alpha", 0, 1, 0, &at) == 1 && at == 0);
        CHECK(codeFindNext(&b, "Alpha", 0, 1, 0, &at) == 1 && at == 11);
        CHECK(codeFindNext(&b, "ALPHA", 0, 1, 0, &at) == 1 && at == 28);
        /* case-insensitive finds the first one regardless of spelling */
        CHECK(codeFindNext(&b, "ALPHA", 0, 0, 0, &at) == 1 && at == 0);

        /* --- forward walk, then the wrap decision --- */
        CHECK(codeFindNext(&b, "alpha", 1, 0, 0, &at) == 1 && at == 11);
        CHECK(codeFindNext(&b, "alpha", 12, 0, 0, &at) == 1 && at == 28);
        CHECK(codeFindNext(&b, "alpha", 29, 0, 0, &at) == 0);      /* no wrap */
        CHECK(codeFindNext(&b, "alpha", 29, 0, 1, &at) == 1 && at == 0);  /* wrap */

        /* --- backward --- */
        CHECK(codeFindPrev(&b, "beta", b.length(), 1, 0, &at) == 1 && at == 23);
        CHECK(codeFindPrev(&b, "beta", 23, 1, 0, &at) == 1 && at == 6);
        CHECK(codeFindPrev(&b, "beta", 6, 1, 0, &at) == 0);        /* no wrap */
        CHECK(codeFindPrev(&b, "beta", 6, 1, 1, &at) == 1 && at == 23);   /* wrap */

        /* --- misses and degenerate needles --- */
        CHECK(codeFindNext(&b, "zzz", 0, 1, 1, &at) == 0);
        CHECK(codeFindNext(&b, "", 0, 1, 1, &at) == 0);
        CHECK(codeFindNext(&b, 0, 0, 1, 1, &at) == 0);

        /* --- replace a single hit, only where one actually is --- */
        b.text("one two one\n");
        CHECK(codeReplaceAt(&b, 0, "one", "1", 1) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "1 two one\n") == 0); free(t); }
        CHECK(codeReplaceAt(&b, 2, "one", "1", 1) == -1);     /* "two" is not "one" */
        CHECK(codeReplaceAt(&b, 99, "one", "1", 1) == -1);    /* out of range */

        /* --- replace all --- */
        b.text("one two one three one\n");
        CHECK(codeReplaceAll(&b, "one", "1", 1) == 3);
        { char *t = b.text(); CHECK(strcmp(t, "1 two 1 three 1\n") == 0); free(t); }

        /* case-insensitive replace all hits every spelling */
        b.text("One ONE one\n");
        CHECK(codeReplaceAll(&b, "one", "x", 0) == 3);
        { char *t = b.text(); CHECK(strcmp(t, "x x x\n") == 0); free(t); }

        /* the replacement CONTAINING the needle must terminate, not loop */
        b.text("aaa\n");
        CHECK(codeReplaceAll(&b, "a", "aa", 1) == 3);
        { char *t = b.text(); CHECK(strcmp(t, "aaaaaa\n") == 0); free(t); }

        /* replacing with nothing is a delete-all */
        b.text("x1x2x3\n");
        CHECK(codeReplaceAll(&b, "x", "", 1) == 3);
        { char *t = b.text(); CHECK(strcmp(t, "123\n") == 0); free(t); }

        /* --- go to line, and its inverse --- */
        b.text("l1\nl2\nl3\nl4\n");
        CHECK(codeGotoLinePos(&b, 1) == 0);
        CHECK(codeGotoLinePos(&b, 2) == 3);
        CHECK(codeGotoLinePos(&b, 3) == 6);
        CHECK(codeGotoLinePos(&b, 0) == 0);                  /* clamped low  */
        CHECK(codeGotoLinePos(&b, 9999) == b.length());      /* clamped high */
        CHECK(codeLineOfPos(&b, 0) == 1);
        CHECK(codeLineOfPos(&b, 3) == 2);
        CHECK(codeLineOfPos(&b, 7) == 3);
        /* round-trip: the line of a line-start is that line */
        { int L; for (L = 1; L <= 4; L++) CHECK(codeLineOfPos(&b, codeGotoLinePos(&b, L)) == L); }
    }

    /* ---- Replace All must be ONE undo step, not one per match ---- */
    {
        CodeUndo u;
        Fl_Text_Buffer b;
        int caret, n, i, at;

        codeUndoInit(&u);
        b.text("one two one three one\n");

        /* drive it the way CodeEditor does: record every modification */
        codeUndoBeginGroup(&u);
        {
            int pos = 0;
            n = 0;
            while (b.search_forward(pos, "one", &at, 1)) {
                char *del = b.text_range(at, at + 3);
                b.replace(at, at + 3, "1");
                codeUndoRecord(&u, &b, at, 1, 3, del);
                free(del);
                pos = at + 1;
                n++;
            }
        }
        codeUndoEndGroup(&u);
        CHECK(n == 3);
        CHECK(u.count == 3);                    /* three records... */
        { char *t = b.text(); CHECK(strcmp(t, "1 two 1 three 1\n") == 0); free(t); }

        CHECK(codeUndoUndo(&u, &b, &caret) == 1);   /* ...but ONE undo */
        CHECK(u.at == 0);
        { char *t = b.text(); CHECK(strcmp(t, "one two one three one\n") == 0); free(t); }

        CHECK(codeUndoRedo(&u, &b, &caret) == 1);   /* and one redo */
        CHECK(u.at == 3);
        { char *t = b.text(); CHECK(strcmp(t, "1 two 1 three 1\n") == 0); free(t); }

        /* an ungrouped edit after the group stays its own step */
        b.insert(0, "Z");
        codeUndoRecord(&u, &b, 0, 1, 0, 0);
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "1 two 1 three 1\n") == 0); free(t); }
        CHECK(codeUndoUndo(&u, &b, &caret) == 1);
        { char *t = b.text(); CHECK(strcmp(t, "one two one three one\n") == 0); free(t); }
        (void)i;
        codeUndoFree(&u);
    }

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("edit_code_model_test: all checks passed\n");
    return 0;
}
