/*
 * chooser98.cpp -- Phase 0 probe: does directory listing work on Win98?
 *
 * FLTK 1.3 enumerates directories with FindFirstFileW / FindNextFileW, which
 * are stubs that fail on Windows 95/98/ME. That silently empties every listing
 * in FLTK's own file chooser, so File / Open would show nothing. See the SOOB
 * patch in vendor/fltk-1.3/FL/src/scandir_win32.c.
 *
 * This probe checks the layers separately, cheapest first, so a failure says
 * WHERE it broke rather than just "Open doesn't work":
 *
 *   stage 1  fl_filename_list()  -- scandir itself, no GUI at all
 *   stage 2  fl_stat/fl_access   -- the other fl_utf8.cxx paths the chooser uses
 *   stage 3  fl_file_chooser()   -- FLTK's own drawn chooser
 *   stage 4  codeFileOpenDialog()-- the native ANSI comdlg32 one we actually use
 *
 * Stages 3 and 4 are shown back to back on purpose: 3 is what FLTK draws, 4 is
 * the real Windows 98 dialog. Compare them and keep the one you prefer.
 *
 * Stages 1 and 2 print to stdout and need no window, so they still report even
 * if stage 3 misbehaves. Build with f98.bat.
 *
 *   f98  &&  c98chooser            (lists the current directory)
 *   c98chooser C:\Projects         (lists that directory instead)
 */
#include <FL/Fl.H>
#include <FL/filename.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/fl_utf8.h>
#include "edit_filedlg.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>

#ifdef WIN32
#include <windows.h>
#endif

static void msg(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fflush(stdout);
}

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

/* Same test the patches use, so the probe reports which path FLTK took. */
static int probeIsWin9x(void)
{
#ifdef WIN32
    OSVERSIONINFOA osv;
    osv.dwOSVersionInfoSize = sizeof(osv);
    if (!GetVersionExA(&osv)) return -1;
    msg("platform: dwPlatformId=%lu major=%lu minor=%lu\n",
        (unsigned long)osv.dwPlatformId,
        (unsigned long)osv.dwMajorVersion,
        (unsigned long)osv.dwMinorVersion);
    return osv.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS ? 1 : 0;
#else
    return 0;
#endif
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : ".";
    int is9x;

    Fl::error = flMsg;
    Fl::warning = flMsg;

    is9x = probeIsWin9x();
    msg("chooser98: Win9x detected = %d  (1 = ANSI path, 0 = Unicode path)\n\n", is9x);

    /* ---- stage 1: scandir, no GUI ---- */
    {
        struct dirent **list = 0;
        int n = fl_filename_list(dir, &list);
        msg("stage 1: fl_filename_list(\"%s\") returned %d\n", dir, n);
        if (n < 0) {
            msg("stage 1: FAILED -- scandir could not open the directory.\n");
        } else if (n == 0) {
            msg("stage 1: EMPTY -- this is the Win98 FindFirstFileW symptom.\n"
                "stage 1: even \".\" and \"..\" are missing, which a real listing\n"
                "stage 1: always has. The scandir patch is not in this build.\n");
        } else {
            int i, show = n < 12 ? n : 12;
            for (i = 0; i < show; i++) msg("  [%2d] %s\n", i, list[i]->d_name);
            if (n > show) msg("  ... and %d more\n", n - show);
            msg("stage 1: OK\n");
        }
        if (list) fl_filename_free_list(&list, n > 0 ? n : 0);
    }

    /* ---- stage 2: the other fl_utf8.cxx paths the chooser leans on ---- */
    {
        char cwd[1024];
        struct stat st;
        int rc;
        cwd[0] = '\0';
        msg("\nstage 2: fl_getcwd -> %s\n",
            fl_getcwd(cwd, (int)sizeof(cwd)) ? cwd : "(NULL -- FAILED)");
        msg("stage 2: fl_access(\"%s\", 0) = %d (0 = exists)\n", dir, fl_access(dir, 0));
        /* fl_stat() must complete before st is read -- argument evaluation order
         * is unspecified, so these cannot share one msg() call. */
        memset(&st, 0, sizeof(st));
        rc = fl_stat(dir, &st);
        msg("stage 2: fl_stat(\"%s\") = %d, is-dir = %d\n",
            dir, rc, (st.st_mode & 0170000) == 0040000);
        msg("stage 2: fl_filename_isdir(\"%s\") = %d\n", dir, fl_filename_isdir(dir));
    }

    /* ---- stage 3: the whole drawn chooser ---- */
    msg("\nstage 3: opening fl_file_chooser() -- pick a file, or Cancel.\n"
        "stage 3: if the listing is EMPTY but stage 1 printed names, the bug is\n"
        "stage 3: in the chooser, not in scandir.\n");
    {
        const char *pick = fl_file_chooser("Phase 0 probe: open a file",
                                           "Source\t*.{lua,pas,pp,inc,dpr,md}",
                                           dir, 0);
        /* NOTE: fl_file_chooser returns a pointer to STATIC storage -- the real
         * editor must copy it immediately, before the next chooser call. */
        msg("stage 3: returned %s\n", pick ? pick : "(NULL -- cancelled)");
    }

    /* ---- stage 4: the native ANSI comdlg32 dialog (edit_filedlg.h) ---- */
    msg("\nstage 4: opening the NATIVE dialog (GetOpenFileNameA).\n"
        "stage 4: this is the one the editor will use on Win98.\n");
    {
        char path[1024];
        if (codeFileOpenDialog(path, (int)sizeof(path), dir))
            msg("stage 4: returned %s\n", path);
        else
            msg("stage 4: cancelled (or failed -- any CommDlgExtendedError is "
                "printed on stderr)\n");
    }

    /* ---- stage 5: native Save As, mostly to check OFN_NOCHANGEDIR ---- */
    {
        char path[1024], cwdBefore[1024], cwdAfter[1024];
        cwdBefore[0] = cwdAfter[0] = '\0';
        fl_getcwd(cwdBefore, (int)sizeof(cwdBefore));
        msg("\nstage 5: native Save As dialog.\n");
        if (codeFileSaveDialog(path, (int)sizeof(path), dir, "untitled.lua"))
            msg("stage 5: returned %s\n", path);
        else
            msg("stage 5: cancelled\n");
        fl_getcwd(cwdAfter, (int)sizeof(cwdAfter));
        msg("stage 5: cwd before = %s\n", cwdBefore);
        msg("stage 5: cwd after  = %s\n", cwdAfter);
        msg("stage 5: cwd %s -- OFN_NOCHANGEDIR %s\n",
            strcmp(cwdBefore, cwdAfter) == 0 ? "UNCHANGED" : "MOVED",
            strcmp(cwdBefore, cwdAfter) == 0 ? "works (assets stay loadable)"
                                             : "FAILED -- relative asset paths will break");
    }

    msg("\nchooser98: done.\n");
    return 0;
}
