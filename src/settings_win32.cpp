// The file-association feature: Windows-only, by nature and not by accident.
//
// Split out of settings.cpp so that the ini reader and writer can compile on a
// non-Windows compiler. Everything here writes HKEY_CURRENT_USER, puts up a
// MessageBoxW or opens an ms-settings: deep link.
//
// Deliberately NOT put behind the platform seam in platform_paths.h. "Make this
// app the default handler for .md" has no honest cross-platform equivalent --
// xdg-mime and LaunchServices are different models with different semantics, not
// two implementations of one call -- so pretending otherwise behind one signature
// would be worse than a Windows-only translation unit that says so. When a Linux
// or macOS build needs this feature it gets its own file and its own entry point,
// which used to be the same shape platform_paths_posix.cpp had. THAT PRECEDENT IS
// GONE, and in the opposite direction: the paths seam collapsed to ONE non-Windows
// file, platform_paths_qt.cpp, because Qt answers the question identically on Linux
// and macOS. So this file is now the weaker case, not the matching one -- it stays
// Windows-only because file-association registration has no single Qt call, and if
// one ever appears this translation unit should collapse the same way.
#include "settings.h"
#include "document.h"
#include "i18n.h"

#include <windows.h>
// shlobj.h IS LIVE, and it is here because a symbol-grep screen said it was not.
// The 2026-09-20 dead-include sweep proposed deleting it; the Windows build
// refused with three errors on ONE line -- SHChangeNotify, SHCNE_ASSOCCHANGED
// and SHCNF_IDLIST at :152, the file-association notify. None of the three was
// in the screen's pattern list, because a header's symbol surface is bigger than
// any pattern anybody writes for it. The compile is the test; the grep is only a
// way to pick which compiles to run.
#include <shlobj.h>
#include <shellapi.h>
#include <appmodel.h>
#include <string>

// settingsLanguageIndex came across with this block: its only caller is
// askAndRegisterFileAssociation below, for the MessageBoxW strings.
static int settingsLanguageIndex(const Settings& settings) {
    if (settings.language != "auto") {
        int index = languageIndexById(settings.language);
        if (index >= 0) return index;
    }
    return detectSystemLanguage();
}

// Packaged (MSIX/Store) installs declare file associations in AppxManifest.xml;
// runtime registry writes would only land in the package's virtual registry.
static bool isRunningPackaged() {
    UINT32 length = 0;
    return GetCurrentPackageFullName(&length, nullptr) != APPMODEL_ERROR_NO_PACKAGE;
}

static bool hasRegisteredFileAssociation(std::wstring_view extension) {
    HKEY hKey;
    LONG result = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Tinta\\Capabilities\\FileAssociations",
        0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) return false;

    wchar_t value[64] = {};
    DWORD type = 0;
    DWORD size = sizeof(value);
    result = RegQueryValueExW(
        hKey, extension.data(), nullptr, &type,
        reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(hKey);
    return result == ERROR_SUCCESS &&
        type == REG_SZ &&
        wcscmp(value, L"Tinta.MarkdownFile") == 0;
}

bool registerFileAssociation() {
    // Get the path to the current executable
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HKEY hKey;
    LONG result;
    const wchar_t* progId = L"Tinta.MarkdownFile";

    // Create ProgID entry in Classes
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* desc = L"Tinta Document";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)desc, (DWORD)((wcslen(desc) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Create DefaultIcon entry
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile\\DefaultIcon", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    std::wstring iconPath = exePath;
    iconPath += L",0";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)iconPath.c_str(), (DWORD)((iconPath.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Create shell\open\command entry
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Classes\\Tinta.MarkdownFile\\shell\\open\\command", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    std::wstring command = L"\"";
    command += exePath;
    command += L"\" \"%1\"";
    RegSetValueExW(hKey, nullptr, 0, REG_SZ, (BYTE*)command.c_str(), (DWORD)((command.length() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Register app capabilities (required for Windows 10/11)
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Tinta\\Capabilities", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* appName = L"Tinta";
    const wchar_t* appDesc = L"A fast, lightweight Markdown and Mermaid reader";
    RegSetValueExW(hKey, L"ApplicationName", 0, REG_SZ, (BYTE*)appName, (DWORD)((wcslen(appName) + 1) * sizeof(wchar_t)));
    RegSetValueExW(hKey, L"ApplicationDescription", 0, REG_SZ, (BYTE*)appDesc, (DWORD)((wcslen(appDesc) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    // Register file associations in capabilities
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Tinta\\Capabilities\\FileAssociations", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    for (std::wstring_view extension : DOCUMENT_FILE_EXTENSIONS) {
        result = RegSetValueExW(
            hKey, extension.data(), 0, REG_SZ, (BYTE*)progId,
            (DWORD)((wcslen(progId) + 1) * sizeof(wchar_t)));
        if (result != ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return false;
        }
    }
    RegCloseKey(hKey);

    // Add to RegisteredApplications
    result = RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications", 0, nullptr,
                              REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
    if (result != ERROR_SUCCESS) return false;
    const wchar_t* capPath = L"Software\\Tinta\\Capabilities";
    RegSetValueExW(hKey, L"Tinta", 0, REG_SZ, (BYTE*)capPath, (DWORD)((wcslen(capPath) + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);

    for (std::wstring_view extension : DOCUMENT_FILE_EXTENSIONS) {
        std::wstring keyPath = L"Software\\Classes\\";
        keyPath.append(extension);
        keyPath += L"\\OpenWithProgids";
        result = RegCreateKeyExW(
            HKEY_CURRENT_USER, keyPath.c_str(), 0, nullptr,
            REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hKey, nullptr);
        if (result != ERROR_SUCCESS) return false;
        result = RegSetValueExW(hKey, progId, 0, REG_NONE, nullptr, 0);
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS) return false;
    }

    // Notify shell of the change
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);

    return true;
}

void openDefaultAppsSettings() {
    ShellExecuteW(nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr, SW_SHOWNORMAL);
}

void askAndRegisterFileAssociation(Settings& settings) {
    int lang = settingsLanguageIndex(settings);
    if (isRunningPackaged()) {
        // Store installs: the package manifest already registers the file
        // types, so only the "make default" choice is missing. Ask once per
        // install (settings.ini lives in the package's private AppData, so
        // it resets on uninstall) and deep-link straight to Tinta's page in
        // Settings > Default apps. Windows 10 ignores the query part and
        // opens the general Default Apps page instead.
        if (settings.hasAskedFileAssociation) return;
        int result = MessageBoxW(
            nullptr,
            tr(lang, "fileassoc.packaged.ask_body"),
            tr(lang, "fileassoc.packaged.title"),
            MB_YESNO | MB_ICONQUESTION);
        if (result == IDYES) {
            ShellExecuteW(nullptr, L"open",
                L"ms-settings:defaultapps?registeredAppUser=Tinta%20Markdown%20Viewer",
                nullptr, nullptr, SW_SHOWNORMAL);
        }
        settings.hasAskedFileAssociation = true;
        saveSettings(settings);
        return;
    }
    if (settings.hasAskedFileAssociation) {
        if (hasRegisteredFileAssociation(L".md") &&
            !hasRegisteredFileAssociation(L".mmd") &&
            !registerFileAssociation()) {
            MessageBoxW(
                nullptr,
                tr(lang, "fileassoc.add_mmd_failed_body"),
                tr(lang, "fileassoc.title"),
                MB_OK | MB_ICONWARNING);
        }
        return;
    }

    int result = MessageBoxW(
        nullptr,
        tr(lang, "fileassoc.ask_body"),
        tr(lang, "fileassoc.title"),
        MB_YESNO | MB_ICONQUESTION
    );

    if (result == IDYES) {
        if (registerFileAssociation()) {
            MessageBoxW(nullptr,
                       tr(lang, "fileassoc.done_body"),
                       tr(lang, "fileassoc.done_title"),
                       MB_OK | MB_ICONINFORMATION);
            openDefaultAppsSettings();
        } else {
            MessageBoxW(nullptr, tr(lang, "fileassoc.register_failed_body"),
                       tr(lang, "error.title"), MB_OK | MB_ICONWARNING);
        }
    }

    settings.hasAskedFileAssociation = true;
    saveSettings(settings);
}
