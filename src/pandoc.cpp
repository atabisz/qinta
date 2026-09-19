// Pandoc bridge: hands the document to a locally installed pandoc for
// the formats our native exporters don't cover. Rich targets (docx, odt,
// epub, pptx, rtf) are fed Tinta's own HTML export, so diagrams and math
// arrive as SVG instead of decaying into code blocks; LaTeX is fed the
// raw markdown since that conversion is source-to-source.

#include "pandoc.h"

#include "editor.h"
#include "export.h"
#include "i18n.h"
#include "settings.h"
#include "platform_paths.h"
#include "file_dialog.h"

// NO WINDOWS HEADER OF ITS OWN, and the absence is measured rather than tidy.
// This file carried <shlobj.h> and used no shell API at all -- probed at HEAD for
// SHGetKnownFolderPath, FOLDERID_*, CSIDL_* and IShellLink, none present -- so it
// was inherited rather than needed. What it WAS doing was reaching windows.h, and
// editor.h above already does that -- by TWO routes, which is why removing shlobj.h
// changes nothing on Windows: editor.h:4 includes app.h, which reaches windows.h
// through <d2d1.h>, and editor.h:12 includes win32_types.h, whose _WIN32 arm includes
// it directly. So the Win32 calls in pandocExportFlow below still compile here.
//
// THE FIRST DRAFT OF THIS COMMENT QUOTED win32_types.h AS BEING "#include <windows.h>
// AND NOTHING ELSE" ON WINDOWS, WHICH IS WHAT THAT FILE SAYS ABOUT ITSELF AND IS NOT
// WHAT IT DOES. Its _WIN32 arm (:85-95) is WIN32_LEAN_AND_MEAN, then NOMINMAX, then
// <windows.h>, then <cstddef>, <cstdint> and <type_traits>. The correction is recorded
// here rather than silently fixed because of HOW it got in: I quoted a neighbouring
// comment instead of reading the code under it, so an absence claim propagated one file
// without ever being probed -- which is the exact failure this tree has catalogued as
// the costliest kind, committed while writing a comment about measuring things.
// win32_types.h's own line has been corrected too, so the quote and its source agree.
//
// WIN32_LEAN_AND_MEAN is the part that would actually have mattered had it bitten: it
// trims windows.h rather than extending it, so "windows.h is reachable" is not the same
// claim as "every Win32 declaration is reachable". Everything this file uses is in the
// lean set, and the green build is the evidence for that rather than this sentence.
//
// Off Windows the same chain supplies only the stand-in value types, so removing
// shlobj.h does NOT make this file portable -- it changes how it fails. It was the
// one row in scripts/census-nonportable-linux.sh whose first cause was shlobj.h,
// which meant the census stopped at line 16 and could report nothing else about it.
// The remaining Win32 surface is real and is all in the export flow: MAX_PATH,
// DWORD, HANDLE, STARTUPINFOW, PROCESS_INFORMATION, CreateProcessW,
// WaitForSingleObject, GetExitCodeProcess, TerminateProcess, CloseHandle,
// CreateThread, GetTempPathW, GetCurrentProcessId, DeleteFileW, swprintf_s and
// _countof. That is a PROCESS seam, not a paths one, and it is the next slice.

#include <fstream>
#include <string>

namespace {

struct PandocFormat {
    const wchar_t* label;
    const wchar_t* ext;      // with the dot
    const wchar_t* filter;   // save-dialog filter description
    bool htmlBridge;         // feed our HTML export instead of raw md
};

const PandocFormat kFormats[PANDOC_FORMAT_COUNT] = {
    {L"EPUB", L".epub", L"EPUB e-book (*.epub)", true},
    {L"OpenDocument", L".odt", L"OpenDocument text (*.odt)", true},
    {L"PowerPoint", L".pptx", L"PowerPoint deck (*.pptx)", true},
    {L"Rich Text", L".rtf", L"Rich Text Format (*.rtf)", true},
    {L"LaTeX", L".tex", L"LaTeX source (*.tex)", false},
};

std::wstring toWideStr(const std::string& s) {
    return platform::fromUtf8(s);
}

std::string toUtf8Str(const std::wstring& s) {
    return platform::toUtf8(s);
}

// The document's display stem, for default filenames and epub metadata
std::wstring docStem(const App& app) {
    std::wstring name = toWideStr(app.currentFile);
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos) name = name.substr(slash + 1);
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) name = name.substr(0, dot);
    if (name.empty()) name = L"Untitled";
    return name;
}

struct PandocJob {
    HWND hwnd = nullptr;
    std::wstring cmdLine;   // full command line, exe quoted first
    std::wstring tempFile;  // deleted when the run finishes
};

DWORD WINAPI pandocWorker(LPVOID param) {
    PandocJob* job = static_cast<PandocJob*>(param);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = job->cmdLine;  // CreateProcess needs it writable
    bool ok = false;
    if (CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Big documents convert in seconds; two minutes is a hang
        if (WaitForSingleObject(pi.hProcess, 120000) == WAIT_OBJECT_0) {
            DWORD code = 1;
            GetExitCodeProcess(pi.hProcess, &code);
            ok = (code == 0);
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    }
    if (!job->tempFile.empty()) DeleteFileW(job->tempFile.c_str());
    postWindowMessage(job->hwnd, WM_APP_PANDOC_DONE, ok ? 1 : 0, 0);
    delete job;
    return 0;
}

}  // namespace

const wchar_t* pandocFormatLabel(int fmt) {
    return fmt >= 0 && fmt < PANDOC_FORMAT_COUNT ? kFormats[fmt].label : L"";
}

const wchar_t* pandocFormatExt(int fmt) {
    return fmt >= 0 && fmt < PANDOC_FORMAT_COUNT ? kFormats[fmt].ext : L"";
}

void pandocResolve(App& app) {
    if (app.pandocChecked) return;
    app.pandocChecked = true;
    app.pandocExe.clear();

    // 1. The user's explicit choice wins
    if (!app.pandocUserPath.empty() &&
        platform::regularFileExists(app.pandocUserPath)) {
        app.pandocExe = app.pandocUserPath;
        return;
    }

    // 2. PATH. The bare name is the seam's contract -- `.exe` is a Windows
    // convention and belongs in platform_paths_win32.cpp, not here.
    std::wstring found = platform::findExecutableOnPath(L"pandoc");
    if (!found.empty()) {
        app.pandocExe = found;
        return;
    }

    // 3. The installer's default locations.
    //
    // THESE THREE VARIABLE NAMES ARE WINDOWS INSTALLER CONVENTIONS and they stay
    // here rather than behind the seam on purpose: the seam answers "what is this
    // variable set to", and which variables are worth asking about is knowledge
    // about pandoc's installer, not about the platform. Putting the table in
    // platform_paths_win32.cpp would make a platform file own a fact about pandoc.
    //
    // The honest consequence, stated rather than discovered later: off Windows all
    // three are simply unset, every probe returns empty, and resolution has already
    // fallen through to step 2 -- which is where pandoc lives on a Linux box
    // anyway, installed by a package manager onto PATH. So this step being inert
    // there is correct behaviour and not a gap. It is also NOT a claim that pandoc
    // export works off Windows: pandocExportFlow below is still Win32 throughout.
    auto probe = [&](const wchar_t* env, const wchar_t* tail) {
        if (!app.pandocExe.empty()) return;
        const std::wstring base = platform::environmentVariable(env);
        if (base.empty()) return;
        const std::wstring p = base + tail;
        if (platform::regularFileExists(p)) app.pandocExe = p;
    };
    probe(L"LOCALAPPDATA", L"\\Pandoc\\pandoc.exe");
    probe(L"ProgramFiles", L"\\Pandoc\\pandoc.exe");
    probe(L"ProgramFiles(x86)", L"\\Pandoc\\pandoc.exe");
}

bool pandocAvailable(App& app) {
    pandocResolve(app);
    return !app.pandocExe.empty();
}

void pandocSetUserPath(App& app, const std::wstring& path) {
    app.pandocUserPath = path;
    app.pandocChecked = false;
    pandocResolve(app);
    Settings settings = loadSettings();
    settings.pandocPath = toUtf8Str(path);
    saveSettings(settings);
}

void pandocExportFlow(App& app, HWND hwnd, int fmt) {
    if (fmt < 0 || fmt >= PANDOC_FORMAT_COUNT) return;
    if (!pandocAvailable(app) || app.pandocRunning) return;
    const PandocFormat& f = kFormats[fmt];

    // Save dialog for the target file. The one site of five whose filter and
    // default extension are built at runtime, which is why file_dialog.h's
    // FileFilter owns its strings instead of pointing at literals: f.ext is a
    // member of a static table, but `L"*" + f.ext` is a temporary.
    //
    // No restoreDirectory here, matching the OFN_NOCHANGEDIR this site never set.
    std::wstring stem = docStem(app);
    platform::FileDialogRequest request;
    request.owner = hwnd;
    request.filters = {{f.filter, std::wstring(L"*") + f.ext}};
    request.initialName = stem + f.ext;
    request.defaultExtension = f.ext + 1;  // skip the leading '.'
    std::optional<platform::FileDialogResult> picked =
        platform::requestSavePath(request);
    if (!picked) return;
    const std::wstring& out = picked->path;

    // Stage the input on the UI thread: our HTML export (diagrams and
    // math as SVG) for rich targets, the raw buffer for LaTeX
    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    wchar_t tempFile[MAX_PATH]{};
    swprintf_s(tempFile, _countof(tempFile), L"%stinta-pandoc-%lu%s",
               tempDir, GetCurrentProcessId(),
               f.htmlBridge ? L".html" : L".md");
    if (f.htmlBridge) {
        // The parse tree can lag the buffer while the render is hidden
        if (app.editMode) editorReparse(app);
        if (!exportHtmlFile(app, tempFile)) return;
    } else {
        std::string md = app.editMode ? toUtf8Str(app.editorText)
                                      : std::string();
        if (md.empty() && !app.currentFile.empty()) {
            std::ifstream in(toWideStr(app.currentFile),
                             std::ios::binary);
            md.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
        }
        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) return;
        outFile.write(md.data(), (std::streamsize)md.size());
    }

    // Assemble the command line; the writer comes from the -o extension
    std::wstring cmd = L"\"" + app.pandocExe + L"\" -f " +
                       (f.htmlBridge ? L"html" : L"gfm") + L" -s ";
    std::wstring title = stem;
    for (auto& ch : title) {
        if (ch == L'"') ch = L'\'';
    }
    cmd += L"--metadata title=\"" + title + L"\" ";
    if (!f.htmlBridge && !app.currentFile.empty()) {
        // Relative image paths in the raw markdown resolve against the
        // document's own folder
        std::wstring dir = toWideStr(app.currentFile);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            cmd += L"--resource-path=\"" + dir.substr(0, slash) + L"\" ";
        }
    }
    cmd += L"-o \"" + out + L"\" \"" + tempFile + L"\"";

    PandocJob* job = new PandocJob{hwnd, cmd, tempFile};
    app.pandocRunning = true;
    HANDLE thread = CreateThread(nullptr, 0, pandocWorker, job, 0, nullptr);
    if (thread) {
        CloseHandle(thread);
    } else {
        app.pandocRunning = false;
        DeleteFileW(tempFile);
        delete job;
    }
}
