// include/platform_process.h and include/win32_command_line.h -- the process seam, and
// the Win32 command-line quoting convention that only one of its two arms needs.
//
// THIS TEST SPAWNS ITSELF, and that is the design decision worth reading first. The
// property the seam promises is that each argument arrives at the child as exactly ONE
// argv entry whatever it contains, and there is no way to check that by inspecting a
// string: on Windows the string is parsed back by the child's own CRT startup code, and
// on POSIX there is no string at all. So the parent runs this binary again with
// --dump-args, the child writes its own argv to a file, and the parent compares. The
// instrument is the operating system, not a second copy of my algorithm.
//
// That also makes the check identical on both platforms, which is the point of a seam:
// the same nine arguments -- including one containing a double quote, one with a
// trailing backslash, one with a backslash run before a quote, and one that is EMPTY --
// have to survive the Win32 quoter on Windows and QProcess's argument vector off it.
//
// THE EMPTY ARGUMENT IS THE ONE THAT WOULD SLIP THROUGH AN EYEBALL. An unquoted empty
// argument does not arrive wrong, it VANISHES, shifting every later argument down one
// position -- so the list below keeps a named argument AFTER the empty one, and the
// comparison is positional. A test that ended on the empty string could not tell the
// difference between "arrived empty" and "was never there".
//
// WHAT IS ALSO PINNED AGAINST LITERALS, and why a round trip is not enough on its own:
// a quoter that is wrong in a way the parser undoes round-trips perfectly. So
// win32CommandLine's output is asserted against expected STRINGS as well, including the
// exact shape the hand-built command line in src/pandoc.cpp used to produce. That is
// the failure memory/a-round-trip-cannot-test-a-defence-it-never-triggers is named for.
//
// THE BYTES ARE NOT THE OLD BYTES, stated here rather than discovered in a diff. The
// code this replaced quoted the exe, the title value, the resource path, the output
// path and the temp file UNCONDITIONALLY while never quoting -f, gfm, -s, --metadata or
// -o. No principled rule reproduces that split, so what is preserved is the child's
// ARGV. argv[0] alone stays byte-identical, because the exe token is quoted
// unconditionally here too.
//
// POSITIVE CONTROL: `--control` perturbs every EXPECTED value, so every comparison must
// fail. The survivors must be exactly the checks that were never perturbed -- contracts
// about process behaviour, which have no expectation to corrupt -- and the threshold is
// that counter rather than a hand-written number.

#include "platform_process.h"

#include "win32_command_line.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#endif

namespace {

int failures = 0;
int checks = 0;
bool control = false;

// Perturbs the EXPECTATION, not the input -- the same arguments go to the same child in
// both modes, so a control failure means the assertion is anchored to a specific result
// rather than to the fact that something ran.
std::wstring expect(const std::wstring& value) {
    return control ? value + L"\x1" : value;
}

void eq(const std::wstring& actual, const std::wstring& wanted, const char* name) {
    checks++;
    if (actual == wanted) {
        std::cout << "  ok   " << name << "\n";
        return;
    }
    failures++;
    std::cout << "  FAIL " << name << "\n";
}

// Something that must hold in both modes, so there is nothing for --control to
// perturb. Counted separately because a control whose threshold is "roughly this many
// should survive" cannot tell a loose assertion from a missing one.
int unperturbed = 0;

void contract(bool cond, const char* name) {
    checks++;
    unperturbed++;
    if (cond) {
        std::cout << "  ok   " << name << "  (contract, not perturbed)\n";
        return;
    }
    failures++;
    std::cout << "  FAIL " << name << "  (contract, not perturbed)\n";
}

// --- the child modes -----------------------------------------------------------------
//
// Length-prefixed rather than newline-separated, because an argument is allowed to
// contain a newline and a format that cannot represent one would be asserting the
// absence of the case it fails on.
int childDumpArgs(const char* out, int argc, char** argv, int first) {
    std::ofstream file(std::filesystem::path(out), std::ios::binary);
    if (!file) return 2;
    for (int i = first; i < argc; ++i) {
        const std::string arg = argv[i];
        file << arg.size() << ":" << arg << "\n";
    }
    return file ? 0 : 2;
}

std::vector<std::string> readDumpedArgs(const std::filesystem::path& path, bool* ok) {
    std::vector<std::string> out;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *ok = false;
        return out;
    }
    std::string blob((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    size_t i = 0;
    while (i < blob.size()) {
        const size_t colon = blob.find(':', i);
        if (colon == std::string::npos) break;
        const size_t len = (size_t)std::strtoul(blob.substr(i, colon - i).c_str(), nullptr, 10);
        if (colon + 1 + len > blob.size()) break;
        out.push_back(blob.substr(colon + 1, len));
        i = colon + 1 + len + 1;  // + the trailing newline
    }
    *ok = true;
    return out;
}

std::wstring widen(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (unsigned char ch : s) out += (wchar_t)ch;
    return out;
}

// --- the quoter, pinned against literals ---------------------------------------------
void testQuoterLiterals() {
    std::cout << "-- win32QuoteArgument: the rule, against expected strings\n";
    using tinta::win32QuoteArgument;

    eq(win32QuoteArgument(L"plain"), expect(L"plain"),
       "nothing to quote is left completely alone");
    eq(win32QuoteArgument(L"-f"), expect(L"-f"),
       "a flag is NOT quoted -- the old hand-built line did not quote it either");
    eq(win32QuoteArgument(L"has space"), expect(L"\"has space\""),
       "a space forces quotes");
    eq(win32QuoteArgument(L"has\ttab"), expect(L"\"has\ttab\""),
       "a tab forces quotes -- CommandLineToArgvW splits on it too");
    eq(win32QuoteArgument(L""), expect(L"\"\""),
       "THE EMPTY ARGUMENT becomes \"\", the only spelling that does not vanish");
    eq(win32QuoteArgument(L"C:\\pandoc\\pandoc.exe"),
       expect(L"C:\\pandoc\\pandoc.exe"),
       "a path with no space is NOT quoted -- where this differs from the old bytes");
    eq(win32QuoteArgument(L"say \"hi\""), expect(L"\"say \\\"hi\\\"\""),
       "an embedded quote is backslash-escaped, not rewritten");
    eq(win32QuoteArgument(L"C:\\dir with space\\"),
       expect(L"\"C:\\dir with space\\\\\""),
       "A TRAILING BACKSLASH IS DOUBLED so it cannot escape the closing quote");
    eq(win32QuoteArgument(L"a\\\\\"b"), expect(L"\"a\\\\\\\\\\\"b\""),
       "a backslash RUN before a quote doubles, then the quote is escaped");
    eq(win32QuoteArgument(L"trailing\\"), expect(L"trailing\\"),
       "a trailing backslash with nothing else needing quotes stays bare");
}

void testCommandLineLiterals() {
    std::cout << "-- win32CommandLine: the assembled string\n";
    using tinta::win32CommandLine;

    eq(win32CommandLine(L"C:\\Pandoc\\pandoc.exe", {}),
       expect(L"\"C:\\Pandoc\\pandoc.exe\""),
       "THE EXE TOKEN IS QUOTED UNCONDITIONALLY -- argv[0] byte-identical to the old line");

    // THE SHAPE src/pandoc.cpp ACTUALLY PRODUCES, pinned whole. This is the literal the
    // round trip below cannot replace: a quoter wrong in a way CommandLineToArgvW undoes
    // would satisfy the round trip and fail here.
    eq(win32CommandLine(L"C:\\Program Files\\Pandoc\\pandoc.exe",
                        {L"-f", L"html", L"-s", L"--metadata", L"title=My Doc", L"-o",
                         L"C:\\out\\book.epub", L"C:\\Temp\\tinta-pandoc-4242.html"}),
       expect(L"\"C:\\Program Files\\Pandoc\\pandoc.exe\" -f html -s --metadata "
              L"\"title=My Doc\" -o C:\\out\\book.epub "
              L"C:\\Temp\\tinta-pandoc-4242.html"),
       "the real export shape: flags bare, the title grouped, paths bare when spaceless");

    // ISC-6's divergence, as a string. The old code rewrote the quote to an apostrophe
    // before concatenating; nothing here does, and the escape is what replaces it.
    eq(win32CommandLine(L"pandoc.exe", {L"--metadata", L"title=The \"Real\" Thing"}),
       expect(L"\"pandoc.exe\" --metadata \"title=The \\\"Real\\\" Thing\""),
       "A TITLE'S DOUBLE QUOTE IS ESCAPED, where it used to be rewritten to an apostrophe");

    eq(win32CommandLine(L"pandoc.exe", {L"-o", L"", L"after"}),
       expect(L"\"pandoc.exe\" -o \"\" after"),
       "an empty argument occupies its position instead of disappearing");
}

#ifdef _WIN32
// --- the OS's own parser -------------------------------------------------------------
//
// CommandLineToArgvW rather than a hand-written parser here, deliberately: a parser I
// wrote would share whatever misreading of the rule the quoter has, and the two would
// agree while both being wrong. This is shell32's, the same code path the CRT's startup
// parsing implements for the child.
void testArgvRoundTripWin32() {
    std::cout << "-- CommandLineToArgvW round trip (the OS's parser, not a second copy)\n";
    const std::vector<std::wstring> inputs = {
        L"-f",
        L"gfm",
        L"-s",
        L"--metadata",
        L"title=The \"Real\" Thing",
        L"--resource-path=C:\\My Docs\\sub",
        L"C:\\trailing\\",
        L"back\\\\\"slash",
        L"",
        L"after-empty",
    };
    const std::wstring exe = L"C:\\Program Files\\Pandoc\\pandoc.exe";
    const std::wstring line = tinta::win32CommandLine(exe, inputs);

    int count = 0;
    LPWSTR* parsed = CommandLineToArgvW(line.c_str(), &count);
    if (!parsed) {
        checks++;
        failures++;
        std::cout << "  FAIL CommandLineToArgvW refused the line -- nothing measured\n";
        return;
    }

    contract(count == (int)inputs.size() + 1, "argv count is exe + every argument");
    eq(count > 0 ? std::wstring(parsed[0]) : std::wstring(), expect(exe),
       "argv[0] is byte-identical to the exe that went in");
    for (size_t i = 0; i < inputs.size(); ++i) {
        const std::wstring got =
            (int)i + 1 < count ? std::wstring(parsed[i + 1]) : std::wstring(L"<missing>");
        eq(got, expect(inputs[i]), "argv entry survives the quoter unchanged");
    }
    LocalFree(parsed);
}
#endif

// --- the seam, end to end ------------------------------------------------------------
void testArgvThroughTheSeam(const std::filesystem::path& self) {
    std::cout << "-- the seam end to end: this binary re-runs itself and dumps its argv\n";

    std::error_code ec;
    const std::filesystem::path dump =
        std::filesystem::temp_directory_path(ec) /
        (L"tinta-process-argv-" + std::to_wstring(platform::currentProcessId()) + L".txt");
    std::filesystem::remove(dump, ec);

    // The same nine as the Win32 round trip, so a divergence between the two instruments
    // is visible rather than hidden behind different data.
    const std::vector<std::wstring> payload = {
        L"-f",
        L"gfm",
        L"-s",
        L"--metadata",
        L"title=The \"Real\" Thing",
        L"--resource-path=C:\\My Docs\\sub",
        L"C:\\trailing\\",
        L"back\\\\\"slash",
        L"",
        L"after-empty",
    };

    std::vector<std::wstring> args = {L"--dump-args", dump.wstring()};
    for (const std::wstring& arg : payload) args.push_back(arg);

    const platform::ProcessResult run =
        platform::runProcessAndWait(self.wstring(), args, 30000);
    contract(run.launched, "the child launched");
    contract(!run.timedOut, "the child finished inside the budget");
    contract(run.exitCode == 0, "the child reported success writing its argv");

    bool readOk = false;
    const std::vector<std::string> got = readDumpedArgs(dump, &readOk);
    contract(readOk, "the argv dump file exists and was readable");
    contract(got.size() == payload.size(),
             "the child received EXACTLY as many arguments as were sent");

    for (size_t i = 0; i < payload.size(); ++i) {
        const std::wstring actual = i < got.size() ? widen(got[i]) : std::wstring(L"<missing>");
        eq(actual, expect(payload[i]),
           i == 8 ? "the EMPTY argument arrived as an empty argument, in its own position"
                  : "the argument reached the child as one argv entry, unchanged");
    }

    // Named explicitly so a later reader cannot mistake the change for a regression:
    // this string used to reach pandoc as title=The 'Real' Thing.
    const std::wstring title = got.size() > 4 ? widen(got[4]) : std::wstring();
    contract(title.find(L'"') != std::wstring::npos && title.find(L'\'') == std::wstring::npos,
             "THE TITLE CARRIES ITS DOUBLE QUOTES -- it used to arrive as 'Real'");

    std::filesystem::remove(dump, ec);
}

void testExitCode(const std::filesystem::path& self) {
    std::cout << "-- exit codes and the launch refusal\n";

    const platform::ProcessResult zero =
        platform::runProcessAndWait(self.wstring(), {L"--child-exit", L"0"}, 30000);
    contract(zero.launched && !zero.timedOut && zero.exitCode == 0,
             "a child exiting 0 reads as launched, not timed out, exitCode 0");

    const platform::ProcessResult three =
        platform::runProcessAndWait(self.wstring(), {L"--child-exit", L"3"}, 30000);
    contract(three.launched && !three.timedOut, "a child exiting 3 still launched and finished");
    contract(three.exitCode == 3, "the child's own exit code comes back, not a boolean");

    // launched false, NOT a non-zero exit code: the caller in src/pandoc.cpp still owns
    // the temp file it staged when nothing ran, and that distinction is the whole reason
    // ProcessResult has three fields.
    const platform::ProcessResult missing = platform::runProcessAndWait(
        (self.parent_path() / L"tinta-no-such-executable-4a91").wstring(), {}, 5000);
    contract(!missing.launched, "a missing executable reports launched=false");
}

void testTimeout(const std::filesystem::path& self) {
    std::cout << "-- the timeout path: a child that outlives its budget\n";

    const auto started = std::chrono::steady_clock::now();
    const platform::ProcessResult run =
        platform::runProcessAndWait(self.wstring(), {L"--child-sleep", L"20000"}, 1000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();

    contract(run.launched, "the slow child did launch");
    contract(run.timedOut, "the overrun is reported as timedOut, not as a non-zero exit");

    // The threshold is the CHILD'S sleep, not the budget: a seam that simply waited for
    // the child would also return timedOut=false and this assertion is what separates
    // "we killed it" from "it happened to be quick". 20s asked for, and the Qt arm may
    // spend up to two budgets before it gives up, so 10s is comfortably between them.
    contract(elapsed < 10000,
             "the call returned long before the child's own 20s -- it was terminated");
    std::cout << "     (returned in " << elapsed << "ms against a 1000ms budget)\n";
}

void testProcessId() {
    std::cout << "-- currentProcessId\n";
    const unsigned long first = platform::currentProcessId();
    contract(first != 0, "the process id is not zero");
    contract(platform::currentProcessId() == first,
             "it is stable within one process -- a temp file name can rely on it");
}

}  // namespace

int main(int argc, char** argv) {
    // THE CHILD MODES COME FIRST and print nothing, because the parent parses this
    // binary's side effects rather than its output.
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--child-exit" && i + 1 < argc) {
            return std::atoi(argv[i + 1]);
        }
        if (arg == "--child-sleep" && i + 1 < argc) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::atoi(argv[i + 1])));
            return 0;
        }
        if (arg == "--dump-args" && i + 1 < argc) {
            return childDumpArgs(argv[i + 1], argc, argv, i + 2);
        }
        if (arg == "--control") control = true;
    }

    // REFUSES RATHER THAN SKIPS, because a test that cannot find itself would otherwise
    // report a green run having measured only the pure string logic -- and the string
    // logic is the half that needs the OS least.
    const std::filesystem::path self = std::filesystem::absolute(argv[0]);
    if (!std::filesystem::is_regular_file(self)) {
        std::cout << "REFUSED: cannot locate this binary to re-run (argv[0]=" << argv[0]
                  << " resolved to " << self.string() << ")\n";
        return 2;
    }

    std::cout << "platform_process / win32_command_line tests"
              << (control ? "  [CONTROL: expectations perturbed]" : "") << "\n";
    std::cout << "self=" << self.string() << "\n\n";

    testQuoterLiterals();
    testCommandLineLiterals();
#ifdef _WIN32
    testArgvRoundTripWin32();
#else
    std::cout << "-- CommandLineToArgvW round trip: SKIPPED, no Win32 parser here.\n"
                 "     The seam test below covers the same nine arguments through the\n"
                 "     real OS, which is the stronger check of the two.\n";
#endif
    testArgvThroughTheSeam(self);
    testExitCode(self);
    testTimeout(self);
    testProcessId();

    std::cout << "\nprocess_failures=" << failures << "\n";
    std::cout << "process_checks=" << checks << "\n";

    if (control) {
        const int survived = checks - failures;
        std::cout << "CONTROL: perturbed_failed=" << failures << " survived=" << survived
                  << " unperturbed_contracts=" << unperturbed << "\n";
        if (survived != unperturbed) {
            std::cout << "CONTROL FAILED: " << (survived - unperturbed)
                      << " perturbed check(s) still passed -- those assertions are not"
                         " anchored to their own result\n";
            return 1;
        }
        std::cout << "CONTROL PASSED: all " << failures
                  << " perturbed expectations missed\n";
        return 0;
    }

    if (failures == 0) std::cout << "All " << checks << " process checks passed.\n";
    return failures == 0 ? 0 : 1;
}
