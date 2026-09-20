// THE LAUNCH-OPTION PARSE, AND THE DEFECT IT WAS BUILT TO FIX.
//
// The case that matters most in this file is the FIRST one, and it is not a hypothetical: as
// shipped, src/main_qt.cpp reads argv[1] and hands it to loadDocumentFromPath, so
// `tinta --light notes.md` tries to open a document named "--light", prints that it cannot,
// and never opens notes.md. A flag is promoted to a filename and the filename is dropped. The
// assertion below is what makes that impossible to reintroduce.
//
// The second thing this file is for is the CENSUS. The bottom of main() walks every option
// spelling the Win32 arm accepts and asserts the shared parse recognises it, which is how
// "no flag is silently accepted" becomes a count with a stated denominator rather than a
// claim. If somebody adds a fourteenth option to one arm, the census here is what notices.
#include "launch_options.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const char* message) {
    checks++;
    if (condition) return;
    std::printf("FAIL: %s\n", message);
    failures++;
}

tinta::LaunchOptions parse(const std::vector<std::string>& args) {
    return tinta::parseLaunchOptions(args);
}

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const std::string& e : v) {
        if (e == s) return true;
    }
    return false;
}

}  // namespace

int main() {
    // --- THE DEFECT: a flag before a filename must not eat the filename ------
    {
        const tinta::LaunchOptions o = parse({"--light", "notes.md"});
        check(o.lightMode, "--light before a path sets light mode");
        check(o.inputFile == "notes.md",
              "AND the path is still the input file -- this is the shipped Qt-arm defect: it "
              "reads argv[1] as the document, so today this opens a file named '--light' and "
              "notes.md is never opened at all");
    }

    // --- CONTROL: a bare path is still a path, with nothing else set ---------
    //
    // Without this, every assertion above is satisfiable by a parse that sets every flag on
    // every input.
    {
        const tinta::LaunchOptions o = parse({"notes.md"});
        check(o.inputFile == "notes.md", "CONTROL: a bare path is the input file");
        check(!o.lightMode && !o.showStats && !o.cascade && !o.forceTabStrip &&
                  !o.startInEditMode && !o.quickNote && !o.hasPos && !o.forceRegister,
              "CONTROL: and a bare path sets NO flag -- a parse that set them "
              "unconditionally would pass every positive case in this file");
        check(o.unknown.empty() && o.missingValues.empty(),
              "CONTROL: and a bare path produces no complaint");
    }

    // --- The order does not matter, either side of the positional -----------
    {
        const tinta::LaunchOptions before = parse({"--cascade", "--light", "a.md"});
        const tinta::LaunchOptions after = parse({"a.md", "--cascade", "--light"});
        check(before.inputFile == "a.md" && after.inputFile == "a.md",
              "a positional is found whether it comes before or after the flags");
        check(before.cascade && after.cascade && before.lightMode && after.lightMode,
              "and the flags are set in both orders");
    }

    // --- --pos takes TWO values ---------------------------------------------
    {
        const tinta::LaunchOptions o = parse({"--pos", "300", "450", "a.md"});
        check(o.hasPos && o.posX == 300 && o.posY == 450,
              "--pos consumes exactly two values");
        check(o.inputFile == "a.md",
              "and does not consume the path that follows them -- an off-by-one here would "
              "silently drop the document");
    }

    // --- --pos with ONE value is refused, not half-honoured -----------------
    //
    // PASS-THROUGH IS WRONG HERE and the wrong answer is dangerous rather than merely wrong:
    // posY would stay 0, which is a real position -- the top of the primary monitor -- so a
    // truncated --pos would MOVE a window instead of being ignored.
    {
        const tinta::LaunchOptions o = parse({"--pos", "300"});
        check(!o.hasPos,
              "--pos with one value does not set a position -- consuming it would leave "
              "posY at 0, which is the top edge of a monitor and not an absence");
        check(contains(o.missingValues, "--pos"),
              "and it is reported by name, because a silent drop is how this class hides");
    }

    // --- A value-taking flag with no value at all ---------------------------
    //
    // This tree has paid for this shape twice already: --printpages with a mistyped path wrote
    // one blank page that read as a passing arm, and --exportpdf with no path produced an
    // 858-byte blank PDF with a confident hash. WinMain's `&& i + 1 < argc` guard makes the
    // option VANISH; here it is an error.
    {
        for (const char* flag : {"--printpages", "--dumpchrome", "--exporthtml",
                                 "--exportdocx", "--exportpdf"}) {
            const tinta::LaunchOptions o = parse({flag});
            check(contains(o.missingValues, flag),
                  "a value-taking flag with no value is reported by name rather than "
                  "silently dropped");
        }
    }

    // --- And WITH a value they capture it -----------------------------------
    //
    // The counter-case to the block above: without it, a parse that reported every
    // value-taking flag as missing would pass all five assertions there.
    {
        const tinta::LaunchOptions o =
            parse({"--printpages", "p", "--dumpchrome", "c", "--exporthtml", "h",
                   "--exportdocx", "d", "--exportpdf", "f"});
        check(o.printPagesDir == "p" && o.dumpChromePath == "c" && o.exportHtmlPath == "h" &&
                  o.exportDocxPath == "d" && o.exportPdfPath == "f",
              "COUNTER-CASE: each value-taking flag captures its own value, so the "
              "missing-value assertions above cannot pass by reporting everything");
        check(o.missingValues.empty(),
              "and nothing is reported missing when every value is present");
        check(o.inputFile.empty(),
              "and a captured value is NOT also treated as the positional path");
    }

    // --- An unrecognised flag is named, not ignored --------------------------
    {
        const tinta::LaunchOptions o = parse({"--lite", "a.md"});
        check(contains(o.unknown, "--lite"),
              "a misspelled flag is recorded by name -- WinMain drops anything starting with "
              "'-' that it does not know, so '--lite' there behaves exactly like no flag");
        check(!o.lightMode, "and a near-miss does not accidentally match the real flag");
        check(o.inputFile == "a.md", "and the real path is still found past the bad flag");
    }

    // --- The argv overload skips argv[0], exactly once ----------------------
    {
        const char* argvLike[] = {"tinta.exe", "--light", "a.md"};
        const tinta::LaunchOptions o = tinta::parseLaunchOptions(3, argvLike);
        check(o.lightMode && o.inputFile == "a.md",
              "the argv overload parses from index 1");

        // CONTROL: the program name must NOT be parsed. A path-shaped argv[0] would
        // otherwise become the input file, which is the exact off-by-one the overload
        // exists to prevent -- and it would look like a working parse in every other test.
        const char* onlyProgram[] = {"tinta.exe"};
        const tinta::LaunchOptions bare = tinta::parseLaunchOptions(1, onlyProgram);
        check(bare.inputFile.empty(),
              "CONTROL: argv[0] is not the input file -- with only a program name there is "
              "no document, and a parse that read index 0 would report 'tinta.exe'");

        const char* flagAtZero[] = {"--light"};
        const tinta::LaunchOptions skipped = tinta::parseLaunchOptions(1, flagAtZero);
        check(!skipped.lightMode,
              "CONTROL: and a flag sitting in the program-name slot is not honoured either");
    }

    // --- Defensive: argc 0 and a null argv are decidable --------------------
    {
        const tinta::LaunchOptions none = tinta::parseLaunchOptions(0, nullptr);
        check(none.inputFile.empty() && !none.lightMode && none.unknown.empty(),
              "argc 0 with a null argv parses to the default options rather than reading "
              "through the null");
    }

    // --- THE CENSUS: every option the Win32 arm accepts is recognised here ---
    //
    // The denominator is stated in the output. This is ISC-14's instrument: an option the
    // Win32 chain knows and this parse does not would land in `unknown`, which is exactly the
    // "accepted by one arm, invisible to the other" class the slice is about.
    {
        struct Spelling {
            const char* flag;
            int values;
        };
        // Transcribed from src/main_d2d.cpp's WinMain chain. Both spellings of the three
        // options that have two are listed separately, because an alias that stops working is
        // a silent regression for whoever used it.
        const Spelling spellings[] = {
            {"-l", 0},         {"--light", 0},      {"-s", 0},
            {"--stats", 0},    {"/register", 0},    {"--register", 0},
            {"--cascade", 0},  {"--tabbed", 0},     {"--edit", 0},
            {"--new", 0},      {"--printpages", 1}, {"--dumpchrome", 1},
            {"--exporthtml", 1}, {"--exportdocx", 1}, {"--exportpdf", 1},
            {"--pos", 2},
        };
        const int total = static_cast<int>(sizeof(spellings) / sizeof(spellings[0]));
        int recognised = 0;
        for (const Spelling& s : spellings) {
            std::vector<std::string> args{s.flag};
            for (int v = 0; v < s.values; v++) args.push_back("1");
            const tinta::LaunchOptions o = parse(args);
            const bool known = o.unknown.empty() && o.missingValues.empty();
            if (known) {
                recognised++;
            } else {
                std::printf("FAIL: census: '%s' is not recognised by the shared parse\n",
                            s.flag);
            }
        }
        check(recognised == total,
              "CENSUS: every option spelling the Win32 arm accepts is recognised by the "
              "shared parse");
        std::printf("       census: %d/%d Win32 option spellings recognised\n", recognised,
                    total);

        // CONTROL for the census: an invented flag must NOT be recognised, or the census is
        // measuring a parse that accepts everything and its 16/16 means nothing.
        const tinta::LaunchOptions bogus = parse({"--definitely-not-an-option"});
        check(!bogus.unknown.empty(),
              "CENSUS CONTROL: an invented flag IS reported unknown -- without this, a parse "
              "that accepted every string would score a perfect census");
    }

    // --- THE SLASH CONVENTION, BOTH WAYS, FROM EITHER HOST ------------------
    //
    // THIS BLOCK EXISTS BECAUSE THE DEFECT SHIPPED. `isFlagLike` tested `arg[0] == '/'`
    // unconditionally, lifted out of WinMain where it had always been correct because WinMain
    // only ever ran on Windows. In a portable file it makes every POSIX absolute path an
    // unknown option: `qinta /tmp/doc.md` printed `unknown option '/tmp/doc.md'` and opened an
    // empty window. scripts/probe-portable-linux.sh found it -- and its own control had been
    // GREEN through it, because that control asserted the ABSENCE of a "cannot open" line and
    // never attempting the open produces that absence too.
    //
    // The assertions below are the reason SlashPolicy is a PARAMETER and not an `#ifdef` in the
    // body: each of the four runs here exercises a convention this host does not use, and an
    // `#ifdef` would make exactly half of them unwritable on any given machine. A defect whose
    // only witness is the other platform's CI is a defect that ships.
    {
        const tinta::LaunchOptions posix =
            tinta::parseLaunchOptions({"/tmp/doc.md"}, tinta::SlashPolicy::PathPrefix);
        check(posix.inputFile == "/tmp/doc.md" && posix.unknown.empty(),
              "SLASH POLICY: under PathPrefix a POSIX absolute path is the POSITIONAL "
              "document -- this is the assertion the shipped defect fails");

        const tinta::LaunchOptions windows =
            tinta::parseLaunchOptions({"/tmp/doc.md"}, tinta::SlashPolicy::FlagPrefix);
        check(contains(windows.unknown, "/tmp/doc.md") && windows.inputFile.empty(),
              "SLASH POLICY CONTROL: under FlagPrefix the SAME string is an unknown option -- "
              "without this the assertion above is satisfied by a parser that has simply "
              "stopped treating '/' as a flag prefix anywhere, which breaks Windows");

        // `/register` MUST survive the choice, and it does for a structural reason rather than
        // a lucky one: it is matched by exact comparison earlier in the chain, so it is
        // recognised before the question "does a leading slash start a flag" is ever asked.
        // Asserted under BOTH policies because that structural claim is the kind that holds
        // until somebody reorders the chain.
        for (int i = 0; i < 2; i++) {
            const tinta::SlashPolicy policy =
                i == 0 ? tinta::SlashPolicy::FlagPrefix : tinta::SlashPolicy::PathPrefix;
            const tinta::LaunchOptions reg = tinta::parseLaunchOptions({"/register"}, policy);
            check(reg.forceRegister && reg.unknown.empty() && reg.inputFile.empty(),
                  i == 0 ? "SLASH POLICY: /register sets forceRegister under FlagPrefix"
                         : "SLASH POLICY: /register sets forceRegister under PathPrefix too -- "
                           "an exact match earlier in the chain, not a slash-prefix hit");
        }

        // A '-' flag is a flag under BOTH conventions -- there is no platform on which a
        // leading dash starts a path. Here so the policy cannot be read as "which character
        // introduces a flag" when it only ever governs one of the two.
        const tinta::LaunchOptions dash =
            tinta::parseLaunchOptions({"--light", "/tmp/doc.md"},
                                      tinta::SlashPolicy::PathPrefix);
        check(dash.lightMode && dash.inputFile == "/tmp/doc.md" && dash.unknown.empty(),
              "SLASH POLICY: PathPrefix changes nothing about '-' -- a dash flag is still a "
              "flag and the slash path is still the document");

        // And the native default is the one this host needs, checked through the DEFAULTED
        // overload rather than by naming a policy, because the default is what every real
        // caller uses and it is the only part of this seam a wrong `#ifdef` would break.
#ifdef _WIN32
        check(tinta::kNativeSlashPolicy == tinta::SlashPolicy::FlagPrefix &&
                  contains(parse({"/tmp/doc.md"}).unknown, "/tmp/doc.md"),
              "SLASH POLICY: this host's default is FlagPrefix and the defaulted overload "
              "uses it");
#else
        check(tinta::kNativeSlashPolicy == tinta::SlashPolicy::PathPrefix &&
                  parse({"/tmp/doc.md"}).inputFile == "/tmp/doc.md",
              "SLASH POLICY: this host's default is PathPrefix and the defaulted overload "
              "uses it");
#endif
    }

    // --- THE WRITE SIDE: every flag this tree spawns ITSELF with ------------
    //
    // The census above walks the options the Win32 arm READS. This block walks the ones it
    // WRITES. Three sites build a command line for a freshly spawned tinta, and each spelling
    // in them is hand-typed text with nothing the compiler can check it against:
    //
    //   src/quicknote_win32.cpp:109   --cascade --new
    //   src/quicknote_win32.cpp:121   --cascade --edit "<path>"
    //   src/tabs.cpp:1636             --cascade --tabbed --pos <x> <y> "<path>"
    //
    // Rename --tabbed and the parse moves while tabs.cpp's string does not. A dragged-out tab
    // then opens with no tab strip, the old spelling lands in `unknown`, and the Win32 arm is
    // built WIN32 so it has no stderr to say so on. These assertions are what turns that from
    // silent into red.
    //
    // WHAT THIS DOES NOT PROVE, said plainly: it checks the parser against what those sites
    // MEAN, not against what they SAY. Closing that needs a string-to-argv split, and
    // include/win32_command_line.h only builds command lines. The fix is for those sites to
    // hand over a vector and quote it through that convention; until then this block and the
    // line numbers above are the tie, and they are a comment, not a compile error.
    {
        // quicknote_win32.cpp:109 -- the new-quick-note window.
        const tinta::LaunchOptions note = parse({"--cascade", "--new"});
        check(note.cascade && note.quickNote,
              "SELF-SPAWN 1/3: `--cascade --new` parses to cascade + quick note");
        check(note.unknown.empty() && note.missingValues.empty() && note.inputFile.empty(),
              "and produces no complaint and no phantom document path");

        // quicknote_win32.cpp:121 -- reopening an existing note straight into the editor.
        const tinta::LaunchOptions edit =
            parse({"--cascade", "--edit", "C:\\notes\\a note.md"});
        check(edit.cascade && edit.startInEditMode,
              "SELF-SPAWN 2/3: `--cascade --edit <path>` parses to cascade + edit mode");
        check(edit.inputFile == "C:\\notes\\a note.md",
              "and the path survives intact -- a space in it is the normal case for a note "
              "title, and --edit takes no value so the path must stay positional");

        // tabs.cpp:1636 -- a tab dragged out of the strip into its own window. This one is the
        // reason the placement seam exists: the x/y it emits are what --pos feeds into
        // choosePlacement, so this is the round trip for this whole slice.
        const tinta::LaunchOptions drag =
            parse({"--cascade", "--tabbed", "--pos", "1920", "-300", "doc.md"});
        check(drag.cascade && drag.forceTabStrip,
              "SELF-SPAWN 3/3: `--cascade --tabbed --pos X Y <path>` parses both flags");
        check(drag.hasPos && drag.posX == 1920 && drag.posY == -300,
              "and both coordinates, INCLUDING a negative one -- this desktop has two displays "
              "at negative Y, so a dropped tab there emits exactly this");
        check(drag.inputFile == "doc.md",
              "and --pos does not swallow the document that follows its two values");
        check(drag.unknown.empty() && drag.missingValues.empty(),
              "and the whole self-spawn line parses with nothing unrecognised");

        // DERIVED, not asserted. The first draft of this line printed a hardcoded
        // "3/3 sites ... all honoured" and went on printing it with four assertions failing
        // above it -- a summary that could only ever agree with itself. The census line two
        // blocks up got this right by counting; this one now counts too.
        const int sitesHonoured = (note.cascade && note.quickNote ? 1 : 0) +
                                  (edit.cascade && edit.startInEditMode ? 1 : 0) +
                                  (drag.cascade && drag.forceTabStrip && drag.hasPos ? 1 : 0);
        std::printf("       self-spawn: %d/3 sites honoured "
                    "(--cascade/--new/--edit/--tabbed/--pos, 5 spellings)\n",
                    sitesHonoured);

        // CONTROL: one spelling misspelled must be REPORTED. Without this, every assertion
        // above is satisfied by a parser that accepts every string it is handed, and the
        // rename this block exists to catch would still pass.
        const tinta::LaunchOptions renamed =
            parse({"--cascade", "--tabs", "--pos", "1920", "-300", "doc.md"});
        check(contains(renamed.unknown, "--tabs") && !renamed.forceTabStrip,
              "SELF-SPAWN CONTROL: a self-spawn line carrying a RENAMED flag is reported "
              "unknown and does not set the option -- this is the failure the block above "
              "would otherwise pass straight through");
    }

    // --- THE OUTPUT GUARD: the run that ate a tracked README ------------------
    //
    // Not a hypothetical either, and this one already happened. On 2026-09-20,
    // `tinta --lite --exportpdf README.md` from the repo root replaced a tracked 2,357-line
    // README with a 4,427,206-byte placeholder PDF, at exit 0, with `unknown option '--lite'`
    // as the only line on stderr. The first assertion below IS that command line.
    //
    // What makes it worth a block rather than a line is that the parser was never the problem:
    // `--lite` went into `unknown` correctly, src/main_d2d.cpp printed it correctly, and then
    // ran the export anyway. So every assertion here is about ACTING on information the parse
    // already had, and `refuseOutput` takes the one fact it cannot know -- whether the path is
    // on disk -- as a parameter, which is what keeps this whole block runnable on any host with
    // no filesystem and no temp directory.
    {
        check(tinta::refuseOutput(parse({"--lite", "--exportpdf", "README.md"}), false) ==
                  tinta::OutputRefusal::CommandLineNotUnderstood,
              "OUTPUT GUARD: the exact command line that destroyed README.md on 2026-09-20 is "
              "refused -- a typo anywhere on the line means the line was not understood, and an "
              "option that WRITES A FILE must not run on a guess");

        // COUNTER-CASE, and it is the one that proves the assertion above is about the typo
        // rather than about --exportpdf. Same flag, same path, no typo, path not on disk.
        check(tinta::refuseOutput(parse({"--exportpdf", "README.md"}), false) ==
                  tinta::OutputRefusal::None,
              "OUTPUT GUARD COUNTER-CASE: with the typo removed and nothing at that path, the "
              "same export is allowed -- so the refusal above is the command line, not the flag");

        // THE DISCRIMINATOR for the existence test: one call, two answers, and the ONLY thing
        // that differs is the parameter. This is what separates "is a document" from "is a
        // document that is actually there".
        check(tinta::refuseOutput(parse({"--exportpdf", "README.md"}), true) ==
                  tinta::OutputRefusal::ExistingDocument,
              "OUTPUT GUARD: a clean command line still refuses when a markdown document "
              "already exists at the output path");

        check(tinta::refuseOutput(parse({"doc.md", "--exportpdf", "doc.md"}), false) ==
                  tinta::OutputRefusal::SameAsInputDocument,
              "OUTPUT GUARD: an output path equal to the document being opened is refused "
              "without consulting the filesystem at all");
        check(tinta::refuseOutput(parse({"doc.md", "--exportpdf", "doc.pdf"}), false) ==
                  tinta::OutputRefusal::None,
              "OUTPUT GUARD: and the ordinary shape -- read doc.md, write doc.pdf -- is allowed");

        // THE FALSE-POSITIVE CONTROL, AND IT IS THE REASON THIS GUARD IS SHAPED THE WAY IT IS.
        // The first design refused any write over an existing file, which is the standard CLI
        // convention. It would have broken every byte-identity rig in this tree on its first
        // run, because they all re-export to a fixed scratch path. The first path below is read
        // verbatim out of scripts/pdfmask.py:27, existing, and it must be ALLOWED.
        struct HarnessCase {
            const char* flag;
            const char* path;
        };
        const HarnessCase harness[] = {
            {"--exportpdf", "build-win-split/pdfscratch/m1.pdf"},
            {"--exporthtml", "build-win-split/out.html"},
            {"--exportdocx", "build-win-split/out.docx"},
            {"--dumpchrome", "build-win-split/chrome.txt"},
            {"--printpages", "build-win-split/pages"},
        };
        int allowed = 0;
        for (const HarnessCase& c : harness) {
            if (tinta::refuseOutput(parse({c.flag, c.path}), true) ==
                tinta::OutputRefusal::None) {
                allowed++;
            }
        }
        check(allowed == 5,
              "OUTPUT GUARD CONTROL: all five real harness shapes are allowed even though the "
              "path already exists -- a guard that refuses the rigs gets reverted, and then the "
              "defect is back with the guard's name on it");

        // .txt is in that list on purpose: it is a plausible --dumpchrome target, so it is
        // deliberately NOT a document extension, and this asserts the decision rather than
        // leaving it to the header's prose.
        check(!tinta::isDocumentPath("chrome.txt") && tinta::isDocumentPath("README.md") &&
                  tinta::isDocumentPath("a.MARKDOWN") && tinta::isDocumentPath("b.mkd") &&
                  tinta::isDocumentPath("c.mdown"),
              "OUTPUT GUARD: the document set is markdown only, case-insensitively, and .txt is "
              "excluded because the chrome dump plausibly writes one");

        // A dot in a DIRECTORY name is not an extension. Without this, an output path like
        // build-win-split/v1.2/out would read its extension as ".2/out" and classify as
        // whatever that compares to -- the kind of thing that works until a path changes.
        check(!tinta::isDocumentPath("build/v1.2/out") &&
                  !tinta::isDocumentPath("build.md/out") &&
                  tinta::isDocumentPath("build.x/out.md"),
              "OUTPUT GUARD: the extension is looked for after the last separator, so a dotted "
              "directory name is not mistaken for an extension");

        // plannedOutput reports the write that WOULD HAPPEN, which is the first branch
        // src/main_d2d.cpp enters -- not the first flag on the line. Asserted because the two
        // orders differ and guarding a file nobody writes would be a silent no-op.
        const tinta::PlannedOutput both =
            tinta::plannedOutput(parse({"--exportpdf", "p.pdf", "--dumpchrome", "c.txt"}));
        check(both.flag == "--dumpchrome" && both.path == "c.txt",
              "OUTPUT GUARD: with two destructive flags the DISPATCH order decides, and "
              "--dumpchrome runs first in src/main_d2d.cpp regardless of command-line order");
        check(tinta::plannedOutput(parse({"--light", "notes.md"})).flag.empty(),
              "OUTPUT GUARD: an ordinary launch plans no write, so the guard cannot fire on it");

        // A value-taking flag with NO value writes nothing, so there is nothing to guard -- the
        // path is empty and src/main_d2d.cpp's branch never opens. It is still an unusable
        // command line, and the missingValues report is what says so. Asserted so the None here
        // reads as "nothing to refuse" rather than as a hole in the guard.
        check(tinta::plannedOutput(parse({"--exportpdf"})).flag.empty() &&
                  contains(parse({"--exportpdf"}).missingValues, "--exportpdf"),
              "OUTPUT GUARD: --exportpdf with no value plans no write at all, and is already "
              "reported through missingValues");
        check(tinta::refuseOutput(parse({"--exporthtml", "out.html", "--exportpdf"}), false) ==
                  tinta::OutputRefusal::CommandLineNotUnderstood,
              "OUTPUT GUARD: but a valueless flag BESIDE a real export still refuses that "
              "export -- the line was not understood, whichever half carried the mistake");

        // Every refusal has to be able to say why. An empty message on a real refusal would
        // print `refusing to write 'x' -- ` and tell the user nothing.
        int messages = 0;
        for (tinta::OutputRefusal r : {tinta::OutputRefusal::CommandLineNotUnderstood,
                                       tinta::OutputRefusal::SameAsInputDocument,
                                       tinta::OutputRefusal::ExistingDocument}) {
            const char* m = tinta::outputRefusalMessage(r);
            if (m != nullptr && m[0] != '\0') messages++;
        }
        check(messages == 3 &&
                  tinta::outputRefusalMessage(tinta::OutputRefusal::None)[0] == '\0',
              "OUTPUT GUARD: all three refusals carry a message and None carries none");
    }

    std::printf("%s%d launch-option assertions run, %d failed\n",
                failures == 0 ? "all " : "", checks, failures);
    return failures == 0 ? 0 : 1;
}
