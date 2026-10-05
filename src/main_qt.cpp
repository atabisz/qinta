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
//   * A NARROW settings load, and the narrowness is the point. What stood here said "No
//     settings load", because the ~30 field assignments following loadSettings() in WinMain
//     reach members whose consumers are still Windows-only -- which is still true of the
//     rest. ELEVEN are read now, each because something on this arm reads or WRITES it:
//     windowX/Y/Width/Height and windowMaximized feed the placement and the show;
//     keyProfile/keyOverrides feed the keymap handleKeyDown translates through; and
//     themeIndex, followSystemTheme, lightThemeIndex and darkThemeIndex, because the theme
//     picker and the close save read-modify-write them -- a field this arm writes and never
//     loaded went back to settings.ini as App's default. followSystemTheme is also acted on
//     now: it starts the theme the system reading picks, and the window follows changes to
//     that reading (slice 12). The close save (qt_launch.h) writes
//     only the theme and geometry fields. Loading all thirty would set state nothing reads,
//     which is the thing the old bullet was right about.
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
//     --light, --pos and --cascade are honoured here for real. The other ten are refused on
//     stderr, by name, and the window still opens -- because a refusal a user can read is
//     the opposite of the old behaviour, where an unhonourable flag was indistinguishable
//     from a working one.
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

#include <cstdio>
#include <string>

#include "app.h"
#include "file_utils.h"
#include "launch_options.h"
#include "qt_init.h"
#include "qt_launch.h"
#include "qt_window.h"
#include "settings.h"
#include "settings_data.h"
#include "theme_data.h"
#include "window_placement.h"

namespace {

// A refusal names the flag AND says what is missing, because "not supported" tells a user
// nothing about whether to wait for it or stop asking. The window still opens afterwards:
// refusing a flag is not a reason to refuse the document.
void refuse(const char* flag, const char* missing) {
    std::fprintf(stderr, "tinta: %s is not honoured by this build -- %s\n", flag, missing);
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
        std::fprintf(stderr, "tinta: unknown option '%s'\n", flag.c_str());
    }
    for (const std::string& flag : options.missingValues) {
        std::fprintf(stderr, "tinta: option '%s' needs a value\n", flag.c_str());
    }

    // THE TEN REFUSALS. Each one is a flag the shared parse understands and this binary
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
    if (options.forceTabStrip) refuse("--tabbed", "there is no tab strip yet (src/input.cpp)");
    if (options.quickNote) refuse("--new", "quick notes need the window manager (src/input.cpp)");
    if (options.startInEditMode) refuse("--edit", "the editor needs input (src/input.cpp)");
    if (options.showStats) refuse("--stats", "the stats overlay needs input (src/input.cpp)");
    if (options.forceRegister) {
        refuse("/register", "file associations are a Windows-registry feature");
    }
    if (!options.printPagesDir.empty()) {
        // The reason changed on 2026-10-06 and the refusal did not. It said "page capture is
        // still on the Direct2D path". printDebugPages is portable now and qinta links it, so
        // what is missing is this entry point calling it, which is the same gap as the three
        // exporters below.
        refuse("--printpages", "page capture is not wired to this entry point yet");
    }
    if (!options.dumpChromePath.empty()) {
        refuse("--dumpchrome", "the chrome dump is still on the Direct2D path");
    }
    if (!options.exportHtmlPath.empty()) {
        refuse("--exporthtml", "the HTML exporter is not wired to this entry point yet");
    }
    if (!options.exportDocxPath.empty()) {
        refuse("--exportdocx", "the DOCX exporter is not wired to this entry point yet");
    }
    if (!options.exportPdfPath.empty()) {
        // NAMED SEPARATELY, AND NO LONGER AS "THE ONE TO DELETE FIRST" -- that sentence
        // stood here until 2026-09-22 and it sent a reader at the most expensive of the ten.
        // It read: "the closest of the ten to honourable: the Qt PDF seam already exists and
        // has its own passing test target. What is missing is this wiring, not the
        // capability -- so this refusal is the one to delete first."
        //
        // Every clause of that was true and the conclusion was wrong. include/pdf_export.h
        // says in its own header that pagination, print layout and the page painter all stay
        // in the print code: the Qt seam is a SINK that renderPages drives, not an exporter.
        // So exportPdfFile needed that file, which stopped at a fatal d2d1.h off Windows at
        // the time (through include/xps_page_emitter.h:24).
        //
        // MEASURED 2026-09-22: all three exporters are behind the same two files, and
        // --exporthtml/--exportdocx are not the cheap alternatives they look like either --
        // export_html.cpp calls exportPdfFile, and export_docx.cpp calls buildDiagramPrims
        // in export_html.cpp. The blocker map is under QINTA_APP_SOURCES in CMakeLists.txt.
        // There is no cheapest refusal in this list; there are two files.
        //
        // BOTH FILES ARE LINKED NOW. print.cpp left Windows on 2026-10-06 and qinta exports
        // PDFs interactively through it, which tests/qt_entry_tests.cpp asserts. So this
        // refusal now has the same reason as --exporthtml and --exportdocx above: the
        // command-line path to the exporter is not written. It is no longer the expensive one.
        refuse("--exportpdf", "the PDF exporter is not wired to this entry point yet");
    }

    const Settings saved = loadSettings();

    // THE SAVED THEME, LOADED BECAUSE THIS ARM WRITES IT -- the theme picker and the close save
    // both read-modify-write the four theme fields, so skipping the load wrote defaults over
    // them. With follow-system on the start theme is the system's pick instead. See qt_launch.h.
    tinta::applySavedTheme(app, saved, tinta::systemPrefersLight());

    // --light: the one appearance flag that needs nothing Windows-only. The three fields it
    // moves live in src/qt_launch.cpp rather than here, so tests/qt_entry_tests.cpp can grab a
    // window before and after the SAME function this line calls -- a test with its own copy of
    // the three assignments could not fail on a defect planted in this one. After the saved
    // theme, so the flag wins, and the close save then persists it, as WinMain's does.
    if (options.lightMode) tinta::applyLightMode(app);

    QString title = QStringLiteral("tinta");
    if (!options.inputFile.empty()) {
        if (loadDocumentFromPath(app, options.inputFile)) {
            title = QStringLiteral("%1 - tinta")
                        .arg(QString::fromLocal8Bit(options.inputFile.c_str()));
        } else {
            std::fprintf(stderr, "tinta: cannot open '%s'\n", options.inputFile.c_str());
        }
    }

    // WHERE THE WINDOW GOES, decided by the same function WinMain calls.
    //
    // Four geometry fields out of Settings, plus windowMaximized, the theme fields and the
    // keymap fields -- see the header comment on why the rest are deliberately not applied. The saved size is the request's size, so a
    // window restores at the dimensions it was closed at, and the 1000x700 that used to be
    // hardcoded here is now only the fallback Settings itself supplies (1024x768).
    // Both halves come from src/qt_launch.cpp rather than living here, because nine field
    // copies inside main() are nine copies no test can reach -- and a swapped posX/posY puts
    // the window somewhere plausible on the wrong axis in a build that compiles and runs.
    // tests/qt_entry_tests.cpp asserts on the request AND on the geometry the window ends up
    // with, which are two separate claims.

    // THE KEYMAP, THE FIFTH FIELD WITH A CONSUMER: the key router hands every keystroke to
    // handleKeyDown, which passes it through translateActionKey before its switch. App::keymap
    // is zero-initialised, and against an unbound map every default action key -- J, K, T, Q,
    // F11 -- translates to 0 and falls through the switch. Without these two lines the router
    // would compile, pass every sendEvent test that forgot the same call, and F11 would do
    // nothing in the running application. The same two lines as WinMain, in the same order.
    app.keyOverrides = saved.keyOverrides;
    applyKeymap(app, saved);

    const tinta::WindowRect placement =
        choosePlacement(tinta::placementRequestFrom(options, saved), tinta::monitorsFromQt());

    QtAppWindow window(app);
    // The size, and the position only if there is one -- the sentinel branch is inside
    // applyPlacement, where a test drives it. See qt_launch.h on why the sentinel is not
    // resolved by the placement seam.
    tinta::applyPlacement(window, placement);
    window.setWindowTitle(title);
    tinta::showPlaced(window, saved.windowMaximized);

    const int rc = QApplication::exec();

    // Explicit, because App's own teardown is inside #ifdef _WIN32. Off Windows
    // nothing else would release the factory, and the wrapper holds Qt objects that
    // want to die before QApplication does.
    releaseQtBackends(app);
    return rc;
}
