#ifndef EDIT_FILEIO_H
#define EDIT_FILEIO_H

/*
 * edit_fileio.h -- read and write text files keeping their encoding and line
 * ending, for the code editor.
 *
 * Fl_Text_Buffer holds UTF-8 with LF line ends. Its own loadfile()/savefile()
 * read non-UTF-8 input as Latin-1 (wrong for a Central European ANSI file --
 * code page 1250 here) and always write UTF-8 through a text-mode stream, so
 * every file came back as UTF-8 + CRLF. Instead:
 *
 *   load: UTF-8 BOM -> CODE_ENC_UTF8_BOM; valid UTF-8 (incl. plain ASCII) ->
 *         CODE_ENC_UTF8; anything else -> CODE_ENC_ANSI, converted from the
 *         system ANSI code page. The first line break decides CRLF vs LF.
 *   save: the same encoding and line ending back out.
 *
 * ANSI conversion goes through MultiByteToWideChar / WideCharToMultiByte with
 * CP_ACP, which Windows 98 has; FLTK's own fl_utf8towc / fl_utf8fromwc do the
 * UTF-16 <-> UTF-8 half, so CP_UTF8 (missing on Win95) is never needed. Off
 * Windows, ANSI means Latin-1.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <FL/fl_utf8.h>
#ifdef _WIN32
#include <windows.h>
#endif

enum {
    CODE_ENC_UTF8 = 0,
    CODE_ENC_UTF8_BOM,
    CODE_ENC_ANSI
};

/* Line ending for files that have no line break to go by. */
#ifdef _WIN32
#define CODE_DEFAULT_CRLF 1
#else
#define CODE_DEFAULT_CRLF 0
#endif

static const char *codeEncName(int enc)
{
    if (enc == CODE_ENC_UTF8_BOM) return "UTF-8 BOM";
    if (enc == CODE_ENC_ANSI)     return "ANSI";
    return "UTF-8";
}

/* ANSI -> UTF-8. Returns a malloc'd, NUL-terminated string. */
static char *codeAnsiToUtf8(const char *src, int n)
{
    char *out;
#ifdef _WIN32
    int wn = n ? MultiByteToWideChar(CP_ACP, 0, src, n, NULL, 0) : 0;
    wchar_t *w = (wchar_t *)malloc((size_t)(wn + 1) * sizeof(wchar_t));
    unsigned un;
    if (!w) return NULL;
    if (wn) MultiByteToWideChar(CP_ACP, 0, src, n, w, wn);
    un = fl_utf8fromwc(NULL, 0, w, (unsigned)wn);
    out = (char *)malloc(un + 1);
    if (out) fl_utf8fromwc(out, un + 1, w, (unsigned)wn);
    free(w);
#else
    unsigned un = fl_utf8froma(NULL, 0, src, (unsigned)n);
    out = (char *)malloc(un + 1);
    if (out) fl_utf8froma(out, un + 1, src, (unsigned)n);
#endif
    return out;
}

/* UTF-8 -> ANSI. Returns a malloc'd buffer, its length in *outLen, and in
 * *lossy whether some character had no ANSI equivalent (written as '?'). */
static char *codeUtf8ToAnsi(const char *src, int n, int *outLen, int *lossy)
{
    char *out;
    *lossy = 0;
#ifdef _WIN32
    {
        unsigned wn = fl_utf8towc(src, (unsigned)n, NULL, 0);
        wchar_t *w = (wchar_t *)malloc((wn + 1) * sizeof(wchar_t));
        int mn;
        BOOL usedDefault = FALSE;
        if (!w) return NULL;
        fl_utf8towc(src, (unsigned)n, w, wn + 1);
        mn = wn ? WideCharToMultiByte(CP_ACP, 0, w, (int)wn, NULL, 0, NULL, NULL) : 0;
        out = (char *)malloc((size_t)mn + 1);
        if (out && mn)
            WideCharToMultiByte(CP_ACP, 0, w, (int)wn, out, mn, NULL, &usedDefault);
        free(w);
        *lossy = usedDefault ? 1 : 0;
        *outLen = mn;
    }
#else
    {
        unsigned mn = fl_utf8toa(src, (unsigned)n, NULL, 0);
        out = (char *)malloc(mn + 1);
        if (out) fl_utf8toa(src, (unsigned)n, out, mn + 1);
        *outLen = (int)mn;
    }
#endif
    return out;
}

/* Would saving this UTF-8 text as ANSI lose characters? */
static int codeAnsiIsLossy(const char *text, int n)
{
    int len, lossy;
    char *a = codeUtf8ToAnsi(text, n, &len, &lossy);
    free(a);
    return lossy;
}

/* Read a whole file. Returns malloc'd UTF-8 text with LF line ends (caller
 * frees), or NULL if it cannot be read. Sets *enc and *crlf. */
static char *codeReadText(const char *path, int *enc, int *crlf)
{
    FILE *f = fl_fopen(path, "rb");
    long n;
    char *raw, *data, *text;
    int dn, i, o;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n < 0) { fclose(f); return NULL; }
    raw = (char *)malloc((size_t)n + 1);
    if (!raw) { fclose(f); return NULL; }
    if (n && fread(raw, 1, (size_t)n, f) != (size_t)n) { free(raw); fclose(f); return NULL; }
    fclose(f);
    raw[n] = '\0';

    data = raw;
    dn = (int)n;
    if (dn >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF) {
        *enc = CODE_ENC_UTF8_BOM;
        data += 3;
        dn -= 3;
    } else if (dn == 0 || fl_utf8test(data, (unsigned)dn)) {
        *enc = CODE_ENC_UTF8;
    } else {
        *enc = CODE_ENC_ANSI;
    }

    if (*enc == CODE_ENC_ANSI) {
        text = codeAnsiToUtf8(data, dn);
        free(raw);
        if (!text) return NULL;
    } else {
        memmove(raw, data, (size_t)dn);
        raw[dn] = '\0';
        text = raw;
    }

    /* line ending from the first break; then CRLF -> LF in place */
    *crlf = CODE_DEFAULT_CRLF;
    for (i = 0; text[i]; i++)
        if (text[i] == '\n') { *crlf = (i > 0 && text[i - 1] == '\r'); break; }
    for (i = 0, o = 0; text[i]; i++) {
        if (text[i] == '\r' && text[i + 1] == '\n') continue;
        text[o++] = text[i];
    }
    text[o] = '\0';
    return text;
}

/* Write UTF-8 text (LF line ends) out in the given encoding and line ending.
 * Returns 0 on success, 1 if the file cannot be written. */
static int codeWriteText(const char *path, const char *text, int n, int enc, int crlf)
{
    FILE *f;
    char *buf, *ansi = 0;
    const char *out;
    int i, o = 0, lines = 0, outLen, lossy, ok;

    for (i = 0; i < n; i++) if (text[i] == '\n') lines++;
    buf = (char *)malloc((size_t)n + (crlf ? lines : 0) + 1);
    if (!buf) return 1;
    for (i = 0; i < n; i++) {
        if (crlf && text[i] == '\n') buf[o++] = '\r';
        buf[o++] = text[i];
    }
    buf[o] = '\0';

    out = buf;
    outLen = o;
    if (enc == CODE_ENC_ANSI) {
        ansi = codeUtf8ToAnsi(buf, o, &outLen, &lossy);
        if (!ansi) { free(buf); return 1; }
        out = ansi;
    }

    f = fl_fopen(path, "wb");
    if (!f) { free(buf); free(ansi); return 1; }
    ok = 1;
    if (enc == CODE_ENC_UTF8_BOM && fwrite("\xEF\xBB\xBF", 1, 3, f) != 3) ok = 0;
    if (ok && outLen && fwrite(out, 1, (size_t)outLen, f) != (size_t)outLen) ok = 0;
    if (fclose(f) != 0) ok = 0;
    free(buf);
    free(ansi);
    return ok ? 0 : 1;
}

#endif /* EDIT_FILEIO_H */
