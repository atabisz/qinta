#ifndef TINTA_LAUNCH_OPTIONS_H
#define TINTA_LAUNCH_OPTIONS_H

#include <string>
#include <vector>

// THE LAUNCH-OPTION SET, AS PURE STRING LOGIC.
//
// The THIRD CATEGORY again -- the one include/win32_command_line.h names and explains. That
// header holds the CONVENTION for turning one command-line string back into argv; this one
// holds the convention for turning argv into the decisions a launch makes. Neither is a type
// that can be stood in for, neither is a call that cannot be, and both are algorithms over
// values, so both can be implemented exactly once and tested on any platform.
//
// WHY THIS IS A SEAM AND NOT A SECOND PARSER. WinMain parses thirteen named options inline,
// in a 36-line if/else chain. src/main_qt.cpp parses none of them: it reads argv[1] and opens
// it as a document. Those are not two parsers that agree, they are one parser and one file
// that has never been told the flags exist -- and the gap is not benign. `tinta --light a.md`
// on the Qt arm today tries to open a document named "--light", fails, prints the failure,
// and NEVER OPENS a.md. A flag is silently promoted to a filename and the filename is
// silently dropped. Adding a second if/else chain next door would fix that case and start the
// clock on the two chains disagreeing, so there is one chain and both arms call it.
//
// TWO THINGS THIS DELIBERATELY DOES THAT WinMain DOES NOT.
//
// First, A FLAG WHOSE ARGUMENT IS MISSING IS AN ERROR, not a no-op. WinMain guards each
// value-taking option with `&& i + 1 < argc`, so a trailing `--exportpdf` with no path fails
// every branch, falls through to the positional test, fails that too because it starts with
// '-', and VANISHES. This tree has already paid for that shape twice: `--printpages` with a
// mistyped path wrote one blank page that read as a passing arm, and `--exportpdf` with no
// path at all produced an 858-byte blank PDF with a confident hash. Those are recorded in
// `errors` here so a caller can refuse rather than proceed with a default.
//
// Second, AN UNRECOGNISED FLAG IS RECORDED rather than ignored. WinMain drops anything
// starting with '-' or '/' that it does not know. A typo like `--lite` therefore behaves
// exactly like no flag at all, which is the failure mode this tree keeps cataloguing under a
// different name each time.
//
// WHAT IS NOT HERE: any judgement about whether an option can be HONOURED. This header knows
// what the options ARE; it does not know what any particular front end can do about them.
// That split is the point -- the Qt arm can honour four and must refuse nine, the Win32 arm
// honours all thirteen, and if this file held that knowledge it would have to hold it twice.
namespace tinta {

struct LaunchOptions {
    // The positional argument: a document to open. Empty when none was given.
    std::string inputFile;

    bool lightMode = false;        // -l / --light
    bool showStats = false;        // -s / --stats
    bool forceRegister = false;    // /register / --register
    bool cascade = false;          // --cascade
    bool forceTabStrip = false;    // --tabbed
    bool startInEditMode = false;  // --edit
    bool quickNote = false;        // --new

    std::string printPagesDir;   // --printpages <dir>
    std::string dumpChromePath;  // --dumpchrome <path>
    std::string exportHtmlPath;  // --exporthtml <path>
    std::string exportDocxPath;  // --exportdocx <path>
    std::string exportPdfPath;   // --exportpdf <path>

    bool hasPos = false;  // --pos <x> <y>
    int posX = 0;
    int posY = 0;

    // Flags that matched no option, spelled as they arrived, so a caller can name them back
    // at the user. A typo belongs here and not in silence.
    std::vector<std::string> unknown;

    // Options that were recognised but whose required argument was absent. Spelled as the
    // option, not as the missing value, because that is what a message has to say.
    std::vector<std::string> missingValues;
};

// WHETHER A LEADING '/' STARTS A FLAG OR STARTS A PATH, and it is a parameter because the
// answer is genuinely different per operating system rather than being an implementation
// choice. On Windows `/register` is an option spelling and no path begins with a slash; off
// Windows EVERY absolute path begins with one.
//
// THIS EXISTS BECAUSE IT SHIPPED WRONG. The first version of this parser hardcoded
// `arg[0] == '-' || arg[0] == '/'`, lifted straight out of WinMain, where it had always been
// correct because WinMain only ever ran on Windows. Moving it into a portable file moved it
// onto a platform where it is false, and the result was that the Qt binary classified
// `/tmp/doc.md` as an unknown option and opened an empty window -- reported by
// scripts/probe-portable-linux.sh, whose own control had been PASSING because it asserted the
// ABSENCE of "cannot open" and never trying to open the file produces that absence too.
//
// It is a parameter and not an `#ifdef` inside the body so that BOTH conventions are testable
// from EITHER host. An `#ifdef` would leave each arm's behaviour provable only on that arm,
// which for a file whose entire purpose is being one parser instead of two is the wrong trade.
// `/register` is unaffected by the choice either way: it is matched by exact comparison earlier
// in the chain, so it is recognised before this question is asked.
enum class SlashPolicy {
    FlagPrefix,  // Windows: `/x` is an option
    PathPrefix,  // POSIX: `/x` is an absolute path
};

// The platform's own answer, which is what both entry points want and neither should have to
// remember. Named rather than defaulted inline so a reader can see there is a default at all.
constexpr SlashPolicy kNativeSlashPolicy =
#ifdef _WIN32
    SlashPolicy::FlagPrefix;
#else
    SlashPolicy::PathPrefix;
#endif

// Parse the arguments AFTER the program name. The exclusion is in the signature's contract
// rather than in each caller's arithmetic, because "skip argv[0]" written twice is an
// off-by-one waiting for one of the two to be edited.
LaunchOptions parseLaunchOptions(const std::vector<std::string>& args,
                                 SlashPolicy slashPolicy = kNativeSlashPolicy);

// The argv-shaped overload both entry points actually call. It performs the argv[0] skip in
// ONE place; that is its whole reason for existing.
LaunchOptions parseLaunchOptions(int argc, const char* const* argv,
                                 SlashPolicy slashPolicy = kNativeSlashPolicy);

}  // namespace tinta

#endif  // TINTA_LAUNCH_OPTIONS_H
