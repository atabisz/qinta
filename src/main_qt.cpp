// tinta's Qt entry point: the file that replaces WinMain when the port is done.
//
// It holds main() and nothing else. Everything it does is one call into
// src/qt_init.cpp or one construction of the window in src/qt_window.cpp, which is
// deliberate and is the opposite of how the Direct2D side grew: src/main_d2d.cpp is
// nearly 2,000 lines because WinMain, the window procedure, the paint path and the command
// line all ended up in one file, and that is a large part of why nothing in the tree
// can test a tinta window. A main() with no logic in it has nothing to test, which is
// the point -- the logic is next door where a test can reach it.
//
// WHAT IT DOES NOT DO YET, and why each one is absent rather than forgotten:
//
//   * THE WHOLE settings load, since 2026-10-11. This arm used to read eleven fields -- the
//     geometry, the theme and the keymap -- on the argument that the other ~20 reached members
//     whose consumers were Windows-only. That stopped being true when WinMain was retired: zoom,
//     reading width, word wrap, the pins and the UI language all have consumers here, and
//     skipping them reset each to App's default on every launch. applySavedPreferences
//     (settings.h) now copies every field WinMain copied, storePreferences writes them back at
//     close, and the last session's tabs reopen (qt_launch.h). followSystemTheme is acted on: it
//     starts the theme the system reading picks, and the window follows changes to that reading.
//   * loadCustomThemes() now runs, as it does in WinMain, because the saved theme index can
//     name a custom theme. The bullet that stood here said it "would change nothing" while
//     the only index this file set was the built-in kLightModeThemeIndex.
//   * THE FLAGS ARE PARSED, THREE ARE HONOURED, AND THE REST ARE REFUSED BY NAME. What
//     stood here was "A FILE ARGUMENT AND NOTHING ELSE. argv[1], if it is there, is opened
//     as a document", and it described a DEFECT rather than a limitation: reading argv[1]
//     as a path means `tinta --light notes.md` tries to open a document called "--light",
//     says it cannot, and never opens notes.md. A flag was promoted to a filename and the
//     filename was dropped. tests/launch_options_tests.cpp opens with that case.
//
//     Now: tinta::parseLaunchOptions (one chain, shared with WinMain) does the reading.
//     --light, --pos and --cascade are honoured here for real, and since 2026-10-06 so are
//     --printpages, --exporthtml, --exportdocx and --exportpdf, which write and exit without a
//     window, and --tabbed, --new, --edit and --stats followed the same day. /register is
//     honoured on Windows and refused elsewhere, on stderr, by name, and the window still
//     opens -- because a refusal a user can read is the opposite of the old behaviour, where
//     an unhonourable flag was indistinguishable from a working one. --dumpchrome was refused
//     the same way until it was removed on 2026-10-11, its body having gone with Direct2D; it
//     is now an unknown option like any other.
//
//     WHY ONLY THREE, since the parse hands over thirteen: the blocker is not this file. It
//     is that --tabbed, --new, --edit and --stats need src/input.cpp -- 4,394 lines, 98
//     off-Windows compile errors -- and the five export/debug options need their own Win32
//     paths. Accepting one of those here would produce a binary that takes --tabbed and
//     shows no tab strip, which is worse than refusing it.
//
//     TWO CLAIMS THAT USED TO END THIS BULLET ARE BOTH DISCHARGED, and the second one
//     was WRONG rather than merely stale. "It needs the document renderer first" went
//     when tests/qt_entry_tests.cpp began laying out and painting a parsed document on
//     both platforms. What replaced it -- "what is missing is four lines here that read
//     argv[1] and put the parse result in app.root" -- was an estimate I never checked,
//     and checking it is what killed it: the load logic on the Windows side is two
//     lambdas INSIDE WinMain (src/main_d2d.cpp's `loadDocumentContent` and
//     `loadFile`), so there was no
//     function to call, and the second of them opens its stream with the MSVC wide-path
//     ifstream extension that does not compile off Windows at all. The real cost was a
//     new portable function, loadDocumentFromPath in src/file_utils.cpp, and the four
//     lines below are what is left of the estimate once that existed.
//
//     A FIGURE INSIDE A COMMENT IS STILL A CLAIM. This one read as a considered
//     estimate for exactly one commit and was never measured until somebody tried to
//     spend it.
//   * No exec() loop features: no menus, no tabs, no input. This used to read
//     "src/input.cpp is the deepest Windows file in the tree (344 native mentions at
//     the last census)", and that is FALSE as of 2026-09-21: input.cpp compiles off
//     Windows at 0 errors and is in tests/portable-sources.txt. A native-mention census
//     is what made it look deepest -- the compiler found 15 lines, and 16 of the 17
//     sites the source census saw but g++ did not were COMMENTS. What is missing here is
//     not a portable input.cpp, it is a Qt key and mouse router calling into it.
//
// So `qinta some.md` opens a window with that document drawn in it, and `qinta`
// with no argument opens the same window showing the placeholder line. The sentence
// that stood here said "this opens a window with the theme background and a line of
// text in it. That is a shell" -- true until this commit, and the reason it is
// corrected rather than deleted is that it named the limit README.md's basis F
// measures, so a reader who remembers it needs to be told here that it moved.
//
// WHAT IT IS STILL NOT is a viewer: no menus, no tabs, no input, no scrolling, no
// resize relayout outside paintEvent. One document, drawn once, and you can close the
// window.
//
// "AND NO MATHS" USED TO END THAT LIST, with src/math_stub.cpp named as the reason.
// It is off the list: src/math_render.cpp is ported, the stub is deleted, and this
// binary typesets equations. Recorded rather than quietly dropped, because the missing
// item was one of the limits README.md's basis F measures.
#include <QApplication>
#include <QFileInfo>

#include <cstdio>
#include <string>

#include "app.h"
#include "export.h"          // exportHtmlFile, exportDocxFile, exportPdfFile
#include "file_utils.h"
#include "instance_channel_qt.h"
#include "launch_options.h"
#include "platform_paths.h"  // regularFileExists, for the output guard
#include "print.h"           // printDebugPages
#include "text_utils.h"      // toWide
#include "qt_init.h"
#include "qt_launch.h"
#include "qt_window.h"
#include "settings.h"
#include "settings_data.h"
#include "tabs.h"  // tabOpenPath, for documents handed over by a second launch
#include "theme_data.h"
#include "window_placement.h"

namespace {

// A refusal names the flag AND says what is missing, because "not supported" tells a user
// nothing about whether to wait for it or stop asking. The window still opens afterwards:
// refusing a flag is not a reason to refuse the document.
void refuse(const char* flag, const char* missing) {
    std::fprintf(stderr, "qinta: %s is not honoured by this build -- %s\n", flag, missing);
}

}  // namespace

int main(int argc, char** argv) {
    // QApplication rather than QGuiApplication: QtAppWindow is a QWidget, and the
    // file-dialog seam (src/file_dialog_qt.cpp) is QWidgets too.
    QApplication qapp(argc, argv);
    QApplication::setApplicationName(QStringLiteral("tinta"));

    // App is 1,848 lines of header and holds a qmd::MarkdownParser by value, which is
    // why merely constructing one pulls src/markdown.cpp and md4c onto the link line.
    // Measured from a link map, not read off the declaration -- and this tree has now
    // learned that same fact from a linker three separate times.
    App app;

    // Before the settings load, as in WinMain, because a saved theme index may name a custom
    // theme -- and before installQtBackends, which sizes the theme picker's preview formats
    // to themeCount() at install.
    loadCustomThemes();

    // Before the window, because the first paint can arrive during show().
    installQtBackends(app);

    // BEFORE the window too, and that ordering is load-bearing: QtAppWindow chooses its
    // branch from App::root at paint time, and the first paint can arrive inside show().
    // Loading afterwards would work by luck -- the placeholder for one frame, then a
    // repaint nobody requested.
    //
    // A FAILED LOAD IS NOT FATAL. App::root stays null, the window shows the placeholder,
    // and the reason goes to stderr. That matches the Windows side, where a file that
    // will not open lands on the start page rather than refusing to launch.
    // AFTER QApplication, deliberately. Qt removes the arguments IT recognises -- -platform,
    // -style, -stylesheet and friends -- from argc/argv during construction, so parsing here
    // means `-platform offscreen` is Qt's business and never reaches `unknown`. Parsing before
    // construction would report every Qt switch as an unknown tinta option.
    const tinta::LaunchOptions options = tinta::parseLaunchOptions(argc, argv);

    // Said out loud before anything is honoured, so the order of the output matches the order
    // of the command line's problems rather than the order this file happens to check them.
    for (const std::string& flag : options.unknown) {
        std::fprintf(stderr, "qinta: unknown option '%s'\n", flag.c_str());
    }
    for (const std::string& flag : options.missingValues) {
        std::fprintf(stderr, "qinta: option '%s' needs a value\n", flag.c_str());
    }

    // THE OUTPUT GUARD, WinMain's, called the same way, because this binary can write files now.
    // The four harness flags below used to be refused here, so there was nothing to guard; since
    // 2026-10-06 they write, and `qinta --exportpdf README.md` would otherwise do on this stack
    // what `tinta --lite --exportpdf README.md` did on 2026-09-20: put a PDF where the document
    // was, at exit 0. Before any setup, so a refusal is a print and a return.
    {
        const tinta::PlannedOutput planned = tinta::plannedOutput(options);
        if (!planned.flag.empty()) {
            const bool exists = platform::regularFileExists(toWide(planned.path));
            const tinta::OutputRefusal refusal = tinta::refuseOutput(options, exists);
            if (refusal != tinta::OutputRefusal::None) {
                std::fprintf(stderr, "qinta: refusing to write '%s' -- %s\n",
                             planned.path.c_str(), tinta::outputRefusalMessage(refusal));
                std::fprintf(stderr,
                             "qinta: %s names the file to WRITE, not the file to read\n",
                             planned.flag.c_str());
                return 2;
            }
        }
    }

    // THE REFUSALS -- TEN UNTIL 2026-10-06, then SIX once the four harness switches below started
    // working, and TWO once the four launch flags did. Each one is a flag the shared parse understands and this binary
    // cannot act on, and each says which missing piece is the reason -- so the list doubles
    // as the port's remaining work, in the place a user actually meets it.
    //
    // The first four all wait on the same file, and that is the substantive finding of this
    // slice rather than an implementation detail: every interactive feature is behind
    // src/input.cpp.
    //
    // THE FIGURE THAT STOOD HERE WAS STALE IN BOTH HALVES, 88 LINES BELOW THIS FILE'S OWN
    // WARNING THAT "A FIGURE INSIDE A COMMENT IS STILL A CLAIM". It read "4,394 lines with
    // 98 off-Windows compile errors". The file is 4,586 lines and has **0** off-Windows
    // errors as of 2026-09-21 -- it compiles, and it is in tests/portable-sources.txt. The
    // reason these four flags still refuse is no longer that the file will not build off
    // Windows; it is that main_qt.cpp routes no keyboard or mouse events, so nothing calls
    // into it. -- SUPERSEDED 2026-09-25 FOR THE KEYBOARD: src/qt_window.cpp::keyPressEvent is
    // now the key router, handing every key to src/input.cpp's handleKeyDown and
    // handleCharInput, and the twelve hand-written Qt::Key_* cases it replaced are gone. THE
    // FOUR REFUSALS BELOW WERE NOT RE-EXAMINED BY THAT SLICE, and their messages still name
    // src/input.cpp, which now links into this binary -- so "(src/input.cpp)" is no longer a
    // measured blocker for any of them. What each one waits on now is the flag slice's first
    // question; until it is asked, refusing stays safer than accepting a flag unverified.
    // Deliberately no replacement count: a line count here would rot the same way, and the
    // census that owns these numbers is scripts/check-readme-figures.sh.
    // --tabbed, --new, --edit AND --stats WERE REFUSED HERE, each citing src/input.cpp, until
    // 2026-10-06. The note above already said that citation had stopped being a measured blocker;
    // the flag slice asked the question it set, and all four are honoured below through
    // src/qt_launch.cpp, the same way WinMain honours them.
#ifndef _WIN32
    // /register IS HONOURED ON WINDOWS since 2026-10-07 (below, after the settings load); it
    // stays refused elsewhere, where there is no registry and xdg-mime / LaunchServices are
    // different models rather than another implementation of this one.
    if (options.forceRegister) {
        refuse("/register", "file associations are a Windows-registry feature");
    }
#endif
    // --printpages, --exporthtml, --exportdocx and --exportpdf WERE REFUSED HERE until
    // 2026-10-06, the last three behind a long note on why the PDF one looked cheap and was
    // not: the exporters needed the print code, which was Windows-bound then. It is portable
    // now, and src/qt_init.cpp keeps a render target outside paint, so all four run below
    // as headless harness switches, as they do in WinMain.

    const Settings saved = loadSettings();

#ifdef _WIN32
    // `/register`: WinMain's block, moved into src/settings_win32.cpp so this is one call. It
    // registers, says so in a message box, opens Default apps and exits -- before the
    // single-instance hand-off below, so a running window cannot swallow it.
    if (options.forceRegister) return registerFileAssociationAndReport(saved) ? 0 : 1;
#endif

    // THE SINGLE-INSTANCE HAND-OFF (2026-10-06), WinMain's FindWindowW + WM_COPYDATA. A plain
    // file launch hands its document to the running window and exits, under the same rule
    // (tinta::shouldJoinExistingWindow) and with the same fallback: nobody listening, or no
    // acknowledgement, means this launch opens its own window. The path goes absolute first,
    // because the running window has its own working directory.
    if (tinta::shouldJoinExistingWindow(options, saved.openInTabs)) {
        const std::string absolute =
            QFileInfo(QString::fromStdString(options.inputFile)).absoluteFilePath().toStdString();
        if (tinta::sendToRunningInstance(tinta::instanceChannelName(), absolute)) {
            releaseQtBackends(app);
            return 0;
        }
    }

    // THE SAVED THEME, LOADED BECAUSE THIS ARM WRITES IT -- the theme picker and the close save
    // both read-modify-write the four theme fields, so skipping the load wrote defaults over
    // them. With follow-system on the start theme is the system's pick instead. See qt_launch.h.
    tinta::applySavedTheme(app, saved, tinta::systemPrefersLight());
    // Every other saved preference, in WinMain's order: after the theme, before the document.
    // The keymap and the UI language are in it (settings.h).
    applySavedPreferences(app, saved);
    app.sessionOwner = tinta::ownsSession(options);

    // --light: the one appearance flag that needs nothing Windows-only. The three fields it
    // moves live in src/qt_launch.cpp rather than here, so tests/qt_entry_tests.cpp can grab a
    // window before and after the SAME function this line calls -- a test with its own copy of
    // the three assignments could not fail on a defect planted in this one. After the saved
    // theme, so the flag wins, and the close save then persists it, as WinMain's does.
    if (options.lightMode) tinta::applyLightMode(app);

    tinta::applyLaunchFlags(app, options);

    QString title = QStringLiteral("Qinta");
    // --new takes no file: WinMain's quick note is an empty buffer with no backing path, even
    // when a path was also given. With no file at all, the last session's active tab opens
    // (tinta::sessionToRestore has WinMain's conditions), and the rest of it joins the tab strip
    // below. A session tab that will not load leaves the start page, as in WinMain.
    int sessionActive = 0;
    const std::vector<std::string> session = tinta::sessionToRestore(options, saved, sessionActive);
    bool namedOnCommandLine = false;
    if (!options.inputFile.empty() && !options.quickNote) {
        if (loadDocumentFromPath(app, options.inputFile)) {
            namedOnCommandLine = true;
            title = QStringLiteral("%1 - Qinta")
                        .arg(QString::fromLocal8Bit(options.inputFile.c_str()));
        } else {
            std::fprintf(stderr, "qinta: cannot open '%s'\n", options.inputFile.c_str());
        }
    } else if (!session.empty() && loadDocumentFromPath(app, session[sessionActive])) {
        title = QStringLiteral("%1 - Qinta")
                    .arg(QString::fromLocal8Bit(session[sessionActive].c_str()));
    }
    tinta::restoreLaunchDocument(app, saved, options, namedOnCommandLine);
    if (!session.empty() && !app.currentFile.empty()) tabsSeedSession(app, session);

    // THE HARNESS SWITCHES: write and exit, in WinMain's dispatch order, which is also the
    // order tinta::plannedOutput reports and the guard above checked. No window is created: the
    // exporters lay the document out at their own page or export width and draw through the
    // factory render target (include/qt_init.h), so nothing here needs a widget. Exit 0 on a
    // written file, 1 when the exporter refused -- no document, an unwritable path.
    {
        int harnessRc = -1;
        if (!options.printPagesDir.empty()) {
            const int pages = printDebugPages(app, toWide(options.printPagesDir));
            std::fprintf(stderr, "qinta: printpages: %d\n", pages);
            harnessRc = pages > 0 ? 0 : 1;
        } else if (!options.exportHtmlPath.empty()) {
            harnessRc = exportHtmlFile(app, toWide(options.exportHtmlPath)) ? 0 : 1;
        } else if (!options.exportDocxPath.empty()) {
            harnessRc = exportDocxFile(app, toWide(options.exportDocxPath)) ? 0 : 1;
        } else if (!options.exportPdfPath.empty()) {
            harnessRc = exportPdfFile(app, toWide(options.exportPdfPath)) ? 0 : 1;
        }
        if (harnessRc >= 0) {
            releaseQtBackends(app);
            return harnessRc;
        }
    }

    // WHERE THE WINDOW GOES, decided by the same function WinMain calls.
    //
    // Four geometry fields out of Settings, plus windowMaximized; every other field was applied
    // above by applySavedTheme and applySavedPreferences. The saved size is the request's size, so a
    // window restores at the dimensions it was closed at, and the 1000x700 that used to be
    // hardcoded here is now only the fallback Settings itself supplies (1024x768).
    // Both halves come from src/qt_launch.cpp rather than living here, because nine field
    // copies inside main() are nine copies no test can reach -- and a swapped posX/posY puts
    // the window somewhere plausible on the wrong axis in a build that compiles and runs.
    // tests/qt_entry_tests.cpp asserts on the request AND on the geometry the window ends up
    // with, which are two separate claims.

    // THE KEYMAP was bound here by its own two lines and is now bound by applySavedPreferences
    // above, before the document loads. It is not optional: App::keymap is zero-initialised, and
    // against an unbound map every default action key -- J, K, T, Q, F11 -- translates to 0 and
    // falls through handleKeyDown's switch, in a build that compiles and passes any test that
    // forgot the same call.

    const tinta::WindowRect placement =
        choosePlacement(tinta::placementRequestFrom(options, saved), tinta::monitorsFromQt());

#ifdef _WIN32
    // The first-run question WinMain asked: offer to register once, remember the answer. HERE
    // and not with /register above, because every return before this line is headless (the
    // four harness switches, the hand-off) and a modal box there would hang a scripted run --
    // tests/run_qinta_cli.cmake runs this binary on CI with a fresh configuration. It reads and
    // saves its own copy of the settings; every other writer in the tree loads fresh before it
    // saves, so the answer it records survives them.
    {
        Settings asked = loadSettings();
        askAndRegisterFileAssociation(asked);
    }
#endif

    QtAppWindow window(app);
    // The size, and the position only if there is one -- the sentinel branch is inside
    // applyPlacement, where a test drives it. See qt_launch.h on why the sentinel is not
    // resolved by the placement seam.
    tinta::applyPlacement(window, placement);
    window.setWindowTitle(title);
    tinta::showPlaced(window, saved.windowMaximized);
    // After the window, as WinMain enters these after its first frame.
    tinta::enterLaunchMode(app, options);

    // And this window becomes the one later launches join. A document arriving opens as a tab and
    // the window comes forward, as WinMain's WM_COPYDATA case restores and foregrounds. If the
    // name is already held by another live window, this one simply does not listen.
    tinta::InstanceServer instanceServer([&app, &window](const std::string& path) {
        if (window.isMinimized()) window.showNormal();
        window.raise();
        window.activateWindow();
        tabOpenPath(app, nullptr, path, true);
    });
    instanceServer.listen(tinta::instanceChannelName());

    const int rc = QApplication::exec();

    // Explicit, because App's own teardown is inside #ifdef _WIN32. Off Windows
    // nothing else would release the factory, and the wrapper holds Qt objects that
    // want to die before QApplication does.
    releaseQtBackends(app);
    return rc;
}
