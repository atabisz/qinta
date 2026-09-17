#include "drafts.h"
#include "i18n.h"
#include "settings.h"
#include "signals.h"
#include "tabs.h"
#include "editor.h"
#include "utils.h"
#include "platform_paths.h"
#include "portable_str.h"

#include <cctype>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <sstream>
#ifndef _WIN32
#include <unistd.h>  // getpid, for the draft filename's process id
#endif

// THE <shlobj.h> THIS FILE OPENED WITH IS GONE, AND IT WAS NEVER USED FOR WHAT ITS
// NAME SUGGESTS. Nothing here called a shell-folder function: the config directory
// comes from tintaConfigDir(), and shlobj.h was doing one job only -- dragging in
// windows.h for CreateDirectoryW, DeleteFileW and the FindFirstFileW family. It is
// deleted rather than guarded because on Windows this file already reaches
// windows.h through drafts.h -> app.h -> <d2d1.h>, so a conditional include would
// have been a second path to a header that was already there.
//
// That is also why the census could not size this file. A fatal include stops the
// read at line 9, so `errors 1` meant "never read", not "one thing to fix": with
// shlobj.h stubbed g++ reported 29 errors from SIX causes, and one of those hid
// behind another on the same line (_wtoi at :111 never got its own diagnostic
// because DWORD failed first). Five of the six had destinations the tree already
// built or already documented; only process liveness needed a decision.

// This process's id, as the one unsigned type the draft filename's %lu wants.
// DWORD off Windows does not exist and pid_t is signed, so the cast happens here
// once rather than at both call sites.
static unsigned long currentProcessId() {
#ifdef _WIN32
    return (unsigned long)GetCurrentProcessId();
#else
    return (unsigned long)::getpid();
#endif
}

// Delete one draft, best effort. Named because DeleteFileW appeared at four sites
// and its portable spelling needs an error_code to keep the same contract: this
// code never checked DeleteFileW's return value, so a missing or locked file has
// always been a silent no-op and still is. The throwing overload of
// filesystem::remove would have turned that into an exception at four sites.
static void removeDraftFile(const std::wstring& path) {
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(path), ec);
}

// The config home's drafts folder (portable beside the exe, else
// %APPDATA%\Tinta), created on first use
static std::wstring draftsDirPath() {
    std::wstring path = tintaConfigDir();
    if (path.empty()) return L"";
    path += platform::pathSeparator();
    path += L"drafts";
    // create_directories, not create_directory, and the error_code overload:
    // CreateDirectoryW returned FALSE for "already exists" and this code ignored
    // that, so throwing here would be a new failure mode rather than a port.
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path), ec);
    return path;
}

// Draft identity: process + window + tab, so several windows (and several
// Tinta processes) never overwrite each other's autosaves
static std::wstring draftFileFor(const App& app, int tabId) {
    std::wstring dir = draftsDirPath();
    if (dir.empty()) return L"";
    wchar_t name[96];
    // The leading separator left the format string and became a join, so the
    // pattern this file scans for later ("draft-") is now the whole prefix rather
    // than a prefix with a backslash in front of it. draftsScanForRecovery reads
    // the same 6 characters either way.
    tinta::swprintf(name, L"draft-%lu-%llu-%d.md", currentProcessId(),
                    (unsigned long long)(uintptr_t)app.hwnd, tabId);
    return dir + platform::pathSeparator() + name;
}

// A draft is the buffer prefixed with one comment line naming the file it
// belongs to ('>' cannot appear in a Windows path, so the terminator is
// unambiguous); an untitled quick note stores an empty path
static void writeDraft(const std::wstring& draftPath,
                       const std::string& origPath,
                       const std::wstring& text) {
    if (draftPath.empty()) return;
    // std::ofstream out(draftPath, ...) is an MSVC extension -- there is no POSIX
    // spelling that takes a wstring. The tree's route is the filesystem::path
    // wrapper, documented at file_utils.cpp:190 and used at :238 and i18n.cpp:1517.
    std::ofstream out(std::filesystem::path(draftPath), std::ios::binary);
    if (!out) return;
    std::string header = "<!-- tinta-draft:" + origPath + " -->\n";
    std::string body = toUtf8(text);
    out.write(header.data(), header.size());
    out.write(body.data(), body.size());
}

void draftsSweep(App& app) {
    tabsInit(app);
    for (int i = 0; i < (int)app.tabs.size(); i++) {
        const App::DocTab& tab = app.tabs[i];
        bool active = (i == app.activeTab);
        bool dirty = active ? (app.editMode && app.editorDirty)
                            : (tab.editMode && tab.editorDirty);
        std::wstring draftPath = draftFileFor(app, tab.id);
        if (draftPath.empty()) continue;
        if (dirty) {
            writeDraft(draftPath, active ? app.currentFile : tab.path,
                       active ? app.editorText : tab.editorText);
        } else {
            removeDraftFile(draftPath);
        }
    }
}

void draftsDeleteForTab(const App& app, int tabId) {
    std::wstring draftPath = draftFileFor(app, tabId);
    if (!draftPath.empty()) removeDraftFile(draftPath);
}

void draftsDeleteAll(const App& app) {
    for (const App::DocTab& tab : app.tabs) {
        draftsDeleteForTab(app, tab.id);
    }
}

// True when the pid is a running Tinta process (its drafts are live
// autosaves, not crash leftovers). Reused pids of other programs fail
// the image-name check and stay recoverable.
//
// THE ONE PLACE IN THIS FILE WITH NO PORTABLE DESTINATION, so it is split in place
// rather than given a seam: file-local, single-use, and OpenProcess /
// QueryFullProcessImageNameW appear at exactly one site in the whole tree. Same
// reasoning as signals.cpp's parkTimestamp last slice -- a seam is two files and a
// declaration for a decision that recurs, and this one does not.
//
// THE TWO ARMS DO NOT GIVE THE SAME GUARANTEE, and that is written here rather than
// hidden behind the shared name. Both answer two questions -- is the pid alive, and
// is it us -- but:
//
//   Windows asks the kernel directly, and a pid belonging to another user's process
//   fails OpenProcess, so it reads as NOT ours.
//   Linux reads /proc/<pid>/exe, where the symlink resolves only for a live process
//   and its target names the image. A process owned by another user resolves for
//   the link's existence but EACCES on the read, so it also reads as NOT ours.
//   Anywhere without /proc -- macOS, the BSDs -- the read simply fails and EVERY
//   pid reads as not ours.
//
// The last case is the one to know about, and it fails in the direction Windows
// already fails in when OpenProcess is refused: a draft belonging to a live sibling
// gets OFFERED for recovery. That is a visible, recoverable annoyance (the user is
// shown a draft they did not lose), never data loss -- draftsRecoverAll opens the
// draft as a new dirty tab and the sibling's own file is untouched. It is not fixed
// here because fixing it means a real macOS implementation (proc_pidpath) on a
// platform this tree does not build for yet, and a stub returning "alive" would be
// the silent success win32_api.h's header spends twenty lines forbidding.
static bool isLiveTintaProcess(unsigned long pid) {
#ifdef _WIN32
    HANDLE proc =
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    if (!proc) return false;
    wchar_t image[MAX_PATH];
    DWORD size = _countof(image);
    bool live = false;
    if (QueryFullProcessImageNameW(proc, 0, image, &size)) {
        std::wstring path(image, size);
        size_t slash = path.find_last_of(L'\\');
        std::wstring base =
            slash == std::wstring::npos ? path : path.substr(slash + 1);
        for (wchar_t& c : base) c = (wchar_t)towlower(c);
        live = (base == L"tinta.exe");
    }
    CloseHandle(proc);
    return live;
#else
    // read_symlink, not exists(): /proc/<pid> exists for a zombie whose image is
    // gone, and it is the LINK TARGET that names the process. The error_code
    // overload keeps this a predicate rather than something that throws into a
    // startup path.
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink(
        std::filesystem::path("/proc") / std::to_string(pid) / "exe", ec);
    if (ec) return false;
    std::string base = exe.filename().string();
    for (char& c : base) c = (char)std::tolower((unsigned char)c);
    // No ".exe" here, and that is the executable's real name rather than an
    // oversight: the Windows arm's literal is carried, not shared.
    return base == "tinta";
#endif
}

void draftsScanForRecovery(App& app) {
    app.recoveredDrafts.clear();
    std::wstring dir = draftsDirPath();
    if (dir.empty()) return;

    // The "draft-*.md" wildcard became an explicit prefix and suffix test, because
    // directory_iterator has nothing to hand a pattern to. file_utils.cpp:101 made
    // the same move and says the same thing: there was never a pattern to match,
    // only an API that demanded one.
    //
    // ONE HONEST DIFFERENCE, in the direction of matching FEWER files. FindFirstFileW
    // matched case-insensitively (and against 8.3 short names); these two tests are
    // case-sensitive. Nothing tinta writes can differ -- draftFileFor produces the
    // prefix and extension literally, in lower case -- so only a hand-renamed
    // `DRAFT-123.MD` changes behaviour, and it changes from "offered" to "ignored".
    // Recorded rather than corrected: making it case-insensitive would be a
    // behaviour decision, and this pass is a port.
    //
    // The error_code overload replaces the INVALID_HANDLE_VALUE early return. On a
    // missing or unreadable directory it yields end(), so the loop body never runs --
    // the same nothing, without an exception in a startup path.
    std::error_code ec;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(std::filesystem::path(dir), ec)) {
        std::wstring name = entry.path().filename().wstring();
        if (name.rfind(L"draft-", 0) != 0) continue;
        if (name.size() < 3 || name.compare(name.size() - 3, 3, L".md") != 0) {
            continue;
        }
        // wcstoul, not _wtoi: the MSVC name had no diagnostic of its own in the
        // stub read because DWORD failed first on the same line, which is why a
        // per-line error count is not a cause count. Base 10 is explicit so a
        // future pid with a leading zero cannot be read as octal.
        unsigned long pid = std::wcstoul(name.c_str() + 6, nullptr, 10);
        if (pid == currentProcessId()) continue;
        if (pid != 0 && isLiveTintaProcess(pid)) continue;
        app.recoveredDrafts.push_back(dir + platform::pathSeparator() + name);
    }

    if (!app.recoveredDrafts.empty()) {
        // Offer the leftovers as a signal chip: click or Restore brings
        // them back as dirty tabs, the cross discards them for good
        wchar_t label[96];
        tinta::swprintf(label, tr(app, "draft.recovered"),
                        (int)app.recoveredDrafts.size());
        signalPush(app, SIG_WARN, SIGI_FILE, label, L"", L"",
                   SIGA_RESTORE_DRAFTS, SIGC_DISCARD_DRAFTS, false);
    }
}

void draftsRecoverAll(App& app, HWND hwnd) {
    for (const std::wstring& path : app.recoveredDrafts) {
        std::ifstream file(std::filesystem::path(path), std::ios::binary);
        if (!file) continue;
        std::stringstream buf;
        buf << file.rdbuf();
        std::string content = buf.str();
        file.close();

        std::string origPath;
        const std::string prefix = "<!-- tinta-draft:";
        if (content.rfind(prefix, 0) == 0) {
            size_t end = content.find(" -->\n", prefix.size());
            if (end != std::string::npos) {
                origPath = content.substr(prefix.size(),
                                          end - prefix.size());
                content = content.substr(end + 5);
            }
        }
        enterRecoveredDraft(app, hwnd, content, origPath);
        removeDraftFile(path);
    }
    app.recoveredDrafts.clear();
    requestRepaint(hwnd);
}

void draftsDiscardAll(App& app) {
    for (const std::wstring& path : app.recoveredDrafts) {
        removeDraftFile(path);
    }
    app.recoveredDrafts.clear();
}
