/*
 * codetest.cpp -- standalone probe for the CodeEditor widget on the Win98 box.
 *
 * Same spirit as fltktest.cpp: no engine, no GL, no assets -- just the widget
 * in a window, reporting to stdout (flushed, because Fl::error goes to stderr
 * and COMMAND.COM has no 2>&1). Build with c98.bat.
 *
 *   codetest                 -- built-in demo (Markdown with Lua + Pascal fences)
 *   codetest scripts\main.lua -- open a file, language from the extension
 *
 * The language menu switches the lexer on the current text, so one buffer can
 * be eyeballed through all three grammars. The title bar reports how long the
 * full re-highlight took -- that is the number worth watching on a Pentium II,
 * since it is the only O(file) operation in the widget. Editing is incremental.
 */
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Box.H>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "edit_code.h"

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

/* Exercises all three lexers at once: Markdown outer, with fenced Lua and
 * Pascal blocks lexed by their own grammars. */
static const char *demoText =
    "# CodeEditor\n"
    "\n"
    "Dark **Dpress** palette, *line numbers*, `three grammars`.\n"
    "See [Dpress](https://github.com/goph-R/dynart-dpress) for the original.\n"
    "\n"
    "- Pascal, Lua and Markdown\n"
    "- fenced blocks pick up the inner grammar\n"
    "1. incremental re-highlight\n"
    "\n"
    "> Carry state lives in each newline's style byte.\n"
    "\n"
    "```lua\n"
    "local level = {}\n"
    "function level.spawn(name, x, y, z)\n"
    "  print(\"spawn \" .. name)       -- builtin in cyan\n"
    "  return { n = name, p = { x, y, z }, id = 0xFF }\n"
    "end\n"
    "--[==[ a levelled long comment\n"
    "   ]] does not close this one\n"
    "]==]\n"
    "```\n"
    "\n"
    "```pascal\n"
    "{$R+}\n"
    "program Demo;\n"
    "var i: Integer;\n"
    "begin\n"
    "  { a brace comment }\n"
    "  WriteLn('it''s quoted');   // and a line comment\n"
    "  for i := 1..10 do Inc(i, $1F);\n"
    "end.\n"
    "```\n"
    "\n"
    "---\n"
    "\n"
    "Type in here -- only the edited line re-lexes.\n";

static CodeEditor *gEd;
static Fl_Window  *gWin;
static char        gTitle[256];
static const char *gPath;

static void report(const char *what)
{
    clock_t t0 = clock();
    gEd->rehighlightAll();
    {
        long ms = (long)((clock() - t0) * 1000 / CLOCKS_PER_SEC);
        static const char *names[] = { "Text", "Pascal", "Lua", "Markdown" };
        sprintf(gTitle, "CodeEditor -- %s -- %s -- full re-highlight %ld ms",
                gPath ? gPath : "(demo)", names[gEd->language()], ms);
        gWin->label(gTitle);
        msg("%s: language=%s full re-highlight = %ld ms\n",
            what, names[gEd->language()], ms);
    }
}

static void langCb(Fl_Widget *w, void *)
{
    gEd->language(((Fl_Choice *)w)->value());
    report("switch");
}

int main(int argc, char **argv)
{
    Fl::error = flMsg;
    Fl::warning = flMsg;
    Fl::scheme("none");          /* plain FLTK drawing: cheapest on a PII */

    gPath = (argc > 1) ? argv[1] : 0;

    gWin = new Fl_Window(720, 520, "CodeEditor");
    gWin->color(CODE_COL_BG);
    gWin->begin();

    {
        Fl_Choice *ch = new Fl_Choice(60, 6, 140, 22, "Lang:");
        ch->add("Text|Pascal|Lua|Markdown");
        ch->labelcolor(CODE_COL_FG);
        ch->color(CODE_COL_GUTTER);
        ch->textcolor(CODE_COL_FG);
        ch->callback(langCb);
        ch->value(LEX_LANG_MARKDOWN);

        gEd = new CodeEditor(0, 34, 720, 486);
        if (gPath) {
            /* Fl_Text_Buffer::loadfile(): 0 = ok, 1 = could not open the file,
             * 2 = read error after a partial load. It opens through fl_fopen(),
             * which on Win9x used to funnel into _wfopen() and always fail --
             * see the SOOB patch in vendor/fltk-1.3/FL/src/fl_utf8.cxx. */
            int r = gEd->loadFile(gPath);
            if (r == 0) {
                msg("codetest: loaded '%s' (%d bytes)\n", gPath, gEd->textBuffer()->length());
            } else {
                msg("codetest: loadfile('%s') returned %d (%s) -- errno %d: %s\n",
                    gPath, r,
                    r == 1 ? "could not open" : "read error",
                    errno, strerror(errno));
                msg("codetest: falling back to the built-in demo\n");
                gPath = 0;                      /* so the demo really loads */
            }
        }
        if (!gPath) {
            gEd->language(LEX_LANG_MARKDOWN);
            gEd->text(demoText);
        }
        ch->value(gEd->language());
    }
    gWin->end();
    gWin->resizable(gEd);

    report("startup");
    /* show() -- NOT show(argc, argv). The latter runs Fl::args(), which treats
     * a bare filename as an unknown switch and dumps the FLTK help text. */
    gWin->show();
    msg("codetest: after show() -- shown=%d\n", gWin->shown());
    msg("codetest: entering Fl::run() -- close the window to exit\n");
    return Fl::run();
}
