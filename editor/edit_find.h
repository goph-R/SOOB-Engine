/*
 * edit_find.h -- Find / Replace / Go to line, as model-layer functions.
 *
 * Operates on an Fl_Text_Buffer and nothing else, so it is unit-testable with
 * no display (edit_code_model_test.cpp). codeedit.cpp puts the dialogs on top.
 *
 * Fl_Text_Buffer already provides the primitives -- search_forward(),
 * search_backward() and skip_lines() -- so this layer is mostly about the
 * things those do NOT do: wrapping around the ends, not matching the selection
 * you are standing on, and replacing every hit without looping forever when the
 * replacement contains the needle.
 */
#ifndef EDIT_FIND_H
#define EDIT_FIND_H

#include <FL/Fl_Text_Buffer.H>
#include <string.h>
#include <stdlib.h>

/* Our own, rather than strncasecmp(): that is POSIX, not C89, and Dev-C++'s
 * MinGW 3.4 does not reliably declare it. ASCII folding is all we need. */
static int codeNCaseEq(const char *a, const char *b, int n)
{
    int i, ca, cb;
    for (i = 0; i < n; i++) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
    return 1;
}

/* Find the next occurrence at or after `from`.
 * Returns 1 and writes the match start to *found, else 0.
 * With wrapAround, a miss retries from the top before giving up. */
static int codeFindNext(Fl_Text_Buffer *buf, const char *needle, int from,
                        int matchCase, int wrapAround, int *found)
{
    int pos = 0;
    if (!buf || !needle || !*needle) return 0;
    if (from < 0) from = 0;
    if (from > buf->length()) from = buf->length();

    if (buf->search_forward(from, needle, &pos, matchCase)) {
        if (found) *found = pos;
        return 1;
    }
    if (wrapAround && from > 0 && buf->search_forward(0, needle, &pos, matchCase)) {
        if (found) *found = pos;
        return 1;
    }
    return 0;
}

/* Find the previous occurrence strictly before `from`. */
static int codeFindPrev(Fl_Text_Buffer *buf, const char *needle, int from,
                        int matchCase, int wrapAround, int *found)
{
    int pos = 0;
    if (!buf || !needle || !*needle) return 0;
    if (from > buf->length()) from = buf->length();
    if (from < 0) from = 0;

    if (from > 0 && buf->search_backward(from - 1, needle, &pos, matchCase)) {
        if (found) *found = pos;
        return 1;
    }
    if (wrapAround && buf->search_backward(buf->length(), needle, &pos, matchCase)) {
        if (found) *found = pos;
        return 1;
    }
    return 0;
}

/* Replace the match starting exactly at `at`, if there is one there.
 * Returns the length written, or -1 if `at` is not a match. */
static int codeReplaceAt(Fl_Text_Buffer *buf, int at, const char *needle,
                         const char *repl, int matchCase)
{
    char *have;
    int n, same;
    if (!buf || !needle || !*needle) return -1;
    n = (int)strlen(needle);
    if (at < 0 || at + n > buf->length()) return -1;

    have = buf->text_range(at, at + n);
    if (!have) return -1;
    same = matchCase ? (strcmp(have, needle) == 0)
                     : codeNCaseEq(have, needle, n);
    free(have);
    if (!same) return -1;

    buf->replace(at, at + n, repl ? repl : "");
    return repl ? (int)strlen(repl) : 0;
}

/* Replace every occurrence. Returns how many were replaced.
 *
 * Scanning restarts past the REPLACEMENT, not past the match, so a replacement
 * that contains the needle ("x" -> "xx") terminates instead of looping. */
static int codeReplaceAll(Fl_Text_Buffer *buf, const char *needle,
                          const char *repl, int matchCase)
{
    int pos = 0, at = 0, n, rl, count = 0;
    if (!buf || !needle || !*needle) return 0;
    n  = (int)strlen(needle);
    rl = repl ? (int)strlen(repl) : 0;

    while (buf->search_forward(pos, needle, &at, matchCase)) {
        buf->replace(at, at + n, repl ? repl : "");
        count++;
        pos = at + rl;
        if (pos > buf->length()) break;
        if (rl == 0 && n == 0) break;          /* paranoia: never stand still */
    }
    return count;
}

/* Buffer position of the start of 1-based line `line`, clamped to the buffer. */
static int codeGotoLinePos(Fl_Text_Buffer *buf, int line)
{
    int pos;
    if (!buf) return 0;
    if (line < 1) line = 1;
    pos = buf->skip_lines(0, line - 1);
    if (pos > buf->length()) pos = buf->length();
    return pos;
}

/* 1-based line number containing `pos` -- for a status bar or Go to line's
 * default value. */
static int codeLineOfPos(Fl_Text_Buffer *buf, int pos)
{
    if (!buf) return 1;
    if (pos < 0) pos = 0;
    if (pos > buf->length()) pos = buf->length();
    return buf->count_lines(0, pos) + 1;
}

#endif /* EDIT_FIND_H */
