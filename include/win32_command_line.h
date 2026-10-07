#ifndef TINTA_WIN32_COMMAND_LINE_H
#define TINTA_WIN32_COMMAND_LINE_H

#include <string>
#include <vector>

// THE WIN32 COMMAND-LINE QUOTING RULE, AS PURE STRING LOGIC.
//
// A THIRD CATEGORY, and it is worth naming because the tree already has two and this
// belongs to neither. win32_types.h holds types, which can be stood in for off Windows
// and be genuinely equivalent. win32_api.h holds calls, which cannot -- "NOTHING HERE
// PRETENDS TO WORK". What lives here is a CONVENTION: how CreateProcessW's single
// command-line string is parsed back into argv. That is not a call and not a type, it is
// an algorithm over characters, so it can be implemented EXACTLY on any platform and
// tested on any platform -- which is the whole reason it is a header of its own rather
// than a static function inside src/platform_process_win32.cpp. That file was deleted on
// 2026-10-07 (QProcess quotes for itself), so this header has no production caller now; it
// is kept because tests/process_tests.cpp asserts it, and deleting it is a separate call.
//
// The rule is CommandLineToArgvW's, which is also what CreateProcessW's child sees
// through the CRT's startup parsing:
//
//   - A run of backslashes NOT followed by a quote is literal.
//   - A run of n backslashes followed by a quote is n/2 backslashes, and the quote is an
//     escape if n was even or a literal quote if n was odd.
//   - A quoted section groups whitespace into one argv entry.
//
// So producing it is the inverse: double every backslash that precedes a quote, escape
// the quote, and wrap in quotes whenever the argument contains whitespace, a quote, or
// nothing at all.
//
// THE EMPTY ARGUMENT IS THE ONE THAT BITES. `""` is the only spelling that survives, and
// an unquoted empty argument does not merely arrive wrong -- it VANISHES, shifting every
// later argument down one position. That is the case a round-trip test finds and an
// eyeball does not.
namespace tinta {

// Quote one argument so the child receives it as exactly one argv entry.
inline std::wstring win32QuoteArgument(const std::wstring& arg) {
    const bool needsQuotes =
        arg.empty() || arg.find_first_of(L" \t\n\v\"") != std::wstring::npos;
    if (!needsQuotes) return arg;

    std::wstring out;
    out.reserve(arg.size() + 2);
    out += L'"';
    for (size_t i = 0; i < arg.size();) {
        size_t backslashes = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++backslashes;
            ++i;
        }
        if (i == arg.size()) {
            // Trailing run: doubled, so the closing quote stays a quote rather than
            // being escaped by the last backslash.
            out.append(backslashes * 2, L'\\');
        } else if (arg[i] == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out += L'"';
            ++i;
        } else {
            out.append(backslashes, L'\\');
            out += arg[i];
            ++i;
        }
    }
    out += L'"';
    return out;
}

// Assemble the full command line for `exe` and `args`.
//
// THE EXE TOKEN IS QUOTED UNCONDITIONALLY, and the two reasons are independent. First,
// argv[0] is parsed by a DIFFERENT rule -- backslashes in it are never escapes, so a
// path is only safe inside plain quotes. Second, it is what the call site this replaced
// already did, so argv[0] stays byte-identical to what shipped and the change cannot
// regress the one token every reader would check first.
//
// The arguments are quoted MINIMALLY, which means the assembled bytes differ from the
// hand-built string this replaced: that one quoted the title value, the resource path,
// the output path and the temp file unconditionally while never quoting `-f`, `-s` or
// `-o`. No principled rule reproduces that split, so the property preserved is the
// child's ARGV rather than the byte string -- asserted by handing the output back to
// CommandLineToArgvW, the OS's own parser, rather than to a second copy of this code.
inline std::wstring win32CommandLine(const std::wstring& exe,
                                     const std::vector<std::wstring>& args) {
    std::wstring line;
    line += L'"';
    line += exe;
    line += L'"';
    for (const std::wstring& arg : args) {
        line += L' ';
        line += win32QuoteArgument(arg);
    }
    return line;
}

} // namespace tinta

#endif // TINTA_WIN32_COMMAND_LINE_H
