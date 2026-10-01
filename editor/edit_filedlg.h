/*
 * edit_filedlg.h -- Open / Save dialogs that look right on Windows 98.
 *
 * FLTK offers two file dialogs and neither is quite what we want here:
 *
 *   fl_file_chooser()        FLTK's own, drawn with FLTK widgets. Works on 98
 *                            (after the scandir patch) but looks like FLTK,
 *                            not like Windows.
 *   Fl_Native_File_Chooser   wraps the platform dialog -- but on Windows it is
 *                            OPENFILENAMEW / GetOpenFileNameW / GetSaveFileNameW
 *                            / SHBrowseForFolderW throughout, and the wide API
 *                            is stubbed out on 95/98/ME. Unusable there.
 *
 * The ANSI comdlg32 entry points work perfectly well on Win98, so this calls
 * GetOpenFileNameA / GetSaveFileNameA directly and gets the genuine period
 * dialog. Off Windows it falls back to fl_file_chooser(), so the same call
 * compiles and behaves on the Linux build.
 *
 * -lcomdlg32 is already in t98.bat / c98.bat / f98.bat, so there is nothing to
 * add to the build.
 *
 * TWO FLAGS THAT MATTER
 *
 *   OFN_NOCHANGEDIR  -- without it the dialog leaves the process sitting in
 *                       whatever directory the user browsed to. The engine and
 *                       editor both resolve assets relative to the working
 *                       directory, so losing it breaks texture/model loading
 *                       after the first Open. Non-negotiable here.
 *   OFN_EXPLORER     -- selects the Win95+ dialog. Without it comdlg32 serves
 *                       the Windows 3.1 style one, which is not the look we are
 *                       after on 98.
 *
 * AND ONE SIZE TRAP
 *
 * OPENFILENAME grew three members (pvReserved, dwReserved, FlagsEx) at
 * _WIN32_WINNT >= 0x0500 -- which is exactly what these builds compile at, per
 * docs/editor-fltk-win98.md. Older comdlg32 versions reject the larger
 * lStructSize with CDERR_STRUCTSIZE and simply never show a dialog. Rather than
 * probe the SDK for a constant MinGW 3.4 may not define, we try the compiled
 * size and retry at the legacy 76 bytes if comdlg32 objects. Self-correcting,
 * and it reports anything else through CommDlgExtendedError().
 */
#ifndef EDIT_FILEDLG_H
#define EDIT_FILEDLG_H

#include <string.h>
#include <stdio.h>

#if defined(WIN32) || defined(_WIN32)
#  include <windows.h>
#  include <commdlg.h>
#else
#  include <FL/Fl_File_Chooser.H>
#endif

/* Win32 filter: pairs of NUL-terminated strings, list ends with an extra NUL.
 * The trailing "\0" in the literal supplies that second terminator. */
#define CODE_FILTER_WIN32 \
    "Source files (*.lua;*.pas;*.pp;*.inc;*.dpr;*.md)\0*.lua;*.pas;*.pp;*.inc;*.dpr;*.md\0" \
    "Lua scripts (*.lua)\0*.lua\0" \
    "Pascal (*.pas;*.pp;*.inc;*.dpr)\0*.pas;*.pp;*.inc;*.dpr\0" \
    "Markdown (*.md)\0*.md\0" \
    "All files (*.*)\0*.*\0"

/* FLTK's own chooser uses a different syntax for the same idea. */
#define CODE_FILTER_FLTK \
    "Source files\t*.{lua,pas,pp,inc,dpr,md}"

#if defined(WIN32) || defined(_WIN32)

/* Legacy OPENFILENAME size on 32-bit Windows, i.e. offsetof(OPENFILENAMEA,
 * pvReserved). Hard-coded because MinGW 3.4 may not declare the member at all,
 * which would make offsetof() fail to compile. Only ever used as a fallback
 * after comdlg32 has explicitly complained. */
#define CODE_OFN_SIZE_V400 76

static int codeFileDlgWin32(char *out, int outlen, int saving,
                            const char *title, const char *initialDir,
                            const char *suggestedName)
{
    OPENFILENAMEA ofn;
    char buf[MAX_PATH];
    DWORD err;

    if (!out || outlen <= 0) return 0;
    out[0] = '\0';

    buf[0] = '\0';
    if (suggestedName && *suggestedName) {
        strncpy(buf, suggestedName, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
    }

    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize     = sizeof(OPENFILENAMEA);
    ofn.hwndOwner       = NULL;
    ofn.lpstrFilter     = CODE_FILTER_WIN32;
    ofn.nFilterIndex    = 1;
    ofn.lpstrFile       = buf;
    ofn.nMaxFile        = sizeof(buf);
    ofn.lpstrTitle      = title;
    ofn.lpstrInitialDir = initialDir;
    ofn.lpstrDefExt     = saving ? "lua" : NULL;
    ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY | OFN_NOCHANGEDIR
              | (saving ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));

    if (saving ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn)) {
        strncpy(out, buf, (size_t)(outlen - 1));
        out[outlen - 1] = '\0';
        return 1;
    }

    /* 0 means cancelled OR failed -- CommDlgExtendedError() tells them apart. */
    err = CommDlgExtendedError();
    if (err == CDERR_STRUCTSIZE) {
        ofn.lStructSize = CODE_OFN_SIZE_V400;      /* pre-0x0500 comdlg32 */
        if (saving ? GetSaveFileNameA(&ofn) : GetOpenFileNameA(&ofn)) {
            strncpy(out, buf, (size_t)(outlen - 1));
            out[outlen - 1] = '\0';
            return 1;
        }
        err = CommDlgExtendedError();
    }
    if (err != 0)
        fprintf(stderr, "codeFileDlg: CommDlgExtendedError = 0x%lx\n", (unsigned long)err);
    return 0;                                      /* err == 0 -> user cancelled */
}

#endif /* WIN32 */

/* Open dialog. Returns 1 and fills `out` with a path, or 0 if cancelled. */
static int codeFileOpenDialog(char *out, int outlen, const char *initialDir)
{
#if defined(WIN32) || defined(_WIN32)
    return codeFileDlgWin32(out, outlen, 0, "Open", initialDir, NULL);
#else
    const char *p = fl_file_chooser("Open", CODE_FILTER_FLTK, initialDir, 0);
    if (!p || !out || outlen <= 0) { if (out && outlen > 0) out[0] = '\0'; return 0; }
    strncpy(out, p, (size_t)(outlen - 1));         /* p is STATIC storage: copy now */
    out[outlen - 1] = '\0';
    return 1;
#endif
}

/* Save-as dialog. `suggestedName` may be NULL. Returns 1 and fills `out`. */
static int codeFileSaveDialog(char *out, int outlen, const char *initialDir,
                              const char *suggestedName)
{
#if defined(WIN32) || defined(_WIN32)
    return codeFileDlgWin32(out, outlen, 1, "Save As", initialDir, suggestedName);
#else
    const char *p = fl_file_chooser("Save As", CODE_FILTER_FLTK,
                                    suggestedName ? suggestedName : initialDir, 0);
    if (!p || !out || outlen <= 0) { if (out && outlen > 0) out[0] = '\0'; return 0; }
    strncpy(out, p, (size_t)(outlen - 1));
    out[outlen - 1] = '\0';
    return 1;
#endif
}

#endif /* EDIT_FILEDLG_H */
