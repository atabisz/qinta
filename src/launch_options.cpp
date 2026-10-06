#include "launch_options.h"

#include <cstdlib>

namespace tinta {
namespace {

// The option order below mirrors src/main_d2d.cpp's chain deliberately, so the two can be read
// side by side while both exist. The chain there is the authority for what tinta accepts; this
// is that chain with three changes, each of them recorded in the header: missing values become
// errors, unknown flags become errors, and the positional test no longer swallows the gap.
// A leading '-' is a flag everywhere. A leading '/' depends on the platform's convention, and
// the caller supplies it -- see SlashPolicy in the header for the defect that made this a
// parameter rather than the two-character condition it used to be.
bool isFlagLike(const std::string& arg, SlashPolicy slashPolicy) {
    if (arg.empty()) return false;
    if (arg[0] == '-') return true;
    return arg[0] == '/' && slashPolicy == SlashPolicy::FlagPrefix;
}

// _wtoi's behaviour on a non-numeric string is 0, and WinMain relies on that silently. Kept
// rather than tightened: --pos arrives from tabDragEnd with numbers this tree printed itself,
// so a strict parse would add a failure mode the shipping path cannot produce. The note is
// here so the leniency reads as a decision rather than an oversight.
int toInt(const std::string& s) {
    return std::atoi(s.c_str());
}

}  // namespace

LaunchOptions parseLaunchOptions(const std::vector<std::string>& args,
                                 SlashPolicy slashPolicy) {
    LaunchOptions out;
    const size_t n = args.size();

    for (size_t i = 0; i < n; i++) {
        const std::string& arg = args[i];

        // Each value-taking option checks its own argument and records the SHORTFALL rather
        // than falling through. `needs(1)` and `needs(2)` are spelled out per branch because a
        // shared helper would have to reach back into the loop index and the clarity is worth
        // more than the four saved lines.
        if (arg == "-l" || arg == "--light") {
            out.lightMode = true;
        } else if (arg == "-s" || arg == "--stats") {
            out.showStats = true;
        } else if (arg == "/register" || arg == "--register") {
            out.forceRegister = true;
        } else if (arg == "--cascade") {
            out.cascade = true;
        } else if (arg == "--tabbed") {
            out.forceTabStrip = true;
        } else if (arg == "--edit") {
            out.startInEditMode = true;
        } else if (arg == "--new") {
            out.quickNote = true;
        } else if (arg == "--printpages") {
            if (i + 1 < n) {
                out.printPagesDir = args[++i];
            } else {
                out.missingValues.push_back(arg);
            }
        } else if (arg == "--dumpchrome") {
            if (i + 1 < n) {
                out.dumpChromePath = args[++i];
            } else {
                out.missingValues.push_back(arg);
            }
        } else if (arg == "--exporthtml") {
            if (i + 1 < n) {
                out.exportHtmlPath = args[++i];
            } else {
                out.missingValues.push_back(arg);
            }
        } else if (arg == "--exportdocx") {
            if (i + 1 < n) {
                out.exportDocxPath = args[++i];
            } else {
                out.missingValues.push_back(arg);
            }
        } else if (arg == "--exportpdf") {
            if (i + 1 < n) {
                out.exportPdfPath = args[++i];
            } else {
                out.missingValues.push_back(arg);
            }
        } else if (arg == "--pos") {
            // TWO values, and a --pos with one is not half-honoured. Consuming the single
            // available value would leave posY at 0, which is a POSITION -- the top edge of
            // the primary monitor -- so a partially-supplied --pos would move a window rather
            // than being refused.
            if (i + 2 < n) {
                out.posX = toInt(args[i + 1]);
                out.posY = toInt(args[i + 2]);
                out.hasPos = true;
                i += 2;
            } else {
                out.missingValues.push_back(arg);
                i = n;  // the remainder is --pos's truncated argument list, not more options
            }
        } else if (isFlagLike(arg, slashPolicy)) {
            out.unknown.push_back(arg);
        } else {
            // THE LAST POSITIONAL WINS, matching WinMain, which assigns rather than appends.
            // tinta opens one document per process; a second path is the caller's mistake and
            // the Windows arm has always resolved it this way.
            out.inputFile = arg;
        }
    }

    return out;
}

LaunchOptions parseLaunchOptions(int argc, const char* const* argv,
                                 SlashPolicy slashPolicy) {
    std::vector<std::string> args;
    if (argc > 1 && argv != nullptr) {
        args.reserve(static_cast<size_t>(argc - 1));
        for (int i = 1; i < argc; i++) {
            if (argv[i] != nullptr) args.push_back(argv[i]);
        }
    }
    return parseLaunchOptions(args, slashPolicy);
}

bool shouldJoinExistingWindow(const LaunchOptions& options, bool openInTabs) {
    return !options.inputFile.empty() && !options.quickNote && !options.cascade &&
           options.printPagesDir.empty() && options.dumpChromePath.empty() &&
           options.exportHtmlPath.empty() && options.exportDocxPath.empty() &&
           options.exportPdfPath.empty() && openInTabs;
}

std::vector<std::string> quickNoteRelaunchArgs() { return {"--cascade", "--new"}; }

std::vector<std::string> documentRelaunchArgs(const std::string& utf8Path) {
    return {"--cascade", "--edit", utf8Path};
}

PlannedOutput plannedOutput(const LaunchOptions& options) {
    // Dispatch order, taken from src/main_d2d.cpp: --dumpchrome, then --printpages, then the three
    // exporters. That file returns from the first branch it enters, so the first match here is the
    // only write that can happen, and reporting a later one would guard a file nobody touches.
    if (!options.dumpChromePath.empty()) return {"--dumpchrome", options.dumpChromePath};
    if (!options.printPagesDir.empty()) return {"--printpages", options.printPagesDir};
    if (!options.exportHtmlPath.empty()) return {"--exporthtml", options.exportHtmlPath};
    if (!options.exportDocxPath.empty()) return {"--exportdocx", options.exportDocxPath};
    if (!options.exportPdfPath.empty()) return {"--exportpdf", options.exportPdfPath};
    return {};
}

bool isDocumentPath(const std::string& path) {
    // The extension set is this app's INPUT formats. It is a new authority in this tree -- nothing
    // else enumerates them, because the open path takes whatever the file dialog hands it -- so it
    // is spelled out here rather than derived from something that does not exist.
    static const char* kDocumentExtensions[] = {".md", ".markdown", ".mkd", ".mdown"};

    // Find the extension without <filesystem>: the last '.' AFTER the last separator, so a dot in
    // a directory name ("build-win-split/v1.2/out") is not mistaken for an extension.
    size_t lastSeparator = path.find_last_of("/\\");
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    if (lastSeparator != std::string::npos && dot < lastSeparator) return false;

    std::string extension = path.substr(dot);
    for (char& c : extension) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    for (const char* candidate : kDocumentExtensions) {
        if (extension == candidate) return true;
    }
    return false;
}

OutputRefusal refuseOutput(const LaunchOptions& options, bool outputExists) {
    const PlannedOutput planned = plannedOutput(options);
    if (planned.flag.empty()) return OutputRefusal::None;

    // ORDER MATTERS, and it is worst-understood-first. A command line carrying a typo tells us the
    // least about what was meant, so it is refused before the more specific tests get a chance to
    // report a reason that implies the rest of the line was read correctly.
    if (!options.unknown.empty() || !options.missingValues.empty()) {
        return OutputRefusal::CommandLineNotUnderstood;
    }

    // Byte comparison, not a canonicalised one. Resolving both sides would need the filesystem and
    // would move this decision onto one host; the exact-match case is the one that actually occurs
    // (a shell completing the same name twice), and a near-miss falls through to the test below,
    // which catches it by extension anyway.
    if (!options.inputFile.empty() && planned.path == options.inputFile) {
        return OutputRefusal::SameAsInputDocument;
    }

    if (outputExists && isDocumentPath(planned.path)) {
        return OutputRefusal::ExistingDocument;
    }

    return OutputRefusal::None;
}

const char* outputRefusalMessage(OutputRefusal refusal) {
    switch (refusal) {
        case OutputRefusal::CommandLineNotUnderstood:
            return "the command line was not fully understood, and this option writes a file";
        case OutputRefusal::SameAsInputDocument:
            return "that is the document being opened";
        case OutputRefusal::ExistingDocument:
            return "a document already exists there, and this option names its OUTPUT";
        case OutputRefusal::None:
            break;
    }
    return "";
}

}  // namespace tinta
