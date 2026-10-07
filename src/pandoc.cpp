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
// shlobj.h did NOT make this file portable -- it changed how it failed. It was the
// one row in scripts/census-nonportable-linux.sh whose first cause was shlobj.h,
// which meant the census stopped at line 16 and could report nothing else about it.
// The Win32 surface that removal then exposed -- MAX_PATH, DWORD, HANDLE,
// STARTUPINFOW, PROCESS_INFORMATION, CreateProcessW, WaitForSingleObject,
// GetExitCodeProcess, TerminateProcess, CloseHandle, CreateThread, GetTempPathW,
// GetCurrentProcessId, DeleteFileW, swprintf_s and _countof -- is gone as of the
// process-seam slice. platform_process.h runs the child, std::thread replaces
// CreateThread, std::filesystem owns the temp file, and the only Win32 spelling left
// in this file is the HWND the completion message is posted to.
//
// AN UNRELATED ERROR ON A DECLARATION MASKS THE DIAGNOSTIC ON ITS USE, AND THAT IS
// WHY THE CENSUS UNDERSTATED THIS FILE. The rule fires at FUNCTION scope, not only at
// file scope: `DWORD WINAPI pandocWorker(LPVOID param)` failed at its own signature,
// so g++ reported nothing whatever about its body -- STARTUPINFOW,
// PROCESS_INFORMATION, CreateProcessW, WaitForSingleObject, GetExitCodeProcess,
// TerminateProcess, both CloseHandles and a DeleteFileW were all invisible to the
// instrument that had been sizing this port. Measured before the port: the census saw
// 12 errors on 9 lines, and supplying only the five declaration types through
// -include, changing nothing else, gave 19 errors on 16 lines. It happened one line
// at a time too -- `HANDLE thread` hid the CreateThread in its own initialiser, and
// `wchar_t tempFile[MAX_PATH]` hid an ofstream construction from the C58 wide-path
// sweep. So an error count is a FLOOR on a file's Win32 surface and never a
// measurement of it: the source is the denominator and the compiler is the
// cross-check.

#include "platform_process.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

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
    std::wstring exe;
    // AN ARGUMENT LIST, NOT A COMMAND LINE. The quoting is the Windows arm's business
    // (platform_process.h:55-70 says why, win32_command_line.h implements it), so what
    // crosses to the worker is the vector the caller meant rather than one string it
    // had to escape by hand.
    std::vector<std::wstring> args;
    std::filesystem::path tempFile;  // deleted when the run finishes
};

// Not a Win32 thread callback any more, so it has an ordinary C++ signature and
// returns nothing: std::thread carries the argument with its own type and nothing here
// ever read a thread exit code. What it DOES is unchanged -- run pandoc on a
// two-minute budget, delete the staged input, tell the window.
void pandocWorker(PandocJob* job) {
    // Big documents convert in seconds; two minutes is a hang
    const platform::ProcessResult run =
        platform::runProcessAndWait(job->exe, job->args, 120000);

    // Three ways to fail and one way to succeed, and the code this replaced drew the
    // line in the same three places: CreateProcessW returning false left ok false, a
    // wait that did not reach WAIT_OBJECT_0 was terminated and left ok false, and only
    // GetExitCodeProcess reporting 0 set it.
    const bool ok = run.launched && !run.timedOut && run.exitCode == 0;

    if (!job->tempFile.empty()) {
        // Ignored for the reason the DeleteFileW here also ignored its return: the
        // conversion's outcome is already decided and a leftover temp file is not worth
        // changing the answer the user sees.
        std::error_code ignored;
        std::filesystem::remove(job->tempFile, ignored);
    }
    postWindowMessage(job->hwnd, WM_APP_PANDOC_DONE, ok ? 1 : 0, 0);
    delete job;
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
    // convention and belongs in the platform seam (QStandardPaths::findExecutable applies
    // PATHEXT), not here.
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
    // math as SVG) for rich targets, the raw buffer for LaTeX.
    //
    // The error_code overload of temp_directory_path, not the throwing one, and that is
    // behaviour preservation rather than caution: GetTempPathW reported failure by
    // leaving its buffer empty, the name was then built relative to the working
    // directory, and an empty path on the left of operator/ does exactly the same. The
    // process id makes the name unique between concurrently running instances, which is
    // the only guarantee platform_process.h:92-96 offers and the only one needed.
    std::error_code tempEc;
    const std::filesystem::path tempDir = std::filesystem::temp_directory_path(tempEc);
    const std::filesystem::path tempFile =
        tempDir / (L"tinta-pandoc-" + std::to_wstring(platform::currentProcessId()) +
                   (f.htmlBridge ? L".html" : L".md"));
    if (f.htmlBridge) {
        // The parse tree can lag the buffer while the render is hidden
        if (app.editMode) editorReparse(app);
        if (!exportHtmlFile(app, tempFile.wstring())) return;
    } else {
        std::string md = app.editMode ? toUtf8Str(app.editorText)
                                      : std::string();
        if (md.empty() && !app.currentFile.empty()) {
            std::ifstream in(std::filesystem::path(toWideStr(app.currentFile)),
                             std::ios::binary);
            md.assign(std::istreambuf_iterator<char>(in),
                      std::istreambuf_iterator<char>());
        }
        // This construction was invisible to the compiler until the
        // `wchar_t tempFile[MAX_PATH]` it used stopped being an error off Windows; the
        // masking rule that hid it is stated once, at the top of this file, rather than
        // in two places that can drift apart. tempFile is an fs::path now, so there is
        // no C58 conversion left here to get wrong.
        std::ofstream outFile(tempFile, std::ios::binary);
        if (!outFile) return;
        outFile.write(md.data(), (std::streamsize)md.size());
    }

    // Assemble the arguments; the writer comes from the -o extension.
    //
    // ONE ENTRY PER ARGUMENT, and the grouping is what the old hand-built string
    // encoded in its quotes: `--metadata title="X"` is TWO arguments to pandoc, so the
    // `=` forms stay glued and the space-separated forms do not.
    std::vector<std::wstring> args;
    args.push_back(L"-f");
    args.push_back(f.htmlBridge ? L"html" : L"gfm");
    args.push_back(L"-s");
    args.push_back(L"--metadata");
    // THE TITLE'S DOUBLE QUOTES SURVIVE NOW, and this is the slice's one deliberate
    // behaviour change rather than a refactor. A loop here used to rewrite every `"` in
    // the stem to `'`, purely because the stem was being concatenated INTO a hand-quoted
    // command line and there was nothing to escape it with. With a list there is nothing
    // to escape it into, so a document called `The "Real" Thing` now carries its own
    // punctuation into the exported file's title metadata.
    args.push_back(L"title=" + stem);
    if (!f.htmlBridge && !app.currentFile.empty()) {
        // Relative image paths in the raw markdown resolve against the
        // document's own folder
        std::wstring dir = toWideStr(app.currentFile);
        size_t slash = dir.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            args.push_back(L"--resource-path=" + dir.substr(0, slash));
        }
    }
    args.push_back(L"-o");
    args.push_back(out);
    args.push_back(tempFile.wstring());

    PandocJob* job = new PandocJob{hwnd, app.pandocExe, args, tempFile};
    app.pandocRunning = true;
    // std::thread rather than CreateThread: threading is standard C++ and needs no
    // platform arm, so it does not go behind the seam. The catch reproduces the old
    // `if (thread) ... else` exactly -- std::thread reports a creation failure by
    // throwing std::system_error where CreateThread returned null, and the recovery is
    // the same three statements it was.
    try {
        std::thread(pandocWorker, job).detach();
    } catch (const std::system_error&) {
        app.pandocRunning = false;
        std::error_code ignored;
        std::filesystem::remove(tempFile, ignored);
        delete job;
    }
}
