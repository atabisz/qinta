// /register on qinta (slice 26, 2026-10-07): registerFileAssociation, run for real, against a
// registry that is not Alex's.
//
// HOW IT STAYS OFF THE REAL REGISTRY. RegOverridePredefKey maps HKEY_CURRENT_USER, for this
// process only, onto a scratch key under the real HKCU, so the function under test runs
// UNCHANGED -- no prefix parameter grown for the test -- and every key it creates lands under
// the scratch key. The override is then removed and every assertion reads back through the
// REAL HKCU at the scratch path, so the read does not share the write's redirection.
//
// The control that makes "nothing reached the real registry" a reading: the same ProgID key that
// must be ABSENT from the real HKCU must be PRESENT under the scratch key. A check that could
// only ever read "absent" would pass for a function that wrote nothing at all.
//
// The one effect that does escape is SHChangeNotify(SHCNE_ASSOCCHANGED), a broadcast telling
// Explorer to re-read associations. It writes nothing.
#include "document.h"
#include "qt_test_app.h"
#include "settings.h"

#include <cstdio>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

// A REG_SZ value under HKCU\<scratch>\<subkey>, or "" when absent.
std::wstring readString(const std::wstring& key, const wchar_t* value) {
    wchar_t buffer[1024] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_REG_SZ, nullptr, buffer,
                     &size) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

bool keyExists(const std::wstring& key) {
    HKEY handle = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, key.c_str(), 0, KEY_READ, &handle) != ERROR_SUCCESS) {
        return false;
    }
    RegCloseKey(handle);
    return true;
}

bool valueExists(const std::wstring& key, const wchar_t* value) {
    return RegGetValueW(HKEY_CURRENT_USER, key.c_str(), value, RRF_RT_ANY, nullptr, nullptr,
                        nullptr) == ERROR_SUCCESS;
}
}  // namespace

int main(int argc, char** argv) {
    QtTestApp qt(argc, argv);
    if (!qt.ok()) return 2;

    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const std::wstring exePath = exe;
    const std::wstring scratch =
        L"Software\\QintaRegisterTest-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring progIdKey = L"Software\\Classes\\Qinta.MarkdownFile";

    const bool realBefore = keyExists(progIdKey);
    std::printf("real_progid_before=%d\n", realBefore ? 1 : 0);

    HKEY scratchKey = nullptr;
    check(RegCreateKeyExW(HKEY_CURRENT_USER, scratch.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                          KEY_ALL_ACCESS, nullptr, &scratchKey, nullptr) == ERROR_SUCCESS,
          "CONTROL: the scratch key was created");
    if (!scratchKey) return 1;
    check(RegOverridePredefKey(HKEY_CURRENT_USER, scratchKey) == ERROR_SUCCESS,
          "CONTROL: HKEY_CURRENT_USER is redirected into the scratch key");
    const bool registered = registerFileAssociation();
    RegOverridePredefKey(HKEY_CURRENT_USER, nullptr);
    RegCloseKey(scratchKey);
    check(registered, "registerFileAssociation reports success");

    const std::wstring in = scratch + L"\\";
    const std::wstring command = readString(in + progIdKey + L"\\shell\\open\\command", nullptr);
    std::printf("open_command=%ls\n", command.c_str());
    check(command == L"\"" + exePath + L"\" \"%1\"",
          "the open command runs THIS executable on the clicked file");
    check(readString(in + progIdKey + L"\\DefaultIcon", nullptr) == exePath + L",0",
          "DefaultIcon is this executable's first icon resource");
    check(readString(in + progIdKey, nullptr) == L"Qinta Document",
          "the ProgID is qinta's, with qinta's document description");
    check(readString(in + L"Software\\Qinta\\Capabilities", L"ApplicationName") == L"Qinta",
          "Capabilities live under Software\\Qinta and name Qinta");
    check(readString(in + L"Software\\RegisteredApplications", L"Qinta") ==
              L"Software\\Qinta\\Capabilities",
          "RegisteredApplications lists Qinta, pointing at its Capabilities");
    for (std::wstring_view extension : DOCUMENT_FILE_EXTENSIONS) {
        const std::wstring ext(extension);
        check(readString(in + L"Software\\Qinta\\Capabilities\\FileAssociations", ext.c_str()) ==
                  L"Qinta.MarkdownFile",
              "every document extension is in Capabilities\\FileAssociations");
        check(valueExists(in + L"Software\\Classes\\" + ext + L"\\OpenWithProgids",
                          L"Qinta.MarkdownFile"),
              "every document extension offers Qinta.MarkdownFile in OpenWithProgids");
    }
    check(!keyExists(in + L"Software\\Classes\\Tinta.MarkdownFile"),
          "nothing is registered under tinta's ProgID");

    // Nothing reached the real HKCU -- and the scratch key holding the same key is the control.
    check(keyExists(in + progIdKey), "CONTROL: the ProgID key exists under the scratch key");
    check(keyExists(progIdKey) == realBefore,
          "the REAL HKCU's Qinta ProgID is exactly as it was before the run");

    check(RegDeleteTreeW(HKEY_CURRENT_USER, scratch.c_str()) == ERROR_SUCCESS &&
              !keyExists(scratch),
          "the scratch key is deleted again");

    std::cout << "File association: " << failures << " failures\n";
    return failures ? 1 : 0;
}
