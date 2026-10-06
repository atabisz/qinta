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

// THE SINGLE-INSTANCE RULE (Win11 Notepad model): a plain file launch joins the running window
// as a new tab, unless it is a quick note, a cascade relaunch, a harness switch that writes and
// exits, or the user turned tabs off. WinMain's inline condition, lifted here 2026-10-06 so the
// Qt arm (src/instance_channel_qt.cpp) and WinMain answer it with one function.
bool shouldJoinExistingWindow(const LaunchOptions& options, bool openInTabs);

// THE THREE SELF-RELAUNCHES, as argument lists, so the window that starts a second window and
// the parse that reads its command line can be tested against each other.
// src/quicknote_win32.cpp passes these exact flags through ShellExecuteW as one quoted string;
// src/window_launch_qt.cpp passes them to QProcess::startDetached as a list. All three carry
// --cascade, which is what makes WinMain skip the single-instance hand-off -- the reason a Qt
// relaunch needs no IPC. The torn-off tab's builder arrived one slice after the other two, when
// the Qt tab drag started detaching and became its caller.
std::vector<std::string> quickNoteRelaunchArgs();                            // Ctrl+Shift+N
std::vector<std::string> documentRelaunchArgs(const std::string& utf8Path);  // a note window
std::vector<std::string> tornOffRelaunchArgs(int x, int y,
                                             const std::string& utf8Path);  // a torn-off tab

// THE DESTRUCTIVE HALF OF A LAUNCH, AND THE GUARD THE PARSER ALONE CANNOT GIVE IT.
//
// Five of the thirteen options WRITE A FILE AND EXIT: --printpages, --dumpchrome, --exporthtml,
// --exportdocx, --exportpdf. Every one of them names its OUTPUT, and on a command line where the
// input is also a path the two are indistinguishable by shape -- `--exportpdf README.md` reads
// like it names the thing to convert. On 2026-09-20 that cost a tracked file: from the repo root,
// `tinta --lite --exportpdf README.md` replaced a 2,357-line README with a 4,427,206-byte
// placeholder PDF, at EXIT 0, and the single line on stderr was about the unrelated `--lite`
// typo. It went unnoticed for about three hours, and the only detector in the tree turned out to
// be scripts/check-readme-figures.sh reporting eighteen misses.
//
// THE PARSER ALREADY KNEW. `--lite` landed in `unknown` and src/main_d2d.cpp printed it and then
// ran the export anyway -- so every fact needed to refuse was collected, reported to the user, and
// discarded one statement later. Nothing had to be DETECTED to prevent this; something had to ACT
// on what was already detected. That asymmetry is why the fix is this small.
//
// WHY NOT SIMPLY REFUSE TO OVERWRITE AN EXISTING FILE, which is the standard CLI convention and
// was the first design. Because the byte-identity rigs that are this tree's primary falsifier all
// re-export to a FIXED scratch path on every run -- scripts/pdfmask.py writes
// build-win-split/pdfscratch/m1.pdf every single invocation, and the PDF, HTML, DOCX and
// printpages harnesses do the same -- so refuse-on-existence breaks all of them the first time it
// runs, and gets reverted inside a slice with the defect's name still on it. The class worth
// guarding is not "overwrites something". It is "overwrites a DOCUMENT", and the signature of the
// incident was never that README.md existed: it was that README.md is an INPUT FORMAT with an
// output flag pointed at it. Re-exporting m1.pdf over m1.pdf is the harness working correctly.
//
// AND WHY THERE IS NO --force. An override would take the option set from thirteen names across
// sixteen spellings to fourteen across seventeen, moving every figure README.md derives from that
// census -- to buy an escape hatch from a rule narrow enough not to need one. The extension test
// is the escape hatch: name your output with the extension of the thing you are writing.
enum class OutputRefusal {
    None,
    CommandLineNotUnderstood,  // a typo or a valueless flag: do not write on a guess
    SameAsInputDocument,       // the output path IS the document being read
    ExistingDocument,          // the output path is a markdown document already on disk
};

// The destructive action this command line will perform, if any. Empty `flag` means the launch
// writes nothing and no guard applies. When several are given the FIRST one src/main_d2d.cpp
// would reach is reported, and the order here mirrors that dispatch chain rather than the parse
// chain, because the dispatch order is what decides which write actually happens.
struct PlannedOutput {
    std::string flag;
    std::string path;
};

PlannedOutput plannedOutput(const LaunchOptions& options);

// Would writing this path destroy one of this app's own documents? Extension membership only, so
// it is pure and needs no filesystem. `.txt` is deliberately NOT a member: it is a plausible
// --dumpchrome target, and this app's documents are markdown, so including it would trade a real
// false-positive risk for no additional catch.
bool isDocumentPath(const std::string& path);

// The whole decision. `outputExists` is the ONE fact the host has to supply -- everything else is
// string logic over the parse result, which is what keeps this assertable with no filesystem and
// no platform, on either host. A caller with nothing to write may pass false.
OutputRefusal refuseOutput(const LaunchOptions& options, bool outputExists);

// The reason, as a message tail. Present tense, no trailing punctuation, so a caller can compose
// it into whatever sentence it prints.
const char* outputRefusalMessage(OutputRefusal refusal);

}  // namespace tinta

#endif  // TINTA_LAUNCH_OPTIONS_H
