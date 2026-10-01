/*
 * edit_lex.h -- line-oriented syntax lexer for Pascal, Lua, Markdown, the
 * C family (C/C++, Java, JavaScript, Python, CSS, SQL, PHP code) and HTML
 * (with embedded <script>, <style> and <?php ?> islands).
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
 * The same budget caps Lua long brackets at level 7 ([=======[): a deeper
 * opener is treated as level 7, so its real closer is not recognised.
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
    LEX_LANG_MARKDOWN,
    LEX_LANG_C,          /* C and C++ */
    LEX_LANG_JAVA,
    LEX_LANG_JS,
    LEX_LANG_PYTHON,
    LEX_LANG_CSS,
    LEX_LANG_HTML,
    LEX_LANG_PHP,        /* HTML host with <?php ?> islands */
    LEX_LANG_SQL
};

/* ---- carry state -------------------------------------------------------
 * Must stay below 'A'+state == 0x7F so the style buffer remains pure ASCII;
 * Fl_Text_Buffer is UTF-8 aware and a >=0x80 byte would confuse it.  That
 * makes LS_MAX = 62 a hard ceiling -- and it is reached exactly.
 */
#define LEX_MD_NSUB 11      /* fence sub-languages: none + 10, see lexMdFenceSub */
enum {
    LS_NORMAL       = 0,
    LS_PAS_BRACE    = 1,    /* inside { ... }                        */
    LS_PAS_PAREN    = 2,    /* inside (* ... *)                      */
    LS_LUA_LSTR     = 3,    /* + level 0..7   ->  3..10              */
    LS_LUA_LCOM     = 11,   /* + level 0..7   -> 11..18              */
    LS_MD_FENCE     = 19,   /* + tilde*11 + sub -> 19..40            */
    LS_CF_BLOCK     = 41,   /* C family: inside a block comment      */
    LS_CF_TEMPLATE  = 42,   /* JS: inside a `template` string        */
    LS_CF_TSQ       = 43,   /* Python: inside a ''' string           */
    LS_CF_TDQ       = 44,   /* Python: inside a """ string           */
    LS_CF_DQ        = 45,   /* PHP / SQL: inside a "string"          */
    LS_CF_SQ        = 46,   /* PHP / SQL: inside a 'string'          */
    LS_HTML_COMMENT = 47,   /* inside <!-- ... -->                   */
    LS_HTML_TAG     = 48,   /* inside a tag spanning lines           */
    LS_HTML_JS      = 49,   /* inside <script> ... </script>         */
    LS_HTML_JS_BLOCK= 50,   /*   ... inside a block comment there    */
    LS_HTML_JS_TPL  = 51,   /*   ... inside a `template` there       */
    LS_HTML_CSS     = 52,   /* inside <style> ... </style>           */
    LS_HTML_CSS_BLOCK=53,   /*   ... inside a block comment there    */
    LS_PHP          = 54,   /* inside <?php ... ?>                   */
    LS_PHP_BLOCK    = 55,   /*   ... inside a block comment there    */
    LS_PHP_DQ       = 56,   /*   ... inside a "string" there         */
    LS_PHP_SQ       = 57,   /*   ... inside a 'string' there         */
    LS_MAX          = 58
};
#define LS_LUA_MAXLEVEL 7

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

/* ---- C family (table-driven) -------------------------------------------
 * C/C++, Java, JavaScript, Python and CSS share one lexer. They differ only in
 * comment syntax, string forms, a few sigils and their word lists, and a LexCf
 * row describes exactly that. Word tables are sorted by byte (uppercase sorts
 * before lowercase) and matched case-sensitively -- except CSS, which folds.
 */
typedef struct LexCf {
    const char *const *keywords; int nKeywords;   /* -> LEX_KEYWORD             */
    const char *const *types;    int nTypes;      /* -> LEX_TYPE                */
    const char *const *builtins; int nBuiltins;   /* -> LEX_IDENT               */
    char lineComment;   /* '/' = //, '#' = #, 'b' = both, '-' = --, 0 = none    */
    char blockComment;  /* slash-star comments, carried across lines            */
    char preproc;       /* C: #word at line start -> LEX_DIRECTIVE, <file> string */
    char backtick;      /* JS `template` strings, carried across lines          */
    char tripleQuote;   /* Python triple-quoted strings, carried across lines   */
    char atWord;        /* @Annotation / @decorator / @media -> LEX_DIRECTIVE   */
    char capsType;      /* Capitalised identifiers are types (class names)      */
    char css;           /* CSS: -idents, .class / #id, #hex, units, property:   */
    char fold;          /* word tables match case-insensitively (all lowercase) */
    char dollarVar;     /* $name is a variable -> LEX_IDENT                     */
    char multiStr;      /* '...' / "..." may span lines                          */
    char phpClose;      /* "?>" ends the code (outside strings / block comments) */
} LexCf;

static const char *const lexCKeywords[] = {
    "NULL","alignas","alignof","asm","auto","break","case","catch","class",
    "const","const_cast","constexpr","continue","decltype","default","delete",
    "do","dynamic_cast","else","enum","explicit","export","extern","false",
    "for","friend","goto","if","inline","mutable","namespace","new","noexcept",
    "nullptr","operator","override","private","protected","public","register",
    "reinterpret_cast","return","sizeof","static","static_assert","static_cast",
    "struct","switch","template","this","throw","true","try","typedef","typeid",
    "typename","union","using","virtual","volatile","while"
};
static const char *const lexCTypes[] = {
    "FILE","bool","char","char16_t","char32_t","double","float","int","int16_t",
    "int32_t","int64_t","int8_t","long","ptrdiff_t","short","signed","size_t",
    "ssize_t","uint16_t","uint32_t","uint64_t","uint8_t","unsigned","void","wchar_t"
};
static const char *const lexJavaKeywords[] = {
    "abstract","assert","break","case","catch","class","const","continue",
    "default","do","else","enum","extends","false","final","finally","for",
    "goto","if","implements","import","instanceof","interface","native","new",
    "null","package","private","protected","public","record","return","static",
    "strictfp","super","switch","synchronized","this","throw","throws",
    "transient","true","try","var","volatile","while","yield"
};
static const char *const lexJavaTypes[] = {
    "boolean","byte","char","double","float","int","long","short","void"
};
static const char *const lexJsKeywords[] = {
    "async","await","break","case","catch","class","const","continue",
    "debugger","default","delete","do","else","export","extends","false",
    "finally","for","from","function","get","if","import","in","instanceof",
    "let","new","null","of","return","set","static","super","switch","this",
    "throw","true","try","typeof","undefined","var","void","while","with","yield"
};
static const char *const lexJsBuiltins[] = {
    "console","document","globalThis","module","require","window"
};
static const char *const lexPyKeywords[] = {
    "False","None","True","and","as","assert","async","await","break","class",
    "continue","def","del","elif","else","except","finally","for","from",
    "global","if","import","in","is","lambda","nonlocal","not","or","pass",
    "raise","return","try","while","with","yield"
};
static const char *const lexPyBuiltins[] = {
    "abs","all","any","bool","bytes","dict","enumerate","filter","float",
    "format","getattr","hasattr","input","int","isinstance","len","list","map",
    "max","min","object","open","print","range","repr","reversed","round",
    "self","set","setattr","sorted","str","sum","super","tuple","type","zip"
};

static const char *const lexPhpKeywords[] = {
    "abstract","and","array","as","break","callable","case","catch","class",
    "clone","const","continue","declare","default","do","echo","else","elseif",
    "empty","enddeclare","endfor","endforeach","endif","endswitch","endwhile",
    "enum","extends","false","final","finally","fn","for","foreach","function",
    "global","goto","if","implements","include","include_once","instanceof",
    "insteadof","interface","isset","list","match","namespace","new","null",
    "or","parent","print","private","protected","public","readonly","require",
    "require_once","return","self","static","switch","throw","trait","true",
    "try","unset","use","var","while","xor","yield"
};
static const char *const lexPhpTypes[] = {
    "bool","float","int","iterable","mixed","object","string","void"
};
static const char *const lexSqlKeywords[] = {
    "add","all","alter","and","any","as","asc","auto_increment","begin",
    "between","by","cascade","case","check","column","commit","constraint",
    "create","cross","database","default","delete","desc","distinct","drop",
    "else","end","exists","false","foreign","from","full","group","having",
    "if","in","index","inner","insert","into","is","join","key","left","like",
    "limit","not","null","offset","on","or","order","outer","primary",
    "references","replace","right","rollback","select","set","table","then",
    "top","transaction","true","truncate","union","unique","update","values",
    "view","when","where","with"
};
static const char *const lexSqlTypes[] = {
    "bigint","binary","bit","blob","boolean","char","date","datetime",
    "decimal","double","float","int","integer","json","numeric","real",
    "serial","smallint","text","time","timestamp","tinyint","uuid",
    "varbinary","varchar"
};
static const char *const lexSqlBuiltins[] = {
    "avg","coalesce","concat","count","lower","max","min","now","sum","upper"
};

#define LEX_TAB(t) t, LEX_COUNT(t)
/*                               keywords                  types                  builtins                 line blk pre tpl 3q @  Aa css fold $ mstr ?> */
static const LexCf lexCfC    = { LEX_TAB(lexCKeywords),    LEX_TAB(lexCTypes),    0, 0,                     '/', 1, 1, 0, 0, 0, 0, 0,  0,  0, 0,   0 };
static const LexCf lexCfJava = { LEX_TAB(lexJavaKeywords), LEX_TAB(lexJavaTypes), 0, 0,                     '/', 1, 0, 0, 0, 1, 1, 0,  0,  0, 0,   0 };
static const LexCf lexCfJs   = { LEX_TAB(lexJsKeywords),   0, 0,                  LEX_TAB(lexJsBuiltins),   '/', 1, 0, 1, 0, 1, 1, 0,  0,  0, 0,   0 };
static const LexCf lexCfPy   = { LEX_TAB(lexPyKeywords),   0, 0,                  LEX_TAB(lexPyBuiltins),   '#', 0, 0, 0, 1, 1, 1, 0,  0,  0, 0,   0 };
static const LexCf lexCfCss  = { 0, 0,                     0, 0,                  0, 0,                     0,   1, 0, 0, 0, 1, 0, 1,  1,  0, 0,   0 };
static const LexCf lexCfSql  = { LEX_TAB(lexSqlKeywords),  LEX_TAB(lexSqlTypes),  LEX_TAB(lexSqlBuiltins), '-', 1, 0, 0, 0, 0, 0, 0,  1,  0, 1,   0 };
static const LexCf lexCfPhp  = { LEX_TAB(lexPhpKeywords),  LEX_TAB(lexPhpTypes),  0, 0,                     'b', 1, 0, 0, 0, 0, 1, 0,  1,  1, 1,   1 };

static int lexIsCssIdent(int c) { return lexIsAlnum(c) || c == '-'; }

/* Quoted string body from s[i] up to and including the closing quote q,
 * escapes honoured; stops at the end of the line otherwise. Sets *closed.
 * Returns the new i. */
static int lexQuotedBody(const char *s, int i, int n, char *out, char q, int *closed)
{
    *closed = 0;
    while (i < n) {
        if (s[i] == '\\' && i + 1 < n) {
            out[i++] = LEX_CH(LEX_STRING);
            out[i++] = LEX_CH(LEX_STRING);
            continue;
        }
        if (s[i] == q) { out[i++] = LEX_CH(LEX_STRING); *closed = 1; break; }
        out[i++] = LEX_CH(LEX_STRING);
    }
    return i;
}

/* Position of "?>" in s[i..n), or n. */
static int lexFindPhpClose(const char *s, int i, int n)
{
    for (; i + 1 < n; i++) if (s[i] == '?' && s[i + 1] == '>') return i;
    return n;
}

/* Lex one line of C-family code. If L->phpClose and a "?>" ends the code on
 * this line, styles only up to it, stores its offset in *stopAt and returns
 * LS_NORMAL; the caller styles the rest. *stopAt is -1 otherwise. */
static int lexCfLineEx(const LexCf *L, int st, const char *s, int n, char *out, int *stopAt)
{
    int i = 0, lineStart = 1;           /* only blanks so far (for # directives) */
    if (stopAt) *stopAt = -1;
    if (st != LS_CF_BLOCK && st != LS_CF_TEMPLATE && st != LS_CF_TSQ &&
        st != LS_CF_TDQ && st != LS_CF_DQ && st != LS_CF_SQ)
        st = LS_NORMAL;
    while (i < n) {
        unsigned char c;

        /* -- continuations of a construct opened on an earlier line -- */
        if (st == LS_CF_BLOCK) {
            while (i < n) {
                if (s[i] == '*' && i + 1 < n && s[i + 1] == '/') {
                    out[i++] = LEX_CH(LEX_COMMENT);
                    out[i++] = LEX_CH(LEX_COMMENT);
                    st = LS_NORMAL;
                    break;
                }
                out[i++] = LEX_CH(LEX_COMMENT);
            }
            continue;
        }
        if (st == LS_CF_TEMPLATE) {
            while (i < n) {
                if (s[i] == '\\' && i + 1 < n) {
                    out[i++] = LEX_CH(LEX_STRING);
                    out[i++] = LEX_CH(LEX_STRING);
                    continue;
                }
                if (s[i] == '`') { out[i++] = LEX_CH(LEX_STRING); st = LS_NORMAL; break; }
                out[i++] = LEX_CH(LEX_STRING);
            }
            continue;
        }
        if (st == LS_CF_DQ || st == LS_CF_SQ) {
            int closed;
            i = lexQuotedBody(s, i, n, out, st == LS_CF_SQ ? '\'' : '"', &closed);
            if (closed) st = LS_NORMAL;
            continue;
        }
        if (st == LS_CF_TSQ || st == LS_CF_TDQ) {
            char q = (st == LS_CF_TSQ) ? '\'' : '"';
            while (i < n) {
                if (s[i] == '\\' && i + 1 < n) {
                    out[i++] = LEX_CH(LEX_STRING);
                    out[i++] = LEX_CH(LEX_STRING);
                    continue;
                }
                if (s[i] == q && i + 2 < n && s[i + 1] == q && s[i + 2] == q) {
                    lexFill(out, i, i + 3, LEX_STRING);
                    i += 3;
                    st = LS_NORMAL;
                    break;
                }
                out[i++] = LEX_CH(LEX_STRING);
            }
            continue;
        }

        c = (unsigned char)s[i];
        if (c == ' ' || c == '\t') { out[i++] = LEX_CH(LEX_PLAIN); continue; }

        /* -- PHP: "?>" leaves code mode -- */
        if (L->phpClose && c == '?' && i + 1 < n && s[i + 1] == '>') {
            if (stopAt) { *stopAt = i; return LS_NORMAL; }
        }

        /* -- comments -- */
        if (L->blockComment && c == '/' && i + 1 < n && s[i + 1] == '*') {
            out[i++] = LEX_CH(LEX_COMMENT);
            out[i++] = LEX_CH(LEX_COMMENT);
            st = LS_CF_BLOCK;                /* top of loop scans for the close */
            continue;
        }
        if (((L->lineComment == '/' || L->lineComment == 'b') &&
             c == '/' && i + 1 < n && s[i + 1] == '/') ||
            ((L->lineComment == '#' || L->lineComment == 'b') && c == '#' &&
             !(L->phpClose && i + 1 < n && s[i + 1] == '[')) ||      /* PHP 8 #[Attr] */
            (L->lineComment == '-' && c == '-' && i + 1 < n && s[i + 1] == '-')) {
            /* PHP: a "?>" ends even a line comment */
            int e = L->phpClose ? lexFindPhpClose(s, i, n) : n;
            lexFill(out, i, e, LEX_COMMENT);
            i = e;
            continue;
        }
        /* -- C preprocessor: #word, then <file> as a string -- */
        if (L->preproc && c == '#' && lineStart) {
            int b = i++;
            while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
            while (i < n && lexIsAlpha((unsigned char)s[i])) i++;
            lexFill(out, b, i, LEX_DIRECTIVE);
            b = i;
            while (b < n && (s[b] == ' ' || s[b] == '\t')) out[b++] = LEX_CH(LEX_PLAIN);
            if (b < n && s[b] == '<') {
                int e = b;
                while (e < n && s[e] != '>') e++;
                if (e < n) e++;
                lexFill(out, b, e, LEX_STRING);
                b = e;
            }
            i = b;
            lineStart = 0;
            continue;
        }
        lineStart = 0;

        /* -- strings -- */
        if (L->tripleQuote && (c == '"' || c == '\'') &&
            i + 2 < n && s[i + 1] == (char)c && s[i + 2] == (char)c) {
            lexFill(out, i, i + 3, LEX_STRING);
            i += 3;
            st = (c == '\'') ? LS_CF_TSQ : LS_CF_TDQ;
            continue;
        }
        if (L->backtick && c == '`') {
            out[i++] = LEX_CH(LEX_STRING);
            st = LS_CF_TEMPLATE;
            continue;
        }
        if (c == '"' || c == '\'') {
            int closed;
            out[i] = LEX_CH(LEX_STRING);
            i = lexQuotedBody(s, i + 1, n, out, (char)c, &closed);
            if (!closed && L->multiStr) st = (c == '\'') ? LS_CF_SQ : LS_CF_DQ;
            continue;
        }

        /* -- @Annotation / @decorator / @media -- */
        if (L->atWord && c == '@' && i + 1 < n && lexIsAlpha((unsigned char)s[i + 1])) {
            int b = i++;
            while (i < n && (lexIsAlnum((unsigned char)s[i]) || s[i] == '.' ||
                             (L->css && s[i] == '-'))) i++;
            lexFill(out, b, i, LEX_DIRECTIVE);
            continue;
        }

        /* -- CSS sigils: #hex colour vs #id, .class, !important -- */
        if (L->css && c == '#') {
            int e = i + 1, hex = 1, len;
            while (e < n && lexIsCssIdent((unsigned char)s[e])) {
                if (!lexIsHex((unsigned char)s[e])) hex = 0;
                e++;
            }
            len = e - i - 1;
            hex = hex && (len == 3 || len == 4 || len == 6 || len == 8);
            lexFill(out, i, e, hex ? LEX_NUMBER : LEX_TYPE);
            i = e;
            continue;
        }
        if (L->css && c == '.' && i + 1 < n &&
            (lexIsAlpha((unsigned char)s[i + 1]) || s[i + 1] == '-')) {
            int e = i + 1;
            while (e < n && lexIsCssIdent((unsigned char)s[e])) e++;
            lexFill(out, i, e, LEX_TYPE);
            i = e;
            continue;
        }
        if (L->css && c == '!' && i + 1 < n && lexIsAlpha((unsigned char)s[i + 1])) {
            int e = i + 1;
            while (e < n && lexIsAlpha((unsigned char)s[e])) e++;
            lexFill(out, i, e, LEX_KEYWORD);
            i = e;
            continue;
        }

        /* -- numbers: 0xFF, 1.5e-3, 10UL, 1.5f, 1_000, 12px, 50% -- */
        if (lexIsDigit(c) || (c == '.' && i + 1 < n && lexIsDigit((unsigned char)s[i + 1]))) {
            int hexNum = (c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X'));
            while (i < n) {
                unsigned char d = (unsigned char)s[i];
                if (!hexNum && (d == 'e' || d == 'E') && i + 1 < n &&
                    (s[i + 1] == '+' || s[i + 1] == '-')) {
                    out[i++] = LEX_CH(LEX_NUMBER);
                    out[i++] = LEX_CH(LEX_NUMBER);
                    continue;
                }
                if (lexIsAlnum(d) || d == '.' || (L->css && d == '%')) {
                    out[i++] = LEX_CH(LEX_NUMBER);
                    continue;
                }
                break;
            }
            continue;
        }

        /* -- identifiers -- */
        if (lexIsAlpha(c) || c == '$' ||
            (L->css && c == '-' && i + 1 < n && (lexIsAlpha((unsigned char)s[i + 1]) || s[i + 1] == '-'))) {
            int b = i, len, slot = LEX_PLAIN;
            while (i < n && (lexIsAlnum((unsigned char)s[i]) || s[i] == '$' ||
                             (L->css && s[i] == '-'))) i++;
            len = i - b;
            if (L->css) {
                /* "prop: value" -- but not a:hover / ::before (alpha or ':' after) */
                int j = i;
                while (j < n && s[j] == ' ') j++;
                if (j < n && s[j] == ':' &&
                    !(j + 1 < n && (lexIsAlpha((unsigned char)s[j + 1]) || s[j + 1] == ':')))
                    slot = LEX_KEYWORD;
                else if (j < n && s[j] == '(')
                    slot = LEX_IDENT;                         /* url( rgb( calc( */
            } else if (L->dollarVar && s[b] == '$') {
                slot = LEX_IDENT;                             /* $variable */
            } else if (lexInTable(L->keywords, L->nKeywords, s + b, len, L->fold)) {
                slot = LEX_KEYWORD;
            } else if (lexInTable(L->types, L->nTypes, s + b, len, L->fold)) {
                slot = LEX_TYPE;
            } else if (lexInTable(L->builtins, L->nBuiltins, s + b, len, L->fold)) {
                slot = LEX_IDENT;
            } else if (L->capsType && s[b] >= 'A' && s[b] <= 'Z') {
                slot = LEX_TYPE;
            }
            lexFill(out, b, i, slot);
            continue;
        }

        if (c && strchr("+-*/%=&|^!~<>?:;,.()[]{}", c)) { out[i++] = LEX_CH(LEX_MARKER); continue; }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
    return st;
}

static int lexCfLine(const LexCf *L, int st, const char *s, int n, char *out)
{
    return lexCfLineEx(L, st, s, n, out, 0);
}

/* ---- HTML -------------------------------------------------------------- */

/* Case-insensitive: does s[i..n) start with the lowercase string w? */
static int lexStartsCI(const char *s, int i, int n, const char *w)
{
    while (*w) {
        if (i >= n || lexLower((unsigned char)s[i]) != (unsigned char)*w) return 0;
        i++; w++;
    }
    return 1;
}
static int lexFindCI(const char *s, int i, int n, const char *w)
{
    for (; i < n; i++) if (lexStartsCI(s, i, n, w)) return i;
    return -1;
}

/* Attributes up to and including '>' (or the end of the line). Sets *done if
 * the tag closed, *selfClose for "/>". Returns the new i. */
static int lexHtmlTagBody(const char *s, int i, int n, char *out, int *done, int *selfClose)
{
    *done = 0;
    *selfClose = 0;
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        if (c == '>') { out[i++] = LEX_CH(LEX_MARKER); *done = 1; return i; }
        if (c == '/' && i + 1 < n && s[i + 1] == '>') {
            out[i++] = LEX_CH(LEX_MARKER);
            out[i++] = LEX_CH(LEX_MARKER);
            *done = 1;
            *selfClose = 1;
            return i;
        }
        if (c == '"' || c == '\'') {
            out[i++] = LEX_CH(LEX_STRING);
            while (i < n && s[i] != (char)c) out[i++] = LEX_CH(LEX_STRING);
            if (i < n) out[i++] = LEX_CH(LEX_STRING);
            continue;
        }
        if (c == '=') { out[i++] = LEX_CH(LEX_MARKER); continue; }
        if (lexIsAlpha(c)) {
            int b = i;
            while (i < n && (lexIsAlnum((unsigned char)s[i]) || s[i] == '-' ||
                             s[i] == ':' || s[i] == '.')) i++;
            lexFill(out, b, i, LEX_IDENT);
            continue;
        }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
    return i;
}

/* Inner C-family state <-> HTML-level state for each embedded language. */
static int lexHtmlJsState(int inner)
{
    if (inner == LS_CF_BLOCK)    return LS_HTML_JS_BLOCK;
    if (inner == LS_CF_TEMPLATE) return LS_HTML_JS_TPL;
    return LS_HTML_JS;
}
static int lexHtmlPhpState(int inner)
{
    if (inner == LS_CF_BLOCK) return LS_PHP_BLOCK;
    if (inner == LS_CF_DQ)    return LS_PHP_DQ;
    if (inner == LS_CF_SQ)    return LS_PHP_SQ;
    return LS_PHP;
}

/* HTML, and PHP when php is set: <?php / <?= / <? open a code island that
 * runs to "?>" (or to the end of the file, as in a pure-PHP file). */
static int lexHtmlLineEx(int st, const char *s, int n, char *out, int php)
{
    int i = 0;
    if (st < LS_HTML_COMMENT || st > LS_PHP_SQ || (!php && st >= LS_PHP)) st = LS_NORMAL;
    while (i < n) {
        /* -- inside <?php ... ?> -- */
        if (st >= LS_PHP && st <= LS_PHP_SQ) {
            int stop, inner;
            inner = (st == LS_PHP_BLOCK) ? LS_CF_BLOCK :
                    (st == LS_PHP_DQ)    ? LS_CF_DQ    :
                    (st == LS_PHP_SQ)    ? LS_CF_SQ    : LS_NORMAL;
            inner = lexCfLineEx(&lexCfPhp, inner, s + i, n - i, out + i, &stop);
            if (stop >= 0) {
                i += stop;
                out[i++] = LEX_CH(LEX_DIRECTIVE);       /* ?> */
                out[i++] = LEX_CH(LEX_DIRECTIVE);
                st = LS_NORMAL;
            } else {
                i = n;
                st = lexHtmlPhpState(inner);
            }
            continue;
        }
        /* -- inside <!-- --> -- */
        if (st == LS_HTML_COMMENT) {
            int e = lexFindCI(s, i, n, "-->");
            int end = (e >= 0) ? e + 3 : n;
            lexFill(out, i, end, LEX_COMMENT);
            i = end;
            if (e >= 0) st = LS_NORMAL;
            continue;
        }
        /* -- inside a tag that started on an earlier line -- */
        if (st == LS_HTML_TAG) {
            int done, selfClose;
            i = lexHtmlTagBody(s, i, n, out, &done, &selfClose);
            if (done) st = LS_NORMAL;
            continue;
        }
        /* -- <script> / <style> bodies: hand the span to the JS / CSS lexer -- */
        if (st >= LS_HTML_JS && st <= LS_HTML_CSS_BLOCK) {
            int isCss = (st >= LS_HTML_CSS);
            int e     = lexFindCI(s, i, n, isCss ? "</style" : "</script");
            int end   = (e >= 0) ? e : n;
            int inner = (st == LS_HTML_JS_BLOCK || st == LS_HTML_CSS_BLOCK) ? LS_CF_BLOCK :
                        (st == LS_HTML_JS_TPL) ? LS_CF_TEMPLATE : LS_NORMAL;
            inner = lexCfLine(isCss ? &lexCfCss : &lexCfJs, inner, s + i, end - i, out + i);
            i = end;
            if (e >= 0) st = LS_NORMAL;          /* the closing tag is lexed below */
            else if (isCss) st = (inner == LS_CF_BLOCK) ? LS_HTML_CSS_BLOCK : LS_HTML_CSS;
            else st = lexHtmlJsState(inner);
            continue;
        }

        /* -- <?php / <?= / <? opens a PHP island -- */
        if (php && s[i] == '<' && i + 1 < n && s[i + 1] == '?') {
            int len = lexStartsCI(s, i, n, "<?php") ? 5 : (i + 2 < n && s[i + 2] == '=') ? 3 : 2;
            lexFill(out, i, i + len, LEX_DIRECTIVE);
            i += len;
            st = LS_PHP;
            continue;
        }

        if (s[i] == '<') {
            if (lexStartsCI(s, i, n, "<!--")) {
                lexFill(out, i, i + 4, LEX_COMMENT);
                i += 4;
                st = LS_HTML_COMMENT;
                continue;
            }
            if (i + 1 < n && s[i + 1] == '!') {           /* <!DOCTYPE html> */
                int e = i;
                while (e < n && s[e] != '>') e++;
                if (e < n) e++;
                lexFill(out, i, e, LEX_DIRECTIVE);
                i = e;
                continue;
            }
            if (i + 1 < n && (lexIsAlpha((unsigned char)s[i + 1]) || s[i + 1] == '/')) {
                int closing = (s[i + 1] == '/'), b, kind = 0, done, selfClose;
                out[i++] = LEX_CH(LEX_MARKER);
                if (closing) out[i++] = LEX_CH(LEX_MARKER);
                b = i;
                while (i < n && (lexIsAlnum((unsigned char)s[i]) || s[i] == '-' || s[i] == ':')) i++;
                lexFill(out, b, i, LEX_TYPE);
                if (!closing && i - b == 6 && lexStartsCI(s, b, n, "script")) kind = 1;
                if (!closing && i - b == 5 && lexStartsCI(s, b, n, "style"))  kind = 2;
                i = lexHtmlTagBody(s, i, n, out, &done, &selfClose);
                if (!done) st = LS_HTML_TAG;
                else if (kind && !selfClose) st = (kind == 1) ? LS_HTML_JS : LS_HTML_CSS;
                continue;
            }
        }
        if (s[i] == '&') {                                  /* &amp; &#169; */
            int e = i + 1;
            while (e < n && (lexIsAlnum((unsigned char)s[e]) || s[e] == '#')) e++;
            if (e < n && s[e] == ';' && e > i + 1) {
                lexFill(out, i, e + 1, LEX_NUMBER);
                i = e + 1;
                continue;
            }
        }
        out[i++] = LEX_CH(LEX_PLAIN);
    }
    return st;
}

static int lexHtmlLine(int st, const char *s, int n, char *out)
{
    return lexHtmlLineEx(st, s, n, out, 0);
}

/* ---- Markdown ---------------------------------------------------------- */
static const char *const lexMdPascalAlias[] = {
    "delphi","fpc","objectpascal","pas","pascal"
};
static const char *const lexMdLuaAlias[] = { "lua" };

/* Fence tags and file extensions in one table per language (sorted). */
static const char *const lexCNames[]    = { "c","cc","cpp","cxx","h","hh","hpp","hxx" };
static const char *const lexJavaNames[] = { "java" };
static const char *const lexJsNames[]   = { "cjs","javascript","js","jsx","mjs" };
static const char *const lexPyNames[]   = { "py","python","pyw" };
static const char *const lexCssNames[]  = { "css" };
static const char *const lexHtmlNames[] = { "htm","html","xhtml" };
static const char *const lexPhpNames[]  = { "php","phtml" };
static const char *const lexSqlNames[]  = { "sql" };

/* Fence sub-language index <-> LEX_LANG_*. Index 0 = untagged / unknown. */
static const int lexMdSubLang[LEX_MD_NSUB] = {
    LEX_LANG_TEXT, LEX_LANG_PASCAL, LEX_LANG_LUA, LEX_LANG_C, LEX_LANG_JAVA,
    LEX_LANG_JS, LEX_LANG_PYTHON, LEX_LANG_CSS, LEX_LANG_HTML, LEX_LANG_PHP,
    LEX_LANG_SQL
};

static int lexMdFenceSub(const char *s, int len)
{
    if (lexInTable(lexMdPascalAlias, LEX_COUNT(lexMdPascalAlias), s, len, 1)) return 1;
    if (lexInTable(lexMdLuaAlias,    LEX_COUNT(lexMdLuaAlias),    s, len, 1)) return 2;
    if (lexInTable(lexCNames,        LEX_COUNT(lexCNames),        s, len, 1)) return 3;
    if (lexInTable(lexJavaNames,     LEX_COUNT(lexJavaNames),     s, len, 1)) return 4;
    if (lexInTable(lexJsNames,       LEX_COUNT(lexJsNames),       s, len, 1)) return 5;
    if (lexInTable(lexPyNames,       LEX_COUNT(lexPyNames),       s, len, 1)) return 6;
    if (lexInTable(lexCssNames,      LEX_COUNT(lexCssNames),      s, len, 1)) return 7;
    if (lexInTable(lexHtmlNames,     LEX_COUNT(lexHtmlNames),     s, len, 1)) return 8;
    if (lexInTable(lexPhpNames,      LEX_COUNT(lexPhpNames),      s, len, 1)) return 9;
    if (lexInTable(lexSqlNames,      LEX_COUNT(lexSqlNames),      s, len, 1)) return 10;
    return 0;
}

static int lexLine(int lang, int st, const char *s, int n, char *out);

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
    if (st >= LS_MD_FENCE && st < LS_MD_FENCE + 2 * LEX_MD_NSUB) {
        int f     = st - LS_MD_FENCE;          /* 0..21 */
        int sub   = f % LEX_MD_NSUB;
        char fc   = (f / LEX_MD_NSUB) ? '~' : '`';
        while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
        k = i;
        while (k < n && s[k] == fc) k++;
        if (k - i >= 3) {                       /* closing fence */
            lexFill(out, 0, n, LEX_MARKER);
            return LS_NORMAL;
        }
        if (lexMdSubLang[sub] == LEX_LANG_PHP) { lexCfLine(&lexCfPhp, LS_NORMAL, s, n, out); return st; }
        if (sub) { lexLine(lexMdSubLang[sub], LS_NORMAL, s, n, out); return st; }
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
            if (e > b) sub = lexMdFenceSub(s + b, e - b);
            lexFill(out, 0, k, LEX_MARKER);
            lexFill(out, k, n, LEX_TYPE);       /* the language tag */
            return LS_MD_FENCE + ((fc == '~') ? LEX_MD_NSUB : 0) + sub;
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
    case LEX_LANG_C:        return lexCfLine(&lexCfC,    st, s, n, out);
    case LEX_LANG_JAVA:     return lexCfLine(&lexCfJava, st, s, n, out);
    case LEX_LANG_JS:       return lexCfLine(&lexCfJs,   st, s, n, out);
    case LEX_LANG_PYTHON:   return lexCfLine(&lexCfPy,   st, s, n, out);
    case LEX_LANG_CSS:      return lexCfLine(&lexCfCss,  st, s, n, out);
    case LEX_LANG_HTML:     return lexHtmlLine(st, s, n, out);
    case LEX_LANG_PHP:      return lexHtmlLineEx(st, s, n, out, 1);
    case LEX_LANG_SQL:      return lexCfLine(&lexCfSql, st, s, n, out);
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
    /* The other languages share one name table for extensions and fence tags. */
    {
        int sub = lexMdFenceSub(d, (int)strlen(d));
        if (sub >= 3) return lexMdSubLang[sub];
    }
    return LEX_LANG_TEXT;
}

#endif /* EDIT_LEX_H */
