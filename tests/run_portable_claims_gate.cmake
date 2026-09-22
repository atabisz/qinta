# Runs scripts/check-portable-claims.sh as a ctest arm, and READS THE PRINTED COUNTERS RATHER
# THAN THE EXIT CODE -- the same reason tests/run_figure_gate.cmake exists, for the same class of
# failure. An exit 0 from that script says "no assertion failed"; it cannot say how many it made,
# and a matcher that stopped matching makes zero assertions and exits 0.
#
# THE GATE HAS ONE STRUCTURAL DEFENCE OF ITS OWN, WHICH IS WORTH KNOWING BEFORE READING THE
# FLOORS BELOW: because every allowlist row must match a live hit, a corpus that suddenly matches
# nothing turns all thirty-odd rows STALE and the arm goes red. So "measured nothing" is already
# loud rather than green. The floor here is a second, cruder reading on top of that -- it catches
# the case where the allowlist is emptied in the same edit that breaks the matcher, which is
# exactly how a self-consistent green gets written.
if(NOT DEFINED BASH_EXE OR NOT DEFINED GATE_SCRIPT OR NOT DEFINED REPO_ROOT
        OR NOT DEFINED EXPECTED_MIN_HITS)
    message(FATAL_ERROR "BASH_EXE, GATE_SCRIPT, REPO_ROOT and EXPECTED_MIN_HITS are required")
endif()

# Passed as a real argument, never inferred from the test name: a wrapper that drops the flag
# reruns the subject and reports a pass for the control, and this repo's own temp rig has sprung
# that trap once already. The CONTROL OK sentence below is what makes a dropped flag impossible
# to miss.
set(gate_args "")
if(GATE_CONTROL)
    set(gate_args --control)
endif()

execute_process(COMMAND "${BASH_EXE}" "${GATE_SCRIPT}" ${gate_args}
    WORKING_DIRECTORY "${REPO_ROOT}"
    OUTPUT_VARIABLE gate_out
    ERROR_VARIABLE gate_err
    RESULT_VARIABLE gate_result)

# Echoed so a red arm's log carries the gate's own FAIL lines with the offending text, not just
# a count. The text IS the finding here -- a line number would not survive to the next commit.
message("${gate_out}${gate_err}")

foreach(counter hits failures checked entries files allow_rows stale)
    if(NOT gate_out MATCHES "portable_claims_${counter}=([0-9]+)")
        message(FATAL_ERROR
            "The gate printed no portable_claims_${counter}= line, so this arm read nothing it "
            "could check. Exit status was ${gate_result}.")
    endif()
    set(v_${counter} "${CMAKE_MATCH_1}")
endforeach()

if(v_hits LESS EXPECTED_MIN_HITS)
    message(FATAL_ERROR
        "The gate found ${v_hits} hits, floor is ${EXPECTED_MIN_HITS}. This tree carries several "
        "dozen correction records and closed quotations that MUST match -- a count below the "
        "floor means the matcher or the corpus broke, not that the tree got cleaner. Fixing a "
        "comment removes a hit AND makes its allowlist row stale, so a legitimate drop shows up "
        "as a stale row to delete rather than as a quiet fall in this number.")
endif()

# ASSERTED SEPARATELY FROM THE EXIT STATUS, and it is the arithmetic that says the two halves of
# the check both ran: every hit is one assertion and every well-formed allowlist row is another,
# so on a clean run the total is exactly their sum. A mismatch means one of the two loops did not
# walk its input -- the failure mode that put an inert three-column loop into this tree's last
# manifest gate and printed a confident "12 targets censused" while asserting nothing.
math(EXPR expected_checked "${v_hits} + ${v_allow_rows}")
if(NOT GATE_CONTROL AND NOT v_checked EQUAL expected_checked)
    message(FATAL_ERROR
        "The gate made ${v_checked} assertions over ${v_hits} hits and ${v_allow_rows} allowlist "
        "rows, expected ${expected_checked}. One of the two assertion loops did not run.")
endif()

if(GATE_CONTROL)
    # A passing control here is failures == hits with the allowlist withheld. Requiring the
    # sentence as well leaves no way for an arm that never received --control to be recorded as a
    # passing control.
    if(NOT gate_out MATCHES "CONTROL OK")
        message(FATAL_ERROR
            "The control arm never printed CONTROL OK, so it did not run as a control. A control "
            "that passes for an unexamined reason is worth less than no control at all.")
    endif()
    if(NOT gate_result EQUAL 0)
        message(FATAL_ERROR "The control arm exited ${gate_result} after printing CONTROL OK.")
    endif()
    return()
endif()

if(NOT v_failures EQUAL 0)
    message(FATAL_ERROR
        "${v_failures} of ${v_checked} portable-claim assertions failed (${v_stale} of them stale "
        "allowlist rows). Each FAIL line above carries the offending text. A comment naming a "
        "tests/portable-sources.txt entry as unportable contradicts the manifest that lists it; a "
        "stale row means the tree was fixed and the excuse was left behind.")
endif()

if(NOT gate_result EQUAL 0)
    message(FATAL_ERROR "The gate reported 0 failures but exited ${gate_result}.")
endif()
