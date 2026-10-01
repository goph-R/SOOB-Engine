/*
 * edit_lex.h -- line-oriented syntax lexer for Pascal, Lua and Markdown.
 *
 * Deliberately has ZERO FLTK dependency so the grammars can be unit-tested
 * headless on Linux (editor/edit_code_test.cpp).  edit_code.h wraps this in an
 * Fl_Text_Editor; nothing here knows that exists.
 *
 * THE MODEL
 *
 * lexLine() lexes exactly one line and returns the lexer state that carries
 * into the NEXT line (open block comment, open Lua long bracket, open Markdown
 * fence).  It writes one style byte per input byte -- 'A' + slot, matching the
 * Fl_Text_Display style table in edit_code.h.
 *
 * That "one line in, carry state out" shape is what makes incremental
 * re-highlighting possible.  edit_code.h stores each line's carry state in the
 * style byte of that line's '\n' (never drawn, so the byte is free), which lets
 * it relex from an edit forward and STOP as soon as the recomputed state
 * matches the stored one.  Typing inside a { } comment relexes to the closing
 * brace; typing ordinary code relexes one line.  On a Pentium II that is the
 * whole performance story.
 *
 * KNOWN LIMIT: inside a Markdown fenced block the sub-lexer restarts at
 * LS_NORMAL on every line, so a Pascal { } comment spanning lines *within* a
 * fence is not carried.  Single-line { } inside a fence is fine.  Carrying it
 * would need two state values per line and there is only one free byte.
 */
#ifndef EDIT_LEX_H
#define EDIT_LEX_H

#include <string.h>

/* ---- style slots -------------------------------------------------------
 * Order MUST match codeStyleTable[] in edit_code.h.
 */
enum {
    LEX_PLAIN = 0,   /* A  default text                    */
    LEX_COMMENT,     /* B  comment / quote / markdown marker */
    LEX_KEYWORD,     /* C  keyword / list bullet / md url   */
    LEX_STRING,      /* D  string                           */
    LEX_NUMBER,      /* E  number                           */
    LEX_TYPE,        /* F  type / md heading / html tag     */
    LEX_IDENT,       /* G  builtin / md link text           */
    LEX_CODE,        /* H  md inline + fenced code          */
    LEX_MARKER,      /* I  punctuation / md syntax marker   */
    LEX_ERROR,       /* J  md thematic break / bad token    */
    LEX_DIRECTIVE,   /* K  pascal {$...} compiler directive */
    LEX_STRONG,      /* L  md **strong**  (bold face)       */
    LEX_EM,          /* M  md *emphasis*  (italic face)     */
    LEX_NSTYLES
};
#define LEX_CH(slot) ((char)('A' + (slot)))

/* ---- languages --------------------------------------------------------- */
enum {
    LEX_LANG_TEXT = 0,
    LEX_LANG_PASCAL,
    LEX_LANG_LUA,
    LEX_LANG_MARKDOWN
};

/* ---- carry state -------------------------------------------------------
 * Must stay below 'A'+state == 0x7F so the style buffer remains pure ASCII;
 * Fl_Text_Buffer is UTF-8 aware and a >=0x80 byte would confuse it.
 */
enum {
    LS_NORMAL       = 0,
    LS_PAS_BRACE    = 1,    /* inside { ... }                        */
    LS_PAS_PAREN    = 2,    /* inside (* ... *)                      */
    LS_LUA_LSTR     = 3,    /* + level 0..15  ->  3..18              */
    LS_LUA_LCOM     = 19,   /* + level 0..15  -> 19..34              */
    LS_MD_FENCE     = 35,   /* + tilde*3 + sub -> 35..40             */
    LS_MAX          = 41
};
#define LS_LUA_MAXLEVEL 15

/* ---- tiny char helpers ------------------------------------------------- */
static int lexIsAlpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int lexIsDigit(int c) { return c >= '0' && c <= '9'; }
static int lexIsAlnum(int c) { return lexIsAlpha(c) || lexIsDigit(c); }
static int lexIsHex(int c)   { return lexIsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static int lexLower(int c)   { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static void lexFill(char *out, int from, int to, int slot)
{
    int i;
    for (i = from; i < to; i++) out[i] = LEX_CH(slot);
}

/* Binary search a sorted, all-lowercase table.  fold=1 lowercases the needle
 * (Pascal is case-insensitive; Lua is not). */
static int lexInTable(const char *const *tab, int n, const char *s, int len, int fold)
{
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1, i = 0, d = 0;
        const char *k = tab[mid];
        for (i = 0; i < len && k[i]; i++) {
            int a = fold ? lexLower((unsigned char)s[i]) : (unsigned char)s[i];
            d = a - (unsigned char)k[i];
            if (d) break;
        }
        if (!d) {
            if (i == len && !k[i]) return 1;      /* exact match */
            d = (i == len) ? -1 : 1;              /* needle is a prefix / has one */
        }
        if (d < 0) hi = mid - 1; else lo = mid + 1;
    }
    return 0;
}

#define LEX_COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ---- Pascal ------------------------------------------------------------ */
static const char *const lexPasKeywords[] = {
    "and","array","asm","begin","case","class","const","constructor",
    "destructor","div","do","downto","else","end","except","false","file",
    "finally","for","function","goto","if","implementation","in","inherited",
    "initialization","inline","interface","label","mod","nil","not","object",
    "of","on","operator","or","out","overload","override","packed","private",
    "procedure","program","property","protected","public","published","raise",
    "record","repeat","result","self","set","shl","shr","then","to","true",
    "try","type","unit","until","uses","var","virtual","while","with","xor"
};
static const char *const lexPasTypes[] = {
    "ansistring","boolean","byte","cardinal","char","comp","double","extended",
    "int64","integer","longint","longword","pchar","pointer","real","shortint",
    "shortstring","single","smallint","string","text","variant","widestring","word"
};

static int lexPascalLine(int st, const char *s, int n, char *out)
{
    int i = 0;
    while (i < n) {
        /* -- continuations of a comment opened on an earlier line -- */
        if (st == LS_PAS_BRACE) {
            while (i < n) {
                out[i] = LEX_CH(LEX_COMMENT);
                if (s[i++] == '}') { st = LS_NORMAL; break; }
            }
            continue;
        }
        if (st == LS_PAS_PAREN) {
            while (i < n) {
                if (s[i] == '*' && i + 1 < n && s[i + 1] == ')') {
                    out[i++] = LEX_CH(LEX_COMMENT);
                    out[i++] = LEX_CH(LEX_COMMENT);
                    st = LS_NORMAL;
                    break;
                }
                out[i++] = LEX_CH(LEX_COMMENT);
            }
            continue;
        }

        /* -- { comment } or {$ directive } -- */
        if (s[i] == '{') {
            int slot = (i + 1 < n && s[i + 1] == '$') ? LEX_DIRECTIVE : LEX_COMMENT;
            out[i++] = LEX_CH(slot);
            while (i < n && s[i] != '}') out[i++] = LEX_CH(slot);
            if (i < n) out[i++] = LEX_CH(slot);
            else st = LS_PAS_BRACE;          /* unterminated: carries over */
            continue;
        }
        /* -- (* comment *) -- */
        if (s[i] == '(' && i + 1 < n && s[i + 1] == '*') {
            out[i++] = LEX_CH(LEX_COMMENT);
            out[i++] = LEX_CH(LEX_COMMENT);
            st = LS_PAS_PAREN;               /* top of loop finishes the line */
            continue;
        }
        /* -- // line comment -- */
        if (s[i] == '/' && i + 1 < n && s[i + 1] == '/') {
            lexFill(out, i, n, LEX_COMMENT);
            i = n;
            continue;
        }
        /* -- 'string' with '' escape (never spans lines in Pascal) -- */
        if (s[i] == '\'') {
            out[i++] = LEX_CH(LEX_STRING);
            while (i < n) {
                if (s[i] == '\'') {
                    out[i++] = LEX_CH(LEX_STRING);
                    if (i < n && s[i] == '\'') { out[i++] = LEX_CH(LEX_STRING); continue; }
                    break;
                }
                out[i++] = LEX_CH(LEX_STRING);
            }
            continue;
        }
        /* -- #13 / #$0D char literals, $1F hex -- */
        if (s[i] == '#' || s[i] == '$') {
            out[i++] = LEX_CH(LEX_NUMBER);
            if (i < n && s[i] == '$') out[i++] = LEX_CH(LEX_NUMBER);
            while (i < n && lexIsHex((unsigned char)s[i])) out[i++] = LEX_CH(LEX_NUMBER);
            continue;
        }
        /* -- numbers (careful: 1..10 is a range, not a float) -- */
        if (lexIsDigit((unsigned char)s[i])) {
            while (i < n) {
                unsigned char c = (unsigned char)s[i];
                if (lexIsDigit(c)) { out[i++] = LEX_CH(LEX_NUMBER); continue; }
                if (c == '.' && i + 1 < n && lexIsDigit((unsigned char)s[i + 1])) {
                    out[i++] = LEX_CH(LEX_NUMBER); continue;
                }
                if ((c == 'e' || c == 'E') && i + 1 < n &&
                    (lexIsDigit((unsigned char)s[i + 1]) || s[i + 1] == '+' || s[i + 1] == '-')) {
                    out[i++] = LEX_CH(LEX_NUMBER);
                    if (i < n && (s[i] == '+' || s[i] == '-')) out[i++] = LEX_CH(LEX_NUMBER);
                    continue;
                }
                break;
            }
            continue;
        }
        /* -- identifiers -- */
        if (lexIsAlpha((unsigned char)s[i])) {
            int b = i, slot;
            while (i < n && lexIsAlnum((unsigned char)s[i])) i++;
            if (lexInTable(lexPasKeywords, LEX_COUNT(lexPasKeywords), s + b, i - b, 1))
                slot = LEX_KEYWORD;
            else if (lexInTable(lexPasTypes, LEX_COUNT(lexPasTypes), s + b, i - b, 1))
                slot = LEX_TYPE;
            else
                slot = LEX_PLAIN;
            lexFill(out, b, i, slot);
            continue;
        }
        /* -- punctuation -- */
        if (s[i] && strchr(":=+-*/<>@^.,;()[]", s[i])) { out[i++] = LEX_CH(LEX_MARKER); continue; }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
    return st;
}

/* ---- Lua --------------------------------------------------------------- */
static const char *const lexLuaKeywords[] = {
    "and","break","do","else","elseif","end","false","for","function","goto",
    "if","in","local","nil","not","or","repeat","return","then","true",
    "until","while"
};
static const char *const lexLuaBuiltins[] = {
    "_G","assert","collectgarbage","coroutine","debug","dofile","error",
    "getmetatable","io","ipairs","load","loadstring","math","next","os",
    "package","pairs","pcall","print","rawequal","rawget","rawlen","rawset",
    "require","select","self","setmetatable","string","table","tonumber",
    "tostring","type","unpack","xpcall"
};

/* Length of a Lua long-bracket opener at s[i] ( [[ , [=[ , [==[ ... ), or 0.
 * Sets *level to the number of '=' signs. */
static int lexLuaLongOpen(const char *s, int i, int n, int *level)
{
    int j = i, k = 0;
    if (j >= n || s[j] != '[') return 0;
    j++;
    while (j < n && s[j] == '=') { k++; j++; }
    if (j < n && s[j] == '[') { *level = k; return j + 1 - i; }
    return 0;
}

static int lexLuaLine(int st, const char *s, int n, char *out)
{
    int i = 0;
    while (i < n) {
        /* -- continuation of a long string / long comment -- */
        if (st >= LS_LUA_LSTR && st < LS_MD_FENCE) {
            int isCom = (st >= LS_LUA_LCOM);
            int lvl   = isCom ? st - LS_LUA_LCOM : st - LS_LUA_LSTR;
            int slot  = isCom ? LEX_COMMENT : LEX_STRING;
            while (i < n) {
                if (s[i] == ']') {
                    int j = i + 1, k = 0;
                    while (j < n && s[j] == '=') { k++; j++; }
                    if (k == lvl && j < n && s[j] == ']') {
                        while (i <= j) out[i++] = LEX_CH(slot);
                        st = LS_NORMAL;
                        break;
                    }
                }
                out[i++] = LEX_CH(slot);
            }
            continue;
        }

        /* -- comment: --[[ long ]] or -- to end of line -- */
        if (s[i] == '-' && i + 1 < n && s[i + 1] == '-') {
            int lvl = 0, len = lexLuaLongOpen(s, i + 2, n, &lvl);
            if (len) {
                lexFill(out, i, i + 2 + len, LEX_COMMENT);
                i += 2 + len;
                if (lvl > LS_LUA_MAXLEVEL) lvl = LS_LUA_MAXLEVEL;
                st = LS_LUA_LCOM + lvl;      /* top of loop scans for the close */
                continue;
            }
            lexFill(out, i, n, LEX_COMMENT);
            i = n;
            continue;
        }
        /* -- long string [[ ]] (a plain a[1] index returns 0 above) -- */
        {
            int lvl = 0, len = lexLuaLongOpen(s, i, n, &lvl);
            if (len) {
                lexFill(out, i, i + len, LEX_STRING);
                i += len;
                if (lvl > LS_LUA_MAXLEVEL) lvl = LS_LUA_MAXLEVEL;
                st = LS_LUA_LSTR + lvl;
                continue;
            }
        }
        /* -- short strings -- */
        if (s[i] == '"' || s[i] == '\'') {
            char q = s[i];
            out[i++] = LEX_CH(LEX_STRING);
            while (i < n) {
                if (s[i] == '\\' && i + 1 < n) {
                    out[i++] = LEX_CH(LEX_STRING);
                    out[i++] = LEX_CH(LEX_STRING);
                    continue;
                }
                if (s[i] == q) { out[i++] = LEX_CH(LEX_STRING); break; }
                out[i++] = LEX_CH(LEX_STRING);
            }
            continue;
        }
        /* -- numbers: 0xFF, 1e-5, .5 -- */
        if (lexIsDigit((unsigned char)s[i]) ||
            (s[i] == '.' && i + 1 < n && lexIsDigit((unsigned char)s[i + 1]))) {
            if (s[i] == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
                out[i++] = LEX_CH(LEX_NUMBER);
                out[i++] = LEX_CH(LEX_NUMBER);
                while (i < n && lexIsHex((unsigned char)s[i])) out[i++] = LEX_CH(LEX_NUMBER);
                continue;
            }
            while (i < n) {
                unsigned char c = (unsigned char)s[i];
                if (lexIsDigit(c) || c == '.') { out[i++] = LEX_CH(LEX_NUMBER); continue; }
                if ((c == 'e' || c == 'E') && i + 1 < n &&
                    (lexIsDigit((unsigned char)s[i + 1]) || s[i + 1] == '+' || s[i + 1] == '-')) {
                    out[i++] = LEX_CH(LEX_NUMBER);
                    if (i < n && (s[i] == '+' || s[i] == '-')) out[i++] = LEX_CH(LEX_NUMBER);
                    continue;
                }
                break;
            }
            continue;
        }
        /* -- identifiers -- */
        if (lexIsAlpha((unsigned char)s[i])) {
            int b = i, slot;
            while (i < n && lexIsAlnum((unsigned char)s[i])) i++;
            if (lexInTable(lexLuaKeywords, LEX_COUNT(lexLuaKeywords), s + b, i - b, 0))
                slot = LEX_KEYWORD;
            else if (lexInTable(lexLuaBuiltins, LEX_COUNT(lexLuaBuiltins), s + b, i - b, 0))
                slot = LEX_IDENT;
            else
                slot = LEX_PLAIN;
            lexFill(out, b, i, slot);
            continue;
        }
        if (s[i] && strchr("+-*/%^#=~<>(){}[];:,.", s[i])) { out[i++] = LEX_CH(LEX_MARKER); continue; }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
    return st;
}

/* ---- Markdown ---------------------------------------------------------- */
static const char *const lexMdPascalAlias[] = {
    "delphi","fpc","objectpascal","pas","pascal"
};
static const char *const lexMdLuaAlias[] = { "lua" };

static void lexMdInline(const char *s, int i, int n, char *out)
{
    while (i < n) {
        unsigned char c = (unsigned char)s[i];

        if (c == '`') {                                   /* `code` */
            int k = i, run, e, ok = 0;
            while (k < n && s[k] == '`') k++;
            run = k - i;
            e = k;
            while (e < n) {
                if (s[e] == '`') {
                    int m = e;
                    while (m < n && s[m] == '`') m++;
                    if (m - e == run) { e = m; ok = 1; break; }
                    e = m;
                    continue;
                }
                e++;
            }
            if (!ok) e = n;
            lexFill(out, i, e, LEX_CODE);
            i = e;
            continue;
        }
        if (c == '*' && i + 1 < n && s[i + 1] == '*') {    /* **strong** */
            int e = i + 2, ok = 0;
            while (e + 1 < n) {
                if (s[e] == '*' && s[e + 1] == '*') { e += 2; ok = 1; break; }
                e++;
            }
            if (!ok) e = n;
            lexFill(out, i, e, LEX_STRONG);
            i = e;
            continue;
        }
        if (c == '*' || c == '_') {                        /* *em* / _em_ */
            int e = i + 1, ok = 0;
            while (e < n) { if ((unsigned char)s[e] == c) { e++; ok = 1; break; } e++; }
            if (!ok) { out[i++] = LEX_CH(LEX_PLAIN); continue; }
            lexFill(out, i, e, LEX_EM);
            i = e;
            continue;
        }
        if (c == '[') {                                    /* [text](url) */
            int e = i + 1;
            while (e < n && s[e] != ']') e++;
            if (e < n) {
                out[i++] = LEX_CH(LEX_MARKER);
                lexFill(out, i, e, LEX_IDENT);
                i = e;
                out[i++] = LEX_CH(LEX_MARKER);
                if (i < n && s[i] == '(') {
                    int u = i;
                    while (u < n && s[u] != ')') u++;
                    if (u < n) u++;
                    lexFill(out, i, u, LEX_KEYWORD);
                    i = u;
                }
                continue;
            }
        }
        if (c == '<') {                                    /* <html tag> */
            int e = i + 1;
            while (e < n && s[e] != '>') e++;
            if (e < n) { e++; lexFill(out, i, e, LEX_TYPE); i = e; continue; }
        }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
}

static int lexMarkdownLine(int st, const char *s, int n, char *out)
{
    int i = 0, k;

    /* -- inside a fenced block -- */
    if (st >= LS_MD_FENCE) {
        int f     = st - LS_MD_FENCE;          /* 0..5 */
        int sub   = f % 3;
        char fc   = (f / 3) ? '~' : '`';
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        k = i;
        while (k < n && s[k] == fc) k++;
        if (k - i >= 3) {                       /* closing fence */
            lexFill(out, 0, n, LEX_MARKER);
            return LS_NORMAL;
        }
        if (sub == 1) { lexPascalLine(LS_NORMAL, s, n, out); return st; }
        if (sub == 2) { lexLuaLine(LS_NORMAL, s, n, out); return st; }
        lexFill(out, 0, n, LEX_CODE);
        return st;
    }

    while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;

    /* -- opening fence: ``` or ~~~ , optionally tagged with a language -- */
    if (i < n && (s[i] == '`' || s[i] == '~')) {
        char fc = s[i];
        k = i;
        while (k < n && s[k] == fc) k++;
        if (k - i >= 3) {
            int sub = 0, b = k, e;
            while (b < n && s[b] == ' ') b++;
            e = b;
            while (e < n && lexIsAlnum((unsigned char)s[e])) e++;
            if (e > b) {
                if (lexInTable(lexMdPascalAlias, LEX_COUNT(lexMdPascalAlias), s + b, e - b, 1)) sub = 1;
                else if (lexInTable(lexMdLuaAlias, LEX_COUNT(lexMdLuaAlias), s + b, e - b, 1)) sub = 2;
            }
            lexFill(out, 0, k, LEX_MARKER);
            lexFill(out, k, n, LEX_TYPE);       /* the language tag */
            return LS_MD_FENCE + ((fc == '~') ? 3 : 0) + sub;
        }
    }

    /* -- thematic break: --- *** ___ (must beat the list-bullet rule) -- */
    if (i < n && (s[i] == '-' || s[i] == '*' || s[i] == '_')) {
        char c0 = s[i];
        int cnt = 0;
        k = i;
        while (k < n && (s[k] == c0 || s[k] == ' ' || s[k] == '\t')) { if (s[k] == c0) cnt++; k++; }
        if (k >= n && cnt >= 3) { lexFill(out, 0, n, LEX_ERROR); return LS_NORMAL; }
    }

    /* -- ATX heading -- */
    if (i < n && s[i] == '#') {
        k = i;
        while (k < n && s[k] == '#') k++;
        if (k - i <= 6 && (k >= n || s[k] == ' ')) {
            lexFill(out, 0, i, LEX_PLAIN);
            lexFill(out, i, k, LEX_MARKER);
            lexFill(out, k, n, LEX_TYPE);
            return LS_NORMAL;
        }
    }

    /* -- blockquote -- */
    if (i < n && s[i] == '>') { lexFill(out, 0, n, LEX_COMMENT); return LS_NORMAL; }

    /* -- bullet list -- */
    if (i < n && (s[i] == '-' || s[i] == '*' || s[i] == '+') &&
        i + 1 < n && (s[i + 1] == ' ' || s[i + 1] == '\t')) {
        lexFill(out, 0, i, LEX_PLAIN);
        out[i] = LEX_CH(LEX_KEYWORD);
        lexMdInline(s, i + 1, n, out);
        return LS_NORMAL;
    }
    /* -- ordered list -- */
    if (i < n && lexIsDigit((unsigned char)s[i])) {
        k = i;
        while (k < n && lexIsDigit((unsigned char)s[k])) k++;
        if (k < n && (s[k] == '.' || s[k] == ')') && k + 1 < n && s[k + 1] == ' ') {
            lexFill(out, 0, i, LEX_PLAIN);
            lexFill(out, i, k + 1, LEX_KEYWORD);
            lexMdInline(s, k + 1, n, out);
            return LS_NORMAL;
        }
    }

    lexMdInline(s, 0, n, out);
    return LS_NORMAL;
}

/* ---- entry point ------------------------------------------------------- */

/* Lex one line.  Writes exactly n style bytes to out.  Returns the state that
 * carries into the next line. */
static int lexLine(int lang, int st, const char *s, int n, char *out)
{
    if (st < 0 || st >= LS_MAX) st = LS_NORMAL;
    switch (lang) {
    case LEX_LANG_PASCAL:   return lexPascalLine(st, s, n, out);
    case LEX_LANG_LUA:      return lexLuaLine(st, s, n, out);
    case LEX_LANG_MARKDOWN: return lexMarkdownLine(st, s, n, out);
    default:                lexFill(out, 0, n, LEX_PLAIN); return LS_NORMAL;
    }
}

/* Pick a language from a filename extension.  Returns LEX_LANG_TEXT if none. */
static int lexLangFromPath(const char *path)
{
    const char *d;
    if (!path) return LEX_LANG_TEXT;
    d = strrchr(path, '.');
    if (!d) return LEX_LANG_TEXT;
    d++;
    if (lexInTable(lexMdPascalAlias, LEX_COUNT(lexMdPascalAlias), d, (int)strlen(d), 1))
        return LEX_LANG_PASCAL;
    if (lexInTable(lexMdLuaAlias, LEX_COUNT(lexMdLuaAlias), d, (int)strlen(d), 1))
        return LEX_LANG_LUA;
    {
        static const char *const mdExt[] = { "markdown", "md", "mdown", "mkd" };
        if (lexInTable(mdExt, LEX_COUNT(mdExt), d, (int)strlen(d), 1))
            return LEX_LANG_MARKDOWN;
    }
    /* .pp / .inc / .dpr are Pascal but are not fence aliases */
    {
        static const char *const pasExt[] = { "dpr", "inc", "lpr", "pp" };
        if (lexInTable(pasExt, LEX_COUNT(pasExt), d, (int)strlen(d), 1))
            return LEX_LANG_PASCAL;
    }
    return LEX_LANG_TEXT;
}

#endif /* EDIT_LEX_H */
