# Runs scripts/check-readme-figures.sh as a ctest arm, and READS THE PRINTED LINE
# RATHER THAN THE EXIT CODE. That is the entire reason this driver exists instead of a
# bare `add_test(COMMAND bash …)`.
#
# The gate's exit status is its failure count, so exit 0 does mean "every claim it
# CHECKED was found". What exit 0 cannot say is how many it checked. One of the claims sits
# inside a `command -v rg` guard (grep for `command -v rg` in
# scripts/check-readme-figures.sh -- de-numbered because that citation had already rotted
# once) and SKIPS when ripgrep is not on PATH, because its pattern needs PCRE2. On such a
# machine the subject arm checks one claim fewer and exits 0, and the control arm -- whose
# test is `failures == checks` -- prints CONTROL PASSED at that lower count. Both arms read
# green having silently stopped asserting a published figure. So EXPECTED_CHECKS is a
# second, independent reading, and a run that does not make the expected number of
# assertions is a failure even when the script is perfectly happy. The numbers are
# deliberately not written into this comment: EXPECTED_CHECKS lives in CMakeLists.txt, which
# is the one copy, and a count repeated here would be a second copy nothing checks.
#
# The count is checked BEFORE the exit status on purpose: a run that both skipped claims
# and found a stale figure should report the skip, because the skip is the reason the
# remaining reading cannot be trusted.
if(NOT DEFINED BASH_EXE OR NOT DEFINED GATE_SCRIPT OR NOT DEFINED REPO_ROOT
        OR NOT DEFINED EXPECTED_CHECKS)
    message(FATAL_ERROR "BASH_EXE, GATE_SCRIPT, REPO_ROOT and EXPECTED_CHECKS are required")
endif()

# GATE_CONTROL selects the positive-control arm. It is passed as a real argument rather
# than inferred from the test name, because a wrapper that silently drops the flag would
# rerun the subject and report a pass for the control -- a trap this repo's own temp rig
# has already sprung once. The CONTROL PASSED assertion below is what makes dropping it
# impossible to miss: that string cannot be printed by a run that never saw --control.
set(gate_args "")
if(GATE_CONTROL)
    set(gate_args --control)
endif()

# The gate must run from the repo root -- it reads docs/port.md and src/ by relative path
# and exits 90 if it cannot find docs/port.md, which is a distinguishable failure rather
# than an empty pass.
execute_process(COMMAND "${BASH_EXE}" "${GATE_SCRIPT}" ${gate_args}
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE gate_out
    ERROR_VARIABLE gate_err
    RESULT_VARIABLE gate_result)

# Echoed so a failing arm's log carries the gate's own per-claim ok/MISS lines and the
# derived value it printed beside each miss. Without this a red arm would say only that
# something did not match.
message("${gate_out}${gate_err}")

if(NOT gate_out MATCHES "readme_figures_checked=([0-9]+)")
    message(FATAL_ERROR
        "The gate printed no readme_figures_checked= line, so the number of claims it "
        "asserted is unknown. Exit status was ${gate_result}.")
endif()
set(actual_checks "${CMAKE_MATCH_1}")

if(NOT actual_checks EQUAL EXPECTED_CHECKS)
    message(FATAL_ERROR
        "The gate asserted ${actual_checks} claims, expected ${EXPECTED_CHECKS}. A count "
        "BELOW the expectation means claims were skipped -- most likely ripgrep missing "
        "from PATH, which drops the render.cpp mention-trajectory claim. A count ABOVE it "
        "means claims were added to the script and EXPECTED_CHECKS in CMakeLists.txt was "
        "not moved with them. Either way the exit status (${gate_result}) is not the "
        "reading it appears to be.")
endif()

# The tests/test-platforms.txt fidelity checks are a SECOND, independently read family, for the
# same reason EXPECTED_CHECKS exists: they are claims about what the BUILD does rather than about
# what README.md says, they are not perturbable by --control, and they live in their own counters so
# the control arm's `failures == checks` invariant stays satisfiable. Read the line, not the exit
# status: the gate does fold these into its exit code, but that fold is for a human at a prompt --
# here the count is what distinguishes 0-failures-from-3-readings from 0-failures-from-0-readings.
#
# A SKIP IS REPORTED, NEVER TREATED AS A PASS, AND NEVER AS A FAILURE EITHER. Off Windows there is
# no *.vcxproj, so two of the three checks cannot be measured at all and skipping is the correct and
# expected outcome -- this arm runs on both hosts. What must not happen is a skip being counted as
# agreement, which is why the number is echoed here rather than left inside the script.
if(NOT gate_out MATCHES "platform_manifest_failures=([0-9]+)")
    message(FATAL_ERROR
        "The gate printed no platform_manifest_failures= line, so whether tests/test-platforms.txt "
        "still matches the build is unknown. Exit status was ${gate_result}.")
endif()
set(platform_failures "${CMAKE_MATCH_1}")

if(NOT gate_out MATCHES "platform_manifest_skips=([0-9]+)")
    message(FATAL_ERROR "The gate printed no platform_manifest_skips= line.")
endif()
set(platform_skips "${CMAKE_MATCH_1}")

if(NOT platform_failures EQUAL 0)
    message(FATAL_ERROR
        "${platform_failures} tests/test-platforms.txt fidelity check(s) failed. Each FAIL line "
        "above names the rows: either the manifest's platform sets no longer match the generated "
        "ctest list, or a windows-only row's reason is not true of what that target compiles. That "
        "manifest is the denominator of README bases D and E, so a wrong row makes both figures "
        "wrong while the gate reports them as matching the README.")
endif()

if(platform_skips GREATER 0)
    message("NOTE: ${platform_skips} tests/test-platforms.txt check(s) SKIPPED -- see the SKIP "
            "lines above. Expected off Windows, where there is no *.vcxproj to census. A skip is "
            "not a pass: on this run those rows were not read.")
endif()

if(NOT gate_result EQUAL 0)
    message(FATAL_ERROR
        "${gate_result} claim(s) failed -- derived README claims not found, plus any "
        "tests/test-platforms.txt failures folded in above. Each MISS prints the value derived "
        "from the tree; the README says something else.")
endif()

# Exit 0 from --control means the script's own failures == checks test held. Requiring the
# sentence as well leaves no way for an arm that never received --control, or that exited 0
# down some other path, to be recorded as a passing control.
if(GATE_CONTROL AND NOT gate_out MATCHES "CONTROL PASSED")
    message(FATAL_ERROR
        "The control arm exited 0 but never printed CONTROL PASSED, so it did not run as a "
        "control. A control that passes for an unexamined reason is worth less than no "
        "control at all.")
endif()
