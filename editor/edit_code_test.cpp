/*
 * edit_code_test.cpp -- Linux headless check for the edit_lex.h grammars.
 *   g++ -I. editor/edit_code_test.cpp -o /tmp/ectest && /tmp/ectest
 *
 * Covers the lexers and, importantly, the carry state stored on each
 * line's '\n' -- that is what edit_code.h relies on to relex incrementally
 * instead of re-parsing the whole buffer on every keystroke.
 */
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "edit_lex.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { \
    printf("FAIL line %d: %s\n", __LINE__, #c); failures++; } } while (0)

/* Lex a whole (multi-line) string the way edit_code.h does: one line at a
 * time, carry state parked in the style byte of each '\n'. */
static int lexText(int lang, const char *src, char *out)
{
    int total = (int)strlen(src), p = 0, st = LS_NORMAL;
    while (p <= total) {
        int e = p;
        while (e < total && src[e] != '\n') e++;
        st = lexLine(lang, st, src + p, e - p, out + p);
        if (e < total) { out[e] = (char)('A' + st); p = e + 1; }
        else { p = e; break; }
    }
    out[total] = '\0';
    return st;
}

/* Style slot at the first occurrence of `needle`. */
static int slotAt(const char *src, const char *sty, const char *needle)
{
    const char *q = strstr(src, needle);
    if (!q) return -1;
    return sty[q - src] - 'A';
}

/* True if every byte of the first occurrence of `needle` carries slot `want`. */
static int allSlot(const char *src, const char *sty, const char *needle, int want)
{
    const char *q = strstr(src, needle);
    int i, n = (int)strlen(needle);
    if (!q) return 0;
    for (i = 0; i < n; i++)
        if (sty[(q - src) + i] - 'A' != want) return 0;
    return 1;
}

/* Carry state recorded on the '\n' that ends line `line` (0-based). */
static int carryAfterLine(const char *src, const char *sty, int line)
{
    int i = 0, seen = 0;
    while (src[i]) {
        if (src[i] == '\n') {
            if (seen == line) return sty[i] - 'A';
            seen++;
        }
        i++;
    }
    return -1;
}

int main(void)
{
    char sty[4096];

    /* ---------------- Pascal ---------------- */
    {
        const char *src =
            "program Hello;\n"
            "var i: Integer;\n"
            "begin\n"
            "  WriteLn('it''s here');  // trailing\n"
            "  i := $1F + 42;\n"
            "end.\n";
        lexText(LEX_LANG_PASCAL, src, sty);

        CHECK(allSlot(src, sty, "program", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "begin",   LEX_KEYWORD));
        CHECK(allSlot(src, sty, "Integer", LEX_TYPE));        /* case-insensitive */
        CHECK(allSlot(src, sty, "Hello",   LEX_PLAIN));
        CHECK(allSlot(src, sty, "'it''s here'", LEX_STRING)); /* '' escape */
        CHECK(allSlot(src, sty, "// trailing", LEX_COMMENT));
        CHECK(allSlot(src, sty, "$1F", LEX_NUMBER));
        CHECK(allSlot(src, sty, "42",  LEX_NUMBER));
    }
    /* case-insensitivity both ways */
    {
        const char *src = "BEGIN PROCEDURE Foo; end";
        lexText(LEX_LANG_PASCAL, src, sty);
        CHECK(allSlot(src, sty, "BEGIN", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "PROCEDURE", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "end", LEX_KEYWORD));
    }
    /* multi-line { } comment: the carry state is what drives incremental relex */
    {
        const char *src =
            "a;\n"
            "{ open\n"
            "still comment\n"
            "closed } b;\n"
            "begin\n";
        lexText(LEX_LANG_PASCAL, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 1) == LS_PAS_BRACE);
        CHECK(carryAfterLine(src, sty, 2) == LS_PAS_BRACE);
        CHECK(carryAfterLine(src, sty, 3) == LS_NORMAL);      /* closed again */
        CHECK(allSlot(src, sty, "still comment", LEX_COMMENT));
        CHECK(allSlot(src, sty, "begin", LEX_KEYWORD));       /* back to code */
    }
    /* (* *) comments and {$ } directives */
    {
        const char *src =
            "{$R+}\n"
            "(* two\n"
            "lines *) x;\n";
        lexText(LEX_LANG_PASCAL, src, sty);
        CHECK(allSlot(src, sty, "{$R+}", LEX_DIRECTIVE));
        CHECK(carryAfterLine(src, sty, 0) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 1) == LS_PAS_PAREN);
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "lines *)", LEX_COMMENT));
    }
    /* 1..10 is a subrange, not the float 1.  followed by .10 */
    {
        const char *src = "for i := 1..10 do";
        lexText(LEX_LANG_PASCAL, src, sty);
        CHECK(slotAt(src, sty, "1..10") == LEX_NUMBER);
        CHECK(allSlot(src, sty, "..", LEX_MARKER));
    }

    /* ---------------- Lua ---------------- */
    {
        const char *src =
            "local t = {}\n"
            "function t.go(n)\n"
            "  print(\"n=\" .. n)   -- say it\n"
            "  return 0xFF, 1e-3\n"
            "end\n";
        lexText(LEX_LANG_LUA, src, sty);

        CHECK(allSlot(src, sty, "local",    LEX_KEYWORD));
        CHECK(allSlot(src, sty, "function", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "return",   LEX_KEYWORD));
        CHECK(allSlot(src, sty, "print",    LEX_IDENT));      /* builtin */
        CHECK(allSlot(src, sty, "\"n=\"",   LEX_STRING));
        CHECK(allSlot(src, sty, "-- say it", LEX_COMMENT));
        CHECK(allSlot(src, sty, "0xFF",     LEX_NUMBER));
        CHECK(allSlot(src, sty, "1e-3",     LEX_NUMBER));
    }
    /* Lua is case-sensitive: Print is not print */
    {
        const char *src = "Print(x) Local y";
        lexText(LEX_LANG_LUA, src, sty);
        CHECK(allSlot(src, sty, "Print", LEX_PLAIN));
        CHECK(allSlot(src, sty, "Local", LEX_PLAIN));
    }
    /* long comment / long string, including levelled brackets */
    {
        const char *src =
            "x = 1\n"
            "--[[ note\n"
            "more ]]\n"
            "s = [==[ raw\n"
            "]] not closed\n"
            "]==]\n"
            "y = 2\n";
        lexText(LEX_LANG_LUA, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 1) == LS_LUA_LCOM + 0);
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 3) == LS_LUA_LSTR + 2);   /* [==[ -> level 2 */
        CHECK(carryAfterLine(src, sty, 4) == LS_LUA_LSTR + 2);   /* ]] must NOT close it */
        CHECK(carryAfterLine(src, sty, 5) == LS_NORMAL);         /* ]==] does */
        CHECK(allSlot(src, sty, "more ]]", LEX_COMMENT));
        CHECK(allSlot(src, sty, "]] not closed", LEX_STRING));
        CHECK(allSlot(src, sty, "y", LEX_PLAIN));
    }
    /* a[1] is an index, not a long-bracket string */
    {
        const char *src = "v = a[1] + b[i]";
        lexText(LEX_LANG_LUA, src, sty);
        CHECK(slotAt(src, sty, "[1]") == LEX_MARKER);
        CHECK(slotAt(src, sty, "1]")  == LEX_NUMBER);
        CHECK(carryAfterLine(src, sty, 0) == -1);   /* single line, no '\n' */
    }

    /* ---------------- Markdown ---------------- */
    {
        const char *src =
            "# Title\n"
            "Some **bold** and *em* and `code` here.\n"
            "- bullet one\n"
            "1. ordered\n"
            "> quoted\n"
            "---\n"
            "See [Dpress](https://github.com/goph-R/dynart-dpress).\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);

        CHECK(slotAt(src, sty, "#")      == LEX_MARKER);
        CHECK(allSlot(src, sty, "Title", LEX_TYPE));
        CHECK(allSlot(src, sty, "**bold**", LEX_STRONG));
        CHECK(allSlot(src, sty, "*em*",     LEX_EM));
        CHECK(allSlot(src, sty, "`code`",   LEX_CODE));
        CHECK(slotAt(src, sty, "- bullet")  == LEX_KEYWORD);
        CHECK(allSlot(src, sty, "1.",       LEX_KEYWORD));
        CHECK(allSlot(src, sty, "> quoted", LEX_COMMENT));
        CHECK(allSlot(src, sty, "---",      LEX_ERROR));
        CHECK(allSlot(src, sty, "Dpress",   LEX_IDENT));
        CHECK(allSlot(src, sty, "(https://github.com/goph-R/dynart-dpress)", LEX_KEYWORD));
    }
    /* fenced block delegating to the Lua lexer -- the Dpress nesting trick */
    {
        const char *src =
            "text\n"
            "```lua\n"
            "local x = 1  -- inside\n"
            "```\n"
            "after\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 1) == LS_MD_FENCE + 2);   /* sub = lua */
        CHECK(carryAfterLine(src, sty, 2) == LS_MD_FENCE + 2);
        CHECK(carryAfterLine(src, sty, 3) == LS_NORMAL);         /* fence closed */
        CHECK(allSlot(src, sty, "local", LEX_KEYWORD));          /* lexed as Lua */
        CHECK(allSlot(src, sty, "-- inside", LEX_COMMENT));
        CHECK(allSlot(src, sty, "after", LEX_PLAIN));
    }
    /* a fenced Pascal block, and an untagged fence stays flat green */
    {
        const char *src =
            "```pascal\n"
            "begin end;\n"
            "```\n"
            "~~~\n"
            "raw text\n"
            "~~~\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_MD_FENCE + 1);   /* sub = pascal */
        CHECK(allSlot(src, sty, "begin", LEX_KEYWORD));
        CHECK(carryAfterLine(src, sty, 3) == LS_MD_FENCE + LEX_MD_NSUB);  /* tilde, no lang */
        CHECK(allSlot(src, sty, "raw text", LEX_CODE));
        CHECK(carryAfterLine(src, sty, 5) == LS_NORMAL);
    }
    /* a ``` fence must not be closed by a ~~~ line and vice versa */
    {
        const char *src = "```\n~~~\n```\nout\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_MD_FENCE + 0);
        CHECK(carryAfterLine(src, sty, 1) == LS_MD_FENCE + 0);   /* ~~~ ignored */
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "out", LEX_PLAIN));
    }

    /* ---------------- word tables: binary search needs them sorted -------- */
    {
        static const struct { const char *const *t; int n; int fold; } tabs[] = {
            { lexPasKeywords,  LEX_COUNT(lexPasKeywords),  1 },
            { lexPasTypes,     LEX_COUNT(lexPasTypes),     1 },
            { lexLuaKeywords,  LEX_COUNT(lexLuaKeywords),  0 },
            { lexLuaBuiltins,  LEX_COUNT(lexLuaBuiltins),  0 },
            { lexCKeywords,    LEX_COUNT(lexCKeywords),    0 },
            { lexCTypes,       LEX_COUNT(lexCTypes),       0 },
            { lexJavaKeywords, LEX_COUNT(lexJavaKeywords), 0 },
            { lexJavaTypes,    LEX_COUNT(lexJavaTypes),    0 },
            { lexJsKeywords,   LEX_COUNT(lexJsKeywords),   0 },
            { lexJsBuiltins,   LEX_COUNT(lexJsBuiltins),   0 },
            { lexPyKeywords,   LEX_COUNT(lexPyKeywords),   0 },
            { lexPyBuiltins,   LEX_COUNT(lexPyBuiltins),   0 },
            { lexCNames,       LEX_COUNT(lexCNames),       1 },
            { lexJsNames,      LEX_COUNT(lexJsNames),      1 },
            { lexPyNames,      LEX_COUNT(lexPyNames),      1 },
            { lexHtmlNames,    LEX_COUNT(lexHtmlNames),    1 },
            { lexPhpKeywords,  LEX_COUNT(lexPhpKeywords),  1 },
            { lexPhpTypes,     LEX_COUNT(lexPhpTypes),     1 },
            { lexPhpNames,     LEX_COUNT(lexPhpNames),     1 },
            { lexSqlKeywords,  LEX_COUNT(lexSqlKeywords),  1 },
            { lexSqlTypes,     LEX_COUNT(lexSqlTypes),     1 },
            { lexSqlBuiltins,  LEX_COUNT(lexSqlBuiltins),  1 }
        };
        int t, k;
        for (t = 0; t < (int)(sizeof(tabs) / sizeof(tabs[0])); t++)
            for (k = 0; k < tabs[t].n; k++) {
                const char *w = tabs[t].t[k];
                if (!lexInTable(tabs[t].t, tabs[t].n, w, (int)strlen(w), tabs[t].fold)) {
                    printf("FAIL: table %d word \"%s\" not found -- table unsorted?\n", t, w);
                    failures++;
                }
            }
    }

    /* ---------------- C / C++ ---------------- */
    {
        const char *src =
            "#include <stdio.h>\n"
            "/* block\n"
            "   comment */ int main(void) {\n"
            "    unsigned long x = 0x1Fu; // done\n"
            "    return printf(\"hi\\n\");\n"
            "}\n";
        lexText(LEX_LANG_C, src, sty);
        CHECK(allSlot(src, sty, "#include", LEX_DIRECTIVE));
        CHECK(allSlot(src, sty, "<stdio.h>", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 1) == LS_CF_BLOCK);
        CHECK(allSlot(src, sty, "   comment */", LEX_COMMENT));
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "int", LEX_TYPE));
        CHECK(allSlot(src, sty, "unsigned", LEX_TYPE));
        CHECK(allSlot(src, sty, "0x1Fu", LEX_NUMBER));
        CHECK(allSlot(src, sty, "// done", LEX_COMMENT));
        CHECK(allSlot(src, sty, "return", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "\"hi\\n\"", LEX_STRING));
        CHECK(slotAt(src, sty, "printf") == LEX_PLAIN);
    }
    /* ---------------- Java ---------------- */
    {
        const char *src =
            "@Override\n"
            "public String name() { return \"x\"; }\n";
        lexText(LEX_LANG_JAVA, src, sty);
        CHECK(allSlot(src, sty, "@Override", LEX_DIRECTIVE));
        CHECK(allSlot(src, sty, "public", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "String", LEX_TYPE));   /* capitalised -> type */
        CHECK(slotAt(src, sty, "name") == LEX_PLAIN);
    }
    /* ---------------- JavaScript ---------------- */
    {
        const char *src =
            "const s = `a\n"
            "b ${x}`; console.log(1.5e-3);\n";
        lexText(LEX_LANG_JS, src, sty);
        CHECK(allSlot(src, sty, "const", LEX_KEYWORD));
        CHECK(carryAfterLine(src, sty, 0) == LS_CF_TEMPLATE);
        CHECK(allSlot(src, sty, "b ${x}`", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 1) == LS_NORMAL);
        CHECK(allSlot(src, sty, "console", LEX_IDENT));
        CHECK(allSlot(src, sty, "1.5e-3", LEX_NUMBER));
    }
    /* ---------------- Python ---------------- */
    {
        const char *src =
            "@dataclass\n"
            "def f(self):  # hi\n"
            "    \"\"\"doc\n"
            "    string\"\"\"\n"
            "    return None\n";
        lexText(LEX_LANG_PYTHON, src, sty);
        CHECK(allSlot(src, sty, "@dataclass", LEX_DIRECTIVE));
        CHECK(allSlot(src, sty, "def", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "self", LEX_IDENT));
        CHECK(allSlot(src, sty, "# hi", LEX_COMMENT));
        CHECK(carryAfterLine(src, sty, 2) == LS_CF_TDQ);
        CHECK(allSlot(src, sty, "string\"\"\"", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 3) == LS_NORMAL);
        CHECK(allSlot(src, sty, "None", LEX_KEYWORD));
    }
    /* ---------------- CSS ---------------- */
    {
        const char *src =
            "@media screen {\n"
            "  a:hover, .nav-item, #main { color: #fff; margin: -2px 50%; }\n"
            "  b { background: url(x.png) !important; }\n"
            "}\n";
        lexText(LEX_LANG_CSS, src, sty);
        CHECK(allSlot(src, sty, "@media", LEX_DIRECTIVE));
        CHECK(slotAt(src, sty, "a:hover") == LEX_PLAIN);   /* selector, not a property */
        CHECK(allSlot(src, sty, ".nav-item", LEX_TYPE));
        CHECK(allSlot(src, sty, "#main", LEX_TYPE));
        CHECK(allSlot(src, sty, "color", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "#fff", LEX_NUMBER));
        CHECK(allSlot(src, sty, "2px", LEX_NUMBER));
        CHECK(allSlot(src, sty, "50%", LEX_NUMBER));
        CHECK(allSlot(src, sty, "url", LEX_IDENT));
        CHECK(allSlot(src, sty, "!important", LEX_KEYWORD));
    }
    /* ---------------- HTML ---------------- */
    {
        const char *src =
            "<!DOCTYPE html>\n"
            "<a href=\"x.html\"\n"
            "   class='big'>Tom &amp; Jerry</a>\n"
            "<!-- note\n"
            "-->\n"
            "<script>var a = 1; /* c\n"
            "*/ </script><p>\n";
        lexText(LEX_LANG_HTML, src, sty);
        CHECK(allSlot(src, sty, "<!DOCTYPE html>", LEX_DIRECTIVE));
        CHECK(slotAt(src, sty, "a href") == LEX_TYPE);
        CHECK(allSlot(src, sty, "href", LEX_IDENT));
        CHECK(allSlot(src, sty, "\"x.html\"", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 1) == LS_HTML_TAG);
        CHECK(allSlot(src, sty, "class", LEX_IDENT));
        CHECK(allSlot(src, sty, "'big'", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "&amp;", LEX_NUMBER));
        CHECK(slotAt(src, sty, "Jerry") == LEX_PLAIN);
        CHECK(carryAfterLine(src, sty, 3) == LS_HTML_COMMENT);
        CHECK(carryAfterLine(src, sty, 4) == LS_NORMAL);
        CHECK(allSlot(src, sty, "var", LEX_KEYWORD));      /* JS inside <script> */
        CHECK(carryAfterLine(src, sty, 5) == LS_HTML_JS_BLOCK);
        CHECK(slotAt(src, sty, "script><p>") == LEX_TYPE);   /* back to HTML */
        CHECK(carryAfterLine(src, sty, 6) == LS_NORMAL);
    }
    /* ---------------- HTML: <script> template / <style> comment carry ---- */
    {
        const char *src =
            "<script>let t = `a\n"
            "b`;</script>\n"
            "<style>/* x\n"
            "y */ p { color: red; }</style>\n";
        lexText(LEX_LANG_HTML, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_HTML_JS_TPL);
        CHECK(allSlot(src, sty, "b`", LEX_STRING));
        CHECK(carryAfterLine(src, sty, 1) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 2) == LS_HTML_CSS_BLOCK);
        CHECK(allSlot(src, sty, "y */", LEX_COMMENT));
        CHECK(allSlot(src, sty, "color", LEX_KEYWORD));
        CHECK(carryAfterLine(src, sty, 3) == LS_NORMAL);
    }
    /* ---------------- SQL ---------------- */
    {
        const char *src =
            "SELECT id, COUNT(*) FROM users -- all\n"
            "WHERE name = 'it''s\n"
            "multi' /* note */ AND age > 18;\n"
            "create table t (id INT, v varchar(20));\n";
        lexText(LEX_LANG_SQL, src, sty);
        CHECK(allSlot(src, sty, "SELECT", LEX_KEYWORD));        /* case-insensitive */
        CHECK(allSlot(src, sty, "COUNT", LEX_IDENT));
        CHECK(allSlot(src, sty, "-- all", LEX_COMMENT));
        CHECK(carryAfterLine(src, sty, 1) == LS_CF_SQ);         /* string spans lines */
        CHECK(allSlot(src, sty, "multi'", LEX_STRING));
        CHECK(allSlot(src, sty, "/* note */", LEX_COMMENT));
        CHECK(allSlot(src, sty, "AND", LEX_KEYWORD));
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "create", LEX_KEYWORD));
        CHECK(allSlot(src, sty, "INT", LEX_TYPE));
        CHECK(allSlot(src, sty, "varchar", LEX_TYPE));
    }
    /* ---------------- PHP: HTML host with code islands ---------------- */
    {
        const char *src =
            "<h1><?= $title ?></h1>\n"
            "<?php\n"
            "  // hi ?> <b>back in html</b>\n"
            "<?php FOREACH ($rows as $r) { echo \"row\n"
            "  $r\"; } /* open\n"
            "  */ new Foo(); # end\n";
        lexText(LEX_LANG_PHP, src, sty);
        CHECK(allSlot(src, sty, "h1", LEX_TYPE));
        CHECK(allSlot(src, sty, "<?=", LEX_DIRECTIVE));
        CHECK(allSlot(src, sty, "$title", LEX_IDENT));
        CHECK(slotAt(src, sty, "?></h1>") == LEX_DIRECTIVE);
        CHECK(carryAfterLine(src, sty, 0) == LS_NORMAL);
        CHECK(carryAfterLine(src, sty, 1) == LS_PHP);           /* island stays open */
        CHECK(allSlot(src, sty, "// hi ", LEX_COMMENT));
        CHECK(slotAt(src, sty, "?> <b>") == LEX_DIRECTIVE);     /* ?> ends even a // comment */
        CHECK(slotAt(src, sty, "b>back") == LEX_TYPE);
        CHECK(carryAfterLine(src, sty, 2) == LS_NORMAL);
        CHECK(allSlot(src, sty, "FOREACH", LEX_KEYWORD));       /* PHP keywords fold */
        CHECK(allSlot(src, sty, "echo", LEX_KEYWORD));
        CHECK(carryAfterLine(src, sty, 3) == LS_PHP_DQ);        /* "string spans lines */
        CHECK(carryAfterLine(src, sty, 4) == LS_PHP_BLOCK);
        CHECK(allSlot(src, sty, "Foo", LEX_TYPE));
        CHECK(allSlot(src, sty, "# end", LEX_COMMENT));
        CHECK(carryAfterLine(src, sty, 5) == LS_PHP);
    }
    /* ---------------- Lua long brackets are capped at level 7 ------------- */
    {
        const char *src = "x = [=======[\nstill]=======]\n";
        lexText(LEX_LANG_LUA, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_LUA_LSTR + 7);
        CHECK(carryAfterLine(src, sty, 1) == LS_NORMAL);
    }
    /* ---------------- every carry state fits in the ASCII style byte ------ */
    CHECK('A' + LS_MAX - 1 < 0x7F);

    /* ---------------- Markdown fence with a new language ---------------- */
    {
        const char *src = "```python\ndef x(): pass\n```\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);
        CHECK(carryAfterLine(src, sty, 0) == LS_MD_FENCE + 6);
        CHECK(allSlot(src, sty, "def", LEX_KEYWORD));
    }
    {   /* ```php is code without the <?php opener */
        const char *src = "```php\n$x = new Foo();\n```\n";
        lexText(LEX_LANG_MARKDOWN, src, sty);
        CHECK(allSlot(src, sty, "$x", LEX_IDENT));
        CHECK(allSlot(src, sty, "new", LEX_KEYWORD));
    }

    /* ---------------- language from filename ---------------- */
    CHECK(lexLangFromPath("scripts/main.lua")   == LEX_LANG_LUA);
    CHECK(lexLangFromPath("unit1.PAS")          == LEX_LANG_PASCAL);
    CHECK(lexLangFromPath("prog.dpr")           == LEX_LANG_PASCAL);
    CHECK(lexLangFromPath("docs/features.md")   == LEX_LANG_MARKDOWN);
    CHECK(lexLangFromPath("level.lvl")          == LEX_LANG_TEXT);
    CHECK(lexLangFromPath("noext")              == LEX_LANG_TEXT);
    CHECK(lexLangFromPath("main.cpp")           == LEX_LANG_C);
    CHECK(lexLangFromPath("game.H")             == LEX_LANG_C);
    CHECK(lexLangFromPath("App.java")           == LEX_LANG_JAVA);
    CHECK(lexLangFromPath("ui.js")              == LEX_LANG_JS);
    CHECK(lexLangFromPath("tool.py")            == LEX_LANG_PYTHON);
    CHECK(lexLangFromPath("site.css")           == LEX_LANG_CSS);
    CHECK(lexLangFromPath("index.html")         == LEX_LANG_HTML);
    CHECK(lexLangFromPath("old.htm")            == LEX_LANG_HTML);
    CHECK(lexLangFromPath("index.php")          == LEX_LANG_PHP);
    CHECK(lexLangFromPath("schema.SQL")         == LEX_LANG_SQL);

    /* ---------------- the convergence rule edit_code.h depends on ----------
     * Lexing a line from a given state must be a pure function of that state,
     * so "recomputed carry == stored carry" is a safe place to stop. */
    {
        const char *line = "  WriteLn('x'); { open";
        char a[128], b[128];
        int sa = lexLine(LEX_LANG_PASCAL, LS_NORMAL, line, (int)strlen(line), a);
        int sb = lexLine(LEX_LANG_PASCAL, LS_NORMAL, line, (int)strlen(line), b);
        CHECK(sa == sb && sa == LS_PAS_BRACE);
        CHECK(memcmp(a, b, strlen(line)) == 0);
        /* same text, different entry state -> different result, as required */
        sb = lexLine(LEX_LANG_PASCAL, LS_PAS_BRACE, line, (int)strlen(line), b);
        CHECK(sb == LS_PAS_BRACE);
        CHECK(b[2] == LEX_CH(LEX_COMMENT));   /* all comment, not a keyword */
    }

    if (failures) { printf("\n%d FAILURE(S)\n", failures); return 1; }
    printf("edit_code_test: all checks passed\n");
    return 0;
}
