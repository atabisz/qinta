# Run a test binary in portable mode: copy it to a scratch directory and put a
# settings.ini beside it, which is the opt-in tintaConfigDir() looks for (#147).
# The point is isolation -- the test then reads and writes ITS OWN config, never
# the user's real one in %APPDATA%\Tinta.
if(NOT DEFINED TEST_BINARY OR NOT DEFINED TEST_DIR)
    message(FATAL_ERROR "TEST_BINARY and TEST_DIR are required")
endif()
# The copy keeps the original filename rather than a hardcoded one, so more than
# one target can share this script. It must be a copy and not the built binary in
# place: portable mode keys off the *executable's* directory, and the build tree
# is not somewhere to be dropping a settings.ini.
get_filename_component(TEST_EXE_NAME "${TEST_BINARY}" NAME)
file(MAKE_DIRECTORY "${TEST_DIR}")
configure_file("${TEST_BINARY}" "${TEST_DIR}/${TEST_EXE_NAME}" COPYONLY)
# Do not reword this line. tests/theme_tests.cpp compares the file's contents
# against it byte for byte and refuses to run on a mismatch, which is how it knows
# it was launched by this script and not against a real configuration. Changing
# the text here makes that test fail with exit 2 and a message that points at the
# wrapper rather than at the edit.
file(WRITE "${TEST_DIR}/settings.ini" "; Isolated native theme test configuration\n")
execute_process(COMMAND "${TEST_DIR}/${TEST_EXE_NAME}" --portable-test
    WORKING_DIRECTORY "${TEST_DIR}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Portable test ${TEST_EXE_NAME} failed: ${result}")
endif()
