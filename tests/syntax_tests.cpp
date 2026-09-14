// Assertions for src/syntax.cpp -- the code-block highlighter -- on both platforms.
//
// WHY THIS TARGET EXISTS. syntax.cpp had no test on EITHER platform. It is 392
// lines of pure logic: ten keyword sets, a language-id lookup, a hand-written
// lexer and a colour switch. Its own header describes fixing 9 errors that only
// a non-Windows compiler surfaced -- which means it was MADE portable and then
// parked in the portable_compile_only OBJECT library, where nothing would notice
// if it stopped being either portable or correct. A compiling file is not a
// tested file, and this target closes the second half of that gap.
//
// It needs no App, no device and no seam, so it is a plain executable on both
// platforms with src/syntax.cpp on the link line.
//
// THE RECONSTRUCTION INVARIANT is the reason this file is worth its length.
// tokenizeLine must partition the line: concatenating every token's text, in
// order, has to reproduce the input exactly. Every path emits contiguous text --
// including the early returns for comments -- so the invariant holds
// unconditionally, and lex() below asserts it on every single line this file
// tokenizes. That catches the whole family of off-by-one bugs a hand-written
// lexer invites (a dropped character, a duplicated one, a token whose length
// overruns the next) without anyone having to predict which one.
//
// LIFETIME, because it is a real trap in this API: SyntaxToken::text is a
// std::wstring_view into the caller's string, not a copy. lex() copies each view
// into an owned std::wstring before returning, so the Tok values below are safe
// to hold; a caller that keeps the raw std::vector<SyntaxToken> past the life of
// the line it was built from has a dangling view.
//
// ASCII ONLY, deliberately. The lexer classifies with std::iswalpha, std::iswdigit
// and std::iswspace, whose answers for non-ASCII depend on the active locale --
// which differs between the two platforms this file has to agree on. Asserting a
// CJK identifier here would be asserting a locale, not the lexer.
//
// WHAT THIS DOES NOT COVER: nothing here draws. getTokenColor is asserted as a
// mapping over a synthetic theme; whether those colours reach a pixel is the
// render seam's business and is asserted separately.

#include "syntax.h"

#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

static int failures = 0;
static int checks = 0;

static const char* typeName(SyntaxTokenType t) {
    switch (t) {
        case SyntaxTokenType::Plain:       return "Plain";
        case SyntaxTokenType::Keyword:     return "Keyword";
        case SyntaxTokenType::String:      return "String";
        case SyntaxTokenType::Comment:     return "Comment";
        case SyntaxTokenType::Number:      return "Number";
        case SyntaxTokenType::Function:    return "Function";
        case SyntaxTokenType::TypeName:    return "TypeName";
        case SyntaxTokenType::Operator:    return "Operator";
        case SyntaxTokenType::ControlFlow: return "ControlFlow";
    }
    return "?";
}

static void checkBool(bool got, bool want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %s, want %s\n", what,
                    got ? "true" : "false", want ? "true" : "false");
    }
}

static void checkInt(int got, int want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %d, want %d\n", what, got, want);
    }
}

static void checkSize(size_t got, size_t want, const char* what) {
    checks++;
    if (got != want) {
        failures++;
        std::printf("FAIL %s: got %zu, want %zu\n", what, got, want);
    }
}

// --- tokenizing helpers ---------------------------------------------------

struct Tok {
    std::wstring text;
    SyntaxTokenType type;
};

// Tokenize, assert the partition invariant, and hand back owned copies.
static std::vector<Tok> lex(const std::wstring& line, int language,
                            bool& inBlockComment, const char* what) {
    std::vector<SyntaxToken> raw = tokenizeLine(line, language, inBlockComment);

    std::vector<Tok> out;
    std::wstring rebuilt;
    for (const SyntaxToken& t : raw) {
        rebuilt.append(t.text);
        out.push_back({std::wstring(t.text), t.tokenType});
    }

    checks++;
    if (rebuilt != line) {
        failures++;
        // Wide text is not portably printable here, so report the shape. A
        // length mismatch means a character was dropped or duplicated; equal
        // lengths with unequal content means a token points at the wrong span.
        std::printf("FAIL %s: tokens do not reconstruct the line "
                    "(rebuilt %zu chars, line %zu chars)\n",
                    what, rebuilt.size(), line.size());
    }
    return out;
}

// The common case: no block comment carried in, and none expected out.
static std::vector<Tok> lex(const std::wstring& line, int language,
                            const char* what) {
    bool inBlock = false;
    std::vector<Tok> out = lex(line, language, inBlock, what);
    checkBool(inBlock, false, what /* ... leaves no open block comment */);
    return out;
}

static void expectTokens(const std::vector<Tok>& got,
                         std::initializer_list<Tok> want, const char* what) {
    checkSize(got.size(), want.size(), what /* ... token count */);
    size_t i = 0;
    for (const Tok& w : want) {
        if (i >= got.size()) break;
        checks++;
        if (got[i].text != w.text || got[i].type != w.type) {
            failures++;
            std::printf("FAIL %s: token %zu is %s(%zu chars), "
                        "want %s(%zu chars)\n",
                        what, i, typeName(got[i].type), got[i].text.size(),
                        typeName(w.type), w.text.size());
        }
        i++;
    }
}

// One line, one expected token list. The overwhelmingly common shape.
static void expect(const std::wstring& line, int language,
                   std::initializer_list<Tok> want, const char* what) {
    expectTokens(lex(line, language, what), want, what);
}

// --- detectLanguage -------------------------------------------------------

static void testDetectLanguage() {
    // Every alias of every id, enumerated. A dropped alias is a code block that
    // silently stops highlighting, which is invisible unless something counts.
    checkInt(detectLanguage(L"cpp"), 1, "cpp -> 1");
    checkInt(detectLanguage(L"c++"), 1, "c++ -> 1");
    checkInt(detectLanguage(L"c"), 1, "c -> 1");
    checkInt(detectLanguage(L"h"), 1, "h -> 1");
    checkInt(detectLanguage(L"hpp"), 1, "hpp -> 1");
    checkInt(detectLanguage(L"cxx"), 1, "cxx -> 1");

    checkInt(detectLanguage(L"python"), 2, "python -> 2");
    checkInt(detectLanguage(L"py"), 2, "py -> 2");

    checkInt(detectLanguage(L"javascript"), 3, "javascript -> 3");
    checkInt(detectLanguage(L"js"), 3, "js -> 3");
    checkInt(detectLanguage(L"jsx"), 3, "jsx -> 3");
    checkInt(detectLanguage(L"ts"), 3, "ts -> 3");
    checkInt(detectLanguage(L"typescript"), 3, "typescript -> 3");
    checkInt(detectLanguage(L"tsx"), 3, "tsx -> 3");
    // JSON deliberately shares the JavaScript rules -- strings, numbers, null
    // all read correctly -- rather than having a table of its own.
    checkInt(detectLanguage(L"json"), 3, "json -> 3 (shares the JS rules)");

    checkInt(detectLanguage(L"rust"), 4, "rust -> 4");
    checkInt(detectLanguage(L"rs"), 4, "rs -> 4");

    checkInt(detectLanguage(L"go"), 5, "go -> 5");
    checkInt(detectLanguage(L"golang"), 5, "golang -> 5");

    checkInt(detectLanguage(L"bash"), 6, "bash -> 6");
    checkInt(detectLanguage(L"shell"), 6, "shell -> 6");
    checkInt(detectLanguage(L"sh"), 6, "sh -> 6");
    checkInt(detectLanguage(L"zsh"), 6, "zsh -> 6");

    checkInt(detectLanguage(L"csharp"), 7, "csharp -> 7");
    checkInt(detectLanguage(L"cs"), 7, "cs -> 7");
    checkInt(detectLanguage(L"c#"), 7, "c# -> 7");

    // Case-insensitive, via std::towlower over the whole tag.
    checkInt(detectLanguage(L"CPP"), 1, "CPP -> 1 (case-insensitive)");
    checkInt(detectLanguage(L"PyThOn"), 2, "PyThOn -> 2 (case-insensitive)");
    checkInt(detectLanguage(L"C#"), 7, "C# -> 7 (case-insensitive)");

    // Unknown tags are 0, which is a real id: getKeywordsForLanguage(0) is null
    // and the lexer still runs, so an unknown fence is plain-but-structured
    // rather than unhighlighted.
    checkInt(detectLanguage(L""), 0, "empty tag -> 0");
    checkInt(detectLanguage(L"ruby"), 0, "ruby -> 0 (not supported)");
    checkInt(detectLanguage(L"cpp "), 0, "no trimming: trailing space -> 0");
    checkInt(detectLanguage(L"c++11"), 0, "c++11 -> 0 (exact match only)");
}

// --- getKeywordsForLanguage ----------------------------------------------

static void testKeywordsForLanguage() {
    // Pointer identity against the exported sets, so a swapped switch arm fails
    // even though both arms return a plausible non-null keyword table.
    checkBool(getKeywordsForLanguage(1) == &CPP_KEYWORDS, true, "1 -> CPP_KEYWORDS");
    checkBool(getKeywordsForLanguage(2) == &PYTHON_KEYWORDS, true, "2 -> PYTHON_KEYWORDS");
    checkBool(getKeywordsForLanguage(3) == &JS_KEYWORDS, true, "3 -> JS_KEYWORDS");
    checkBool(getKeywordsForLanguage(4) == &RUST_KEYWORDS, true, "4 -> RUST_KEYWORDS");
    checkBool(getKeywordsForLanguage(5) == &GO_KEYWORDS, true, "5 -> GO_KEYWORDS");
    checkBool(getKeywordsForLanguage(6) == &BASH_KEYWORDS, true, "6 -> BASH_KEYWORDS");
    checkBool(getKeywordsForLanguage(7) == &CSHARP_KEYWORDS, true, "7 -> CSHARP_KEYWORDS");

    checkBool(getKeywordsForLanguage(0) == nullptr, true, "0 -> null");
    checkBool(getKeywordsForLanguage(8) == nullptr, true, "8 -> null");
    checkBool(getKeywordsForLanguage(-1) == nullptr, true, "-1 -> null");

    // One marker per table that appears in NO other table, so the identity
    // checks above are anchored to content and not just to addresses.
    checkBool(CPP_KEYWORDS.count(L"co_await") == 1, true, "co_await is a C++ keyword");
    checkBool(PYTHON_KEYWORDS.count(L"elif") == 1, true, "elif is a Python keyword");
    checkBool(JS_KEYWORDS.count(L"undefined") == 1, true, "undefined is a JS keyword");
    checkBool(RUST_KEYWORDS.count(L"fn") == 1, true, "fn is a Rust keyword");
    checkBool(GO_KEYWORDS.count(L"fallthrough") == 1, true, "fallthrough is a Go keyword");
    checkBool(BASH_KEYWORDS.count(L"esac") == 1, true, "esac is a Bash keyword");
    checkBool(CSHARP_KEYWORDS.count(L"nameof") == 1, true, "nameof is a C# keyword");

    // CSHARP_CONTROL_FLOW is a SUBSET of CSHARP_KEYWORDS, which is what makes
    // the precedence in tokenizeLine matter rather than being arbitrary. If the
    // two ever diverged, a control-flow word could lose its keyword status.
    checks++;
    {
        size_t missing = 0;
        for (const std::wstring& w : CSHARP_CONTROL_FLOW) {
            if (CSHARP_KEYWORDS.count(w) == 0) missing++;
        }
        if (missing != 0) {
            failures++;
            std::printf("FAIL %zu control-flow word(s) are not C# keywords\n", missing);
        }
    }
    checkBool(CSHARP_CONTROL_FLOW.count(L"if") == 1, true, "if is C# control flow");
    checkBool(CSHARP_CONTROL_FLOW.count(L"class") == 0, true,
              "class is a C# keyword but NOT control flow");
}

// --- getTokenColor -------------------------------------------------------

static bool sameColor(const tinta::Color& a, const tinta::Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void checkColor(const tinta::Color& got, const tinta::Color& want,
                       const char* what) {
    checks++;
    if (!sameColor(got, want)) {
        failures++;
        std::printf("FAIL %s: got rgba(%.3f,%.3f,%.3f,%.3f), "
                    "want rgba(%.3f,%.3f,%.3f,%.3f)\n",
                    what, got.r, got.g, got.b, got.a, want.r, want.g, want.b, want.a);
    }
}

static void testGetTokenColor() {
    // Every field distinct, so a mis-wired switch arm cannot pass by returning
    // a colour that happens to match. D2DTheme is a plain aggregate with no
    // device handles, so it is constructible here field by field: zero-init and
    // then name what matters, which also survives a field being added.
    D2DTheme theme{};
    theme.name = L"synthetic";
    theme.text = tinta::Color{0.90f, 0.0f, 0.0f, 1.0f};
    theme.code = tinta::Color{0.10f, 0.0f, 0.0f, 1.0f};
    theme.syntaxKeyword = tinta::Color{0.20f, 0.0f, 0.0f, 1.0f};
    theme.syntaxString = tinta::Color{0.30f, 0.0f, 0.0f, 1.0f};
    theme.syntaxComment = tinta::Color{0.40f, 0.0f, 0.0f, 1.0f};
    theme.syntaxNumber = tinta::Color{0.50f, 0.0f, 0.0f, 1.0f};
    theme.syntaxFunction = tinta::Color{0.60f, 0.0f, 0.0f, 1.0f};
    theme.syntaxType = tinta::Color{0.70f, 0.0f, 0.0f, 1.0f};
    theme.syntaxControlFlow = tinta::Color{0.80f, 0.0f, 0.0f, 1.0f};

    checkColor(getTokenColor(theme, SyntaxTokenType::Keyword),
               theme.syntaxKeyword, "Keyword -> syntaxKeyword");
    checkColor(getTokenColor(theme, SyntaxTokenType::String),
               theme.syntaxString, "String -> syntaxString");
    checkColor(getTokenColor(theme, SyntaxTokenType::Comment),
               theme.syntaxComment, "Comment -> syntaxComment");
    checkColor(getTokenColor(theme, SyntaxTokenType::Number),
               theme.syntaxNumber, "Number -> syntaxNumber");
    checkColor(getTokenColor(theme, SyntaxTokenType::Function),
               theme.syntaxFunction, "Function -> syntaxFunction");
    checkColor(getTokenColor(theme, SyntaxTokenType::TypeName),
               theme.syntaxType, "TypeName -> syntaxType");
    checkColor(getTokenColor(theme, SyntaxTokenType::ControlFlow),
               theme.syntaxControlFlow, "ControlFlow -> syntaxControlFlow");

    // Operator and Plain both fall to theme.code, NOT theme.text. That is the
    // one mapping in the switch worth spelling out: code-block body text is
    // coloured by the code colour, so a theme that sets text and code
    // differently keeps its code blocks self-consistent. theme.text is set to a
    // distinct value above purely so this assertion can fail if that changes.
    checkColor(getTokenColor(theme, SyntaxTokenType::Operator),
               theme.code, "Operator -> code");
    checkColor(getTokenColor(theme, SyntaxTokenType::Plain),
               theme.code, "Plain -> code (not text)");
}

// --- tokenizeLine: the basics --------------------------------------------

static void testBasics() {
    // Empty line: no tokens at all, and the invariant still holds.
    expect(L"", 1, {}, "empty line yields no tokens");

    // A whitespace RUN is one Plain token, not one per character.
    expect(L"   ", 1, {{L"   ", SyntaxTokenType::Plain}},
           "a whitespace run is a single Plain token");
    expect(L" \t ", 1, {{L" \t ", SyntaxTokenType::Plain}},
           "mixed whitespace is still one run");

    // The declaration that exercises most of the ladder at once.
    expect(L"int x = 42;", 1,
           {{L"int", SyntaxTokenType::Keyword},
            {L" ", SyntaxTokenType::Plain},
            {L"x", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"=", SyntaxTokenType::Operator},
            {L" ", SyntaxTokenType::Plain},
            {L"42", SyntaxTokenType::Number},
            {L";", SyntaxTokenType::Operator}},
           "int x = 42;");

    // Unknown language: no keyword table, so words are Plain or Function only.
    expect(L"foo(bar)", 0,
           {{L"foo", SyntaxTokenType::Function},
            {L"(", SyntaxTokenType::Operator},
            {L"bar", SyntaxTokenType::Plain},
            {L")", SyntaxTokenType::Operator}},
           "unknown language still finds calls and punctuation");

    // Whitespace between the name and the paren does not stop it being a call.
    expect(L"foo (", 0,
           {{L"foo", SyntaxTokenType::Function},
            {L" ", SyntaxTokenType::Plain},
            {L"(", SyntaxTokenType::Operator}},
           "a call is recognised across intervening whitespace");

    // Leading underscore is an identifier start, and digits may follow.
    expect(L"_a1", 1, {{L"_a1", SyntaxTokenType::Plain}},
           "underscore starts an identifier");
}

// --- tokenizeLine: numbers ----------------------------------------------

static void testNumbers() {
    expect(L"0", 1, {{L"0", SyntaxTokenType::Number}}, "a single digit");
    expect(L"0xFF", 1, {{L"0xFF", SyntaxTokenType::Number}}, "hex literal");
    expect(L"0X1a", 1, {{L"0X1a", SyntaxTokenType::Number}},
           "capital X and mixed-case digits");

    // A bare 0x with no digits is still a Number token. Recorded as the current
    // behaviour rather than defended: the lexer's job is to colour, not to
    // reject, and a half-typed literal in an editor is the normal case.
    expect(L"0x", 1, {{L"0x", SyntaxTokenType::Number}},
           "bare 0x is a Number");
    expect(L"0xG", 1,
           {{L"0x", SyntaxTokenType::Number},
            {L"G", SyntaxTokenType::Plain}},
           "0x stops at the first non-hex digit");

    // A leading dot is a number only when a digit follows it.
    expect(L".5", 1, {{L".5", SyntaxTokenType::Number}}, "leading dot number");
    expect(L".", 1, {{L".", SyntaxTokenType::Operator}},
           "a lone dot is an operator");
    expect(L".x", 1,
           {{L".", SyntaxTokenType::Operator},
            {L"x", SyntaxTokenType::Plain}},
           "dot then letter is not a number");

    // The decimal scanner is GREEDY and accepts '.', 'e', 'E', 'f' and 'L'
    // anywhere, so malformed literals come out as one token. Asserted because
    // it is a deliberate simplification, and because tightening it would change
    // the token COUNT -- which is exactly what a reader of a diff would miss.
    expect(L"1.2.3", 1, {{L"1.2.3", SyntaxTokenType::Number}},
           "two dots are absorbed into one Number");
    expect(L"1e", 1, {{L"1e", SyntaxTokenType::Number}},
           "a trailing exponent marker is absorbed");
    expect(L"1.5f", 1, {{L"1.5f", SyntaxTokenType::Number}}, "float suffix");
    expect(L"10L", 1, {{L"10L", SyntaxTokenType::Number}}, "long suffix");

    // ...but only those characters. Any other letter ends the number and starts
    // an identifier, so 123abc is two tokens.
    expect(L"123abc", 1,
           {{L"123", SyntaxTokenType::Number},
            {L"abc", SyntaxTokenType::Plain}},
           "a number ends at an ordinary letter");
}

// --- tokenizeLine: strings ----------------------------------------------

static void testStrings() {
    expect(L"\"ab\"", 1, {{L"\"ab\"", SyntaxTokenType::String}},
           "double-quoted string");
    expect(L"'c'", 1, {{L"'c'", SyntaxTokenType::String}},
           "single-quoted string");

    // A backslash escapes the next character, so an escaped quote does not
    // close the string.
    expect(L"\"a\\\"b\"", 1, {{L"\"a\\\"b\"", SyntaxTokenType::String}},
           "an escaped quote does not close the string");

    // Unterminated: consumes the rest of the line rather than resyncing. This
    // is why a stray quote in a code block colours everything after it.
    expect(L"\"unterminated", 1,
           {{L"\"unterminated", SyntaxTokenType::String}},
           "an unterminated string runs to end of line");

    // A trailing backslash has nothing to escape, so the scanner steps over it
    // and stops at end of line rather than reading past the buffer.
    expect(L"\"a\\", 1, {{L"\"a\\", SyntaxTokenType::String}},
           "a trailing backslash does not run off the end");

    // The two quote characters do not close each other.
    expect(L"\"it's\"", 1, {{L"\"it's\"", SyntaxTokenType::String}},
           "an apostrophe inside double quotes is not a delimiter");

    expect(L"x = \"s\";", 1,
           {{L"x", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"=", SyntaxTokenType::Operator},
            {L" ", SyntaxTokenType::Plain},
            {L"\"s\"", SyntaxTokenType::String},
            {L";", SyntaxTokenType::Operator}},
           "a string in context");
}

// --- tokenizeLine: comments and the inBlockComment out-param -------------

static void testComments() {
    expect(L"// hi", 1, {{L"// hi", SyntaxTokenType::Comment}},
           "a line comment is one token");
    expect(L"x // hi", 1,
           {{L"x", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"// hi", SyntaxTokenType::Comment}},
           "a trailing line comment swallows the rest");

    // '//' is recognised in EVERY language, not only the C-like ones. In Python
    // that is floor division, so `a // b` is coloured as a comment. Asserted as
    // the current behaviour, and flagged as wrong rather than defended: fixing
    // it means gating '//' on the language, which would change these counts.
    expect(L"x = a // b", 2,
           {{L"x", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"=", SyntaxTokenType::Operator},
            {L" ", SyntaxTokenType::Plain},
            {L"a", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"// b", SyntaxTokenType::Comment}},
           "KNOWN WRONG: Python floor division reads as a comment");

    // '#' is a comment only in Python (2) and Bash (6).
    expect(L"# note", 2, {{L"# note", SyntaxTokenType::Comment}},
           "# is a Python comment");
    expect(L"# note", 6, {{L"# note", SyntaxTokenType::Comment}},
           "# is a Bash comment");

    // A lone '#' at end of line is handled by the SECOND of the two '#' checks
    // in tokenizeLine -- the first sits behind `i + 1 < length` and cannot see
    // it. That redundancy looks like dead code until this case is asserted.
    expect(L"#", 6, {{L"#", SyntaxTokenType::Comment}},
           "a lone # at end of line is still a Bash comment");
    expect(L"a #", 2,
           {{L"a", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"#", SyntaxTokenType::Comment}},
           "a trailing lone # is a Python comment");

    // In JavaScript '#' is neither a comment nor a directive, so it falls all
    // the way to the operator case.
    expect(L"#", 3, {{L"#", SyntaxTokenType::Operator}},
           "# in JS is an operator");

    // A block comment closed on the same line does not set the flag, and code
    // after it is tokenized normally.
    expect(L"/* a */ b", 1,
           {{L"/* a */", SyntaxTokenType::Comment},
            {L" ", SyntaxTokenType::Plain},
            {L"b", SyntaxTokenType::Plain}},
           "a same-line block comment, with code after it");

    // Two block comments on one line, so the loop re-enters rather than
    // treating the first close as the end of the line.
    expect(L"/*a*/x/*b*/", 1,
           {{L"/*a*/", SyntaxTokenType::Comment},
            {L"x", SyntaxTokenType::Plain},
            {L"/*b*/", SyntaxTokenType::Comment}},
           "two block comments on one line");

    // THE STATEFUL PATH: inBlockComment is how the caller carries an open
    // comment from one line to the next, and it is the only mutable state in
    // the whole file. Three lines, checking the flag after each.
    {
        bool inBlock = false;
        std::vector<Tok> t1 = lex(L"code /* open", 1, inBlock, "block line 1");
        expectTokens(t1,
                     {{L"code", SyntaxTokenType::Plain},
                      {L" ", SyntaxTokenType::Plain},
                      {L"/* open", SyntaxTokenType::Comment}},
                     "block line 1 tokens");
        checkBool(inBlock, true, "flag set by an unterminated /*");

        std::vector<Tok> t2 = lex(L"still inside", 1, inBlock, "block line 2");
        expectTokens(t2, {{L"still inside", SyntaxTokenType::Comment}},
                     "an interior line is one Comment token");
        checkBool(inBlock, true, "flag stays set across an interior line");

        std::vector<Tok> t3 = lex(L"end */ x", 1, inBlock, "block line 3");
        expectTokens(t3,
                     {{L"end */", SyntaxTokenType::Comment},
                      {L" ", SyntaxTokenType::Plain},
                      {L"x", SyntaxTokenType::Plain}},
                     "the closing line resumes code after */");
        checkBool(inBlock, false, "flag cleared by */");
    }

    // An interior line is a comment WHATEVER it contains -- keywords, strings
    // and quotes included -- because the continuation branch runs before any
    // other classification.
    {
        bool inBlock = true;
        std::vector<Tok> t = lex(L"int x = \"q\";", 1, inBlock,
                                 "interior line with code in it");
        expectTokens(t, {{L"int x = \"q\";", SyntaxTokenType::Comment}},
                     "an interior line ignores keywords and quotes");
        checkBool(inBlock, true, "flag survives code-looking interior text");
    }

    // An empty interior line produces NO tokens and leaves the flag set. The
    // loop never runs, so the continuation branch is never reached -- worth
    // asserting because it is the one interior case that emits nothing.
    {
        bool inBlock = true;
        std::vector<Tok> t = lex(L"", 1, inBlock, "empty interior line");
        expectTokens(t, {}, "an empty interior line emits no tokens");
        checkBool(inBlock, true, "an empty line does not close a block comment");
    }

    // Block comments are language-independent too, so Python gets them as well.
    // Same class of bug as '//' above, recorded the same way.
    {
        bool inBlock = false;
        std::vector<Tok> t = lex(L"a /* b", 2, inBlock, "python /* line");
        expectTokens(t,
                     {{L"a", SyntaxTokenType::Plain},
                      {L" ", SyntaxTokenType::Plain},
                      {L"/* b", SyntaxTokenType::Comment}},
                     "KNOWN WRONG: /* opens a block comment in Python too");
        checkBool(inBlock, true, "and it sets the flag, so the next line is a comment");
    }
}

// --- tokenizeLine: the classification ladder ----------------------------

static void testPrecedence() {
    // KEYWORD BEATS FUNCTION. `if (` is followed by a paren, so the call check
    // would match it; the keyword test runs first.
    expect(L"if (x)", 1,
           {{L"if", SyntaxTokenType::Keyword},
            {L" ", SyntaxTokenType::Plain},
            {L"(", SyntaxTokenType::Operator},
            {L"x", SyntaxTokenType::Plain},
            {L")", SyntaxTokenType::Operator}},
           "if is a Keyword in C++, not a Function");

    // ...and in C# the SAME word is ControlFlow, because that check runs ahead
    // of the keyword check. This pair is the whole reason the ladder is ordered.
    expect(L"if (x)", 7,
           {{L"if", SyntaxTokenType::ControlFlow},
            {L" ", SyntaxTokenType::Plain},
            {L"(", SyntaxTokenType::Operator},
            {L"x", SyntaxTokenType::Plain},
            {L")", SyntaxTokenType::Operator}},
           "if is ControlFlow in C#");

    // TYPE BEATS FUNCTION, so a constructor-style call is coloured as a type.
    expect(L"size_t(3)", 1,
           {{L"size_t", SyntaxTokenType::TypeName},
            {L"(", SyntaxTokenType::Operator},
            {L"3", SyntaxTokenType::Number},
            {L")", SyntaxTokenType::Operator}},
           "size_t(3) is a TypeName, not a Function");

    // CPP_TYPES applies to language 1 ONLY. The same word in Rust is Plain.
    expect(L"size_t", 1, {{L"size_t", SyntaxTokenType::TypeName}},
           "size_t is a TypeName in C++");
    expect(L"size_t", 4, {{L"size_t", SyntaxTokenType::Plain}},
           "size_t is Plain in Rust");

    // 'string' is in BOTH CPP_TYPES and CSHARP_KEYWORDS, so it lands on a
    // different token type per language. A single word proving the ladder is
    // consulted per-language rather than once.
    expect(L"string", 1, {{L"string", SyntaxTokenType::TypeName}},
           "string is a TypeName in C++");
    expect(L"string", 7, {{L"string", SyntaxTokenType::Keyword}},
           "string is a Keyword in C# (keyword beats CSHARP_TYPES)");

    // CSHARP_TYPES applies to language 7 only.
    expect(L"List", 7, {{L"List", SyntaxTokenType::TypeName}},
           "List is a TypeName in C#");
    expect(L"List", 1, {{L"List", SyntaxTokenType::Plain}},
           "List is Plain in C++ (not in CPP_TYPES)");

    // 'true' is a keyword in six of the seven tables, and in Bash it is a
    // command that happens to be listed too. One check that the tables are
    // actually consulted for a word that is not syntax in the C sense.
    expect(L"true", 1, {{L"true", SyntaxTokenType::Keyword}}, "true in C++");
    expect(L"true", 6, {{L"true", SyntaxTokenType::Keyword}}, "true in Bash");
    expect(L"true", 0, {{L"true", SyntaxTokenType::Plain}},
           "true is Plain with no keyword table");
}

// --- tokenizeLine: C# specifics -----------------------------------------

static void testCSharp() {
    // PASCALCASE HEURISTIC: an unknown identifier starting with an uppercase
    // letter is guessed to be a type. It is the last rung of the ladder, so it
    // only fires when nothing else matched.
    expect(L"Foo bar", 7,
           {{L"Foo", SyntaxTokenType::TypeName},
            {L" ", SyntaxTokenType::Plain},
            {L"bar", SyntaxTokenType::Plain}},
           "an unknown PascalCase word is a TypeName in C#");

    // The length > 1 guard means a single capital is NOT a type -- generic
    // parameters like T stay plain. Undocumented in the source; asserted here
    // because it is the kind of condition a tidy-up would drop.
    expect(L"X", 7, {{L"X", SyntaxTokenType::Plain}},
           "a single capital letter is Plain, not a TypeName");
    expect(L"Xy", 7, {{L"Xy", SyntaxTokenType::TypeName}},
           "two characters is enough for the PascalCase guess");

    // The heuristic is C#-only.
    expect(L"Foo", 1, {{L"Foo", SyntaxTokenType::Plain}},
           "PascalCase means nothing in C++");

    // ...and a call still wins over it, because isFunction is checked first.
    expect(L"Foo()", 7,
           {{L"Foo", SyntaxTokenType::Function},
            {L"(", SyntaxTokenType::Operator},
            {L")", SyntaxTokenType::Operator}},
           "PascalCase followed by ( is a Function, not a TypeName");

    // VERBATIM STRINGS: @"..." where "" is the escape and backslash is literal.
    expect(L"@\"a\"\"b\"", 7, {{L"@\"a\"\"b\"", SyntaxTokenType::String}},
           "@\"a\"\"b\" is one verbatim string");

    // INTERPOLATED STRINGS: $"..." with ordinary backslash escaping.
    expect(L"$\"a\\\"b\"", 7, {{L"$\"a\\\"b\"", SyntaxTokenType::String}},
           "$\"a\\\"b\" is one interpolated string");

    // THE DISCRIMINATOR between the two scanners. In a VERBATIM string the
    // backslash is literal, so the quote after it closes the string and the
    // rest of the line is separate tokens. The interpolated case above is one
    // token for the identical character sequence bar the prefix -- which is the
    // only way to tell the two code paths apart from the outside.
    expect(L"@\"a\\\"b\"", 7,
           {{L"@\"a\\\"", SyntaxTokenType::String},
            {L"b", SyntaxTokenType::Plain},
            {L"\"", SyntaxTokenType::String}},
           "in a verbatim string a backslash does not escape the quote");

    // Both prefix orders are accepted and both are treated as verbatim.
    expect(L"$@\"x\"", 7, {{L"$@\"x\"", SyntaxTokenType::String}},
           "$@\"x\" is one string");
    expect(L"@$\"x\"", 7, {{L"@$\"x\"", SyntaxTokenType::String}},
           "@$\"x\" is one string");

    // A prefix character not followed by a quote is just an operator, so
    // attributes and identifiers beginning with @ still tokenize sensibly.
    expect(L"@ x", 7,
           {{L"@", SyntaxTokenType::Operator},
            {L" ", SyntaxTokenType::Plain},
            {L"x", SyntaxTokenType::Plain}},
           "a bare @ is an operator");
    expect(L"@name", 7,
           {{L"@", SyntaxTokenType::Operator},
            {L"name", SyntaxTokenType::Plain}},
           "@name is an operator plus an identifier");

    // The @ and $ handling is language 7 only: in C++ they fall straight to the
    // operator case even when a quote follows.
    expect(L"@\"x\"", 1,
           {{L"@", SyntaxTokenType::Operator},
            {L"\"x\"", SyntaxTokenType::String}},
           "@\"x\" in C++ is an operator plus an ordinary string");
}

// --- tokenizeLine: preprocessor directives ------------------------------

static void testPreprocessor() {
    // A directive is scanned as '#' plus following word characters and then
    // looked up in the SAME keyword table as everything else -- which is why
    // CPP_KEYWORDS contains entries like L"#include".
    expect(L"#include <a>", 1,
           {{L"#include", SyntaxTokenType::Keyword},
            {L" ", SyntaxTokenType::Plain},
            {L"<", SyntaxTokenType::Operator},
            {L"a", SyntaxTokenType::Plain},
            {L">", SyntaxTokenType::Operator}},
           "#include is a Keyword");
    expect(L"#pragma", 1, {{L"#pragma", SyntaxTokenType::Keyword}},
           "#pragma is a Keyword");

    // An unrecognised directive is Plain, not Operator -- so the '#' and the
    // word stay a single token either way.
    expect(L"#foo", 1, {{L"#foo", SyntaxTokenType::Plain}},
           "an unknown directive is one Plain token");

    // A '#' with a space after it produces a one-character token.
    expect(L"# x", 1,
           {{L"#", SyntaxTokenType::Plain},
            {L" ", SyntaxTokenType::Plain},
            {L"x", SyntaxTokenType::Plain}},
           "# followed by a space is a lone Plain token in C++");

    // C# has its own directive set, including ones C++ does not have.
    expect(L"#nullable", 7, {{L"#nullable", SyntaxTokenType::Keyword}},
           "#nullable is a C# Keyword");
    expect(L"#region", 7, {{L"#region", SyntaxTokenType::Keyword}},
           "#region is a C# Keyword");
    expect(L"#nullable", 1, {{L"#nullable", SyntaxTokenType::Plain}},
           "#nullable means nothing in C++");
}

int main() {
    testDetectLanguage();
    testKeywordsForLanguage();
    testGetTokenColor();
    testBasics();
    testNumbers();
    testStrings();
    testComments();
    testPrecedence();
    testCSharp();
    testPreprocessor();

    if (failures) {
        std::printf("%d of %d syntax assertion(s) failed\n", failures, checks);
        return 1;
    }
    // Printed on SUCCESS as well as failure, deliberately. A test binary that
    // says nothing when it passes is indistinguishable from one that was never
    // built into the run, and this repo has no CI to notice the difference.
    std::printf("all %d syntax assertions run, 0 failed\n", checks);
    return 0;
}
