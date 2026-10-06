// ON THE Qt BUILD SINCE 2026-10-06, when the Direct2D stack was retired: the strokes go to
// handleKeyDown and handleCharInput with a null window handle, exactly as src/qt_window.cpp
// delivers them, where they used to go with a message-only HWND. The configuration home is
// pinned by tests/qt_test_app.h. The cases are unchanged.
#include "input.h"
#include "keymap.h"
#include "qt_test_app.h"
#include "settings.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <system_error>

// keymap.h carries the virtual-key codes a portable source names; these two are named only
// here. The Windows values, so a Windows build that does see <windows.h> agrees.
#ifndef VK_F3
#define VK_F3 0x72
#endif
#ifndef VK_OEM_1
#define VK_OEM_1 0xBA
#endif

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// Same contract as the main message loop: a consumed key must not have
// its layout-dependent WM_CHAR translated into the new editor/overlay.
void stroke(App& app, unsigned vk, unsigned character = 0) {
    bool consumed = handleKeyDown(app, app.hwnd, vk);
    if (!consumed && character) handleCharInput(app, app.hwnd, (wchar_t)character);
}
}

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;
    auto state = std::make_unique<App>();
    App& app = *state;
    app.currentFile = TINTA_SHORTCUT_FIXTURE;
    app.width = 1050;
    app.height = 900;
    Settings settings;
    settings.keyProfile = "windows";
    applyKeymap(app, settings);

    stroke(app, VK_F1);
    check(app.showHelp, "F1 opens Help with no character message");
    stroke(app, VK_F1);
    check(!app.showHelp, "F1 closes Help");
    stroke(app, 'P', 'p');
    check(!app.showHelp, "P cannot open Help");

    std::wstring original;
    for (unsigned ch : {0x65, 0x0443, 0x03B5}) {
        app.editMode = false;
        stroke(app, 'E', ch);
        check(app.editMode && !app.editorDirty, "E enters a clean editor for Latin/Cyrillic/Greek input");
        check(app.editorText.rfind(L"# Shortcut regression:", 0) == 0,
              "the mode-switch key cannot prefix the Markdown heading");
        original = app.editorText;
        stroke(app, 'X', 'x');
        check(app.editorText == L"x" + original, "immediate next character is preserved without a timer");
    }
    app.editMode = false;
    stroke(app, 'E', 0x0443);
    original = app.editorText;
    stroke(app, VK_F1);
    check(app.showHelp && app.editMode, "F1 opens Help while editing");
    stroke(app, 'E', 'e');
    check(app.editorText == original, "Help captures printable input over the editor");
    stroke(app, VK_F1);
    check(!app.showHelp && app.editMode && !app.editorDirty, "Help closes over a clean editor");
    stroke(app, 'P', 'p');
    check(app.editorText == L"p" + original, "normal P typing survives in editor");

    app.editMode = false;
    app.showSearch = true;
    app.searchActive = true;
    app.searchJustOpened = false;
    stroke(app, 'E', 0x0443);
    check(!app.editMode && app.searchQuery == L"\x0443", "search receives non-Latin E text");
    stroke(app, VK_ESCAPE);
    app.showToc = true;
    app.tocPinned = false;
    stroke(app, 'E', 0x0443);
    check(!app.editMode && app.tocFilter == L"\x0443", "unpinned Contents receives non-Latin filter text");
    app.tocPinned = true;
    stroke(app, 'E', 0x0443);
    check(app.editMode, "pinned Contents lets E reach the document");

    app.editMode = false;
    app.showToc = false;
    app.showSettings = true;
    stroke(app, 'E', 'e');
    stroke(app, VK_F1);
    check(!app.editMode && !app.showHelp, "Settings retains modal keyboard capture");
    app.showSettings = false;
    settings.keyProfile = "vim";
    applyKeymap(app, settings);
    stroke(app, VK_OEM_2, '?');
    check(app.showHelp, "vim punctuation opens Help");
    stroke(app, VK_OEM_2, '?');
    check(!app.showHelp, "vim punctuation closes Help");
    stroke(app, VK_OEM_2, '/');
    stroke(app, 'E', 'e');
    check(app.showSearch && app.searchQuery == L"e", "vim search keeps its first query character");
    stroke(app, VK_ESCAPE);
    stroke(app, VK_OEM_1, 0xFF1A);
    check(app.editMode && !app.editorDirty, "full-width colon still enters a clean editor");

    app.editMode = false;
    settings.keyProfile = "custom";
    settings.keyOverrides = {{"edit", "F2"}, {"help", "H"}};
    applyKeymap(app, settings);
    stroke(app, 'H', 'h');
    check(app.showHelp, "custom letter Help opens only once");
    stroke(app, 'H', 'h');
    check(!app.showHelp, "custom letter Help closes without reopening");
    stroke(app, VK_F2);
    check(app.editMode && !app.editorDirty, "custom function key enters Edit");
    app.confirmExitPending = true;
    settings.keyOverrides = {{"help", "F3"}};
    applyKeymap(app, settings);
    stroke(app, VK_F3);
    check(app.confirmExitPending && !app.showHelp, "Help cannot bypass unsaved-changes confirmation");

    // --- the file watch's 0-means-could-not-read contract -------------------
    //
    // platform::fileWriteTicks returns 0 both for a missing file and for one it
    // could not read, and the watch loop must treat that as "no information" rather
    // than as a timestamp. Getting it wrong is not a crash: a 0 stored into
    // lastFileWriteTime compares unequal to the next real reading, so the app
    // raises the unsaved-changes dialog over an external edit that never happened.
    // That is the trap file_utils.cpp:40-45 names, and it is tested here rather
    // than read off the source, because both halves of it are one-line guards.
    {
        app.showHelp = false;
        app.confirmExitPending = false;
        app.editMode = true;
        app.editorDirty = true;   // the dirty branch is the only one that prompts
        app.fileWatchEnabled = true;

        // App starts with NO tabs and the watch loop iterates app.tabs, so without
        // this the loop body never runs and both assertions below would pass for a
        // reason that has nothing to do with the contract -- which is exactly the
        // shape the positive control at the end of this block exists to catch.
        if (app.tabs.empty()) app.tabs.emplace_back();
        app.activeTab = 0;

        const std::filesystem::path probe =
            std::filesystem::temp_directory_path() / "tinta-watch-probe.md";
        std::error_code ec;
        std::filesystem::remove(probe, ec);
        check(!std::filesystem::exists(probe),
              "CONTROL: the watch probe file does not exist yet");

        // A recognisable stored time. Nothing may overwrite it while the file
        // cannot be read -- not even with a plausible-looking value.
        const FILETIME sentinel{0x11223344u, 0x55667788u};
        app.currentFile = probe.string();
        app.lastFileWriteTime = sentinel;

        handleFileWatchTimer(app, app.hwnd);
        check(app.lastFileWriteTime.dwLowDateTime == sentinel.dwLowDateTime &&
                  app.lastFileWriteTime.dwHighDateTime == sentinel.dwHighDateTime,
              "an unreadable file leaves the stored write time ALONE, not zeroed");
        check(!app.confirmExitPending,
              "an unreadable file raises no external-edit prompt");

        // POSITIVE CONTROL, and the test is worthless without it: an
        // implementation that simply never prompted would pass both assertions
        // above. So the same state with a file that DOES exist must prompt, and
        // must store the reading it just took.
        { std::ofstream out(probe, std::ios::binary); out << "# watched\n"; }
        check(std::filesystem::exists(probe),
              "CONTROL: the watch probe file was created");
        app.lastFileWriteTime = FILETIME{0, 0};
        handleFileWatchTimer(app, app.hwnd);
        check(app.confirmExitPending,
              "CONTROL: a real external change does raise the prompt");
        check(app.lastFileWriteTime.dwLowDateTime != 0 ||
                  app.lastFileWriteTime.dwHighDateTime != 0,
              "CONTROL: a readable file's time IS stored");

        app.confirmExitPending = false;
        app.editMode = false;
        app.editorDirty = false;
        std::filesystem::remove(probe, ec);
    }

    std::cout << "Input shortcuts: " << failures << " failures\n";
    return failures ? 1 : 0;
}
