# tests/run_qinta_cli.cmake -- qinta's four harness switches, run as the real binary.
#
#   cmake -DQINTA_EXE=<qinta> -DWORK_DIR=<scratch dir> -P tests/run_qinta_cli.cmake
#
# --printpages, --exporthtml, --exportdocx and --exportpdf were refused by src/main_qt.cpp until
# 2026-10-06. This runs each one against a generated mixed document and reads the RESULT, never
# the exit code alone: a PDF must start with %PDF and carry pages, the .docx must hold the Mermaid
# diagram as an image part, the HTML must hold the heading, and --printpages must write PNGs.
# A switch that exited 0 having written nothing is the failure this exists to catch.
#
# Two controls. The OUTPUT GUARD: naming an existing markdown file as the PDF target must exit 2
# and leave that file byte-identical, which is the `tinta --exportpdf README.md` accident this
# binary can now repeat. And NO DOCUMENT: a missing input must make the exporter exit 1 and write
# nothing, so exit 0 above means "wrote a document", not "this binary always exits 0".
#
# QT_QPA_PLATFORM comes from the test's ENVIRONMENT, set per platform in CMakeLists.txt, and on
# Windows the Qt bin directory is prepended to PATH there too. Without it the loader puts a modal
# "Qt6Core.dll was not found" box on the desktop and this waits for a person.

if(NOT QINTA_EXE OR NOT WORK_DIR)
    message(FATAL_ERROR "run_qinta_cli.cmake needs -DQINTA_EXE and -DWORK_DIR")
endif()

set(checks 0)
set(failures 0)
# check(<description> <condition...>): the condition comes last and UNQUOTED, so it expands into
# if()'s own arguments. A first version took it as one quoted string, and if("rc EQUAL 0 AND ...")
# tests a non-empty string, not a condition: all six checks read FAIL beside readings that were
# right, which is how it was caught.
macro(check what)
    math(EXPR checks "${checks} + 1")
    if(${ARGN})
        message(STATUS "ok   : ${what}")
    else()
        math(EXPR failures "${failures} + 1")
        message(STATUS "FAIL : ${what}")
    endif()
endmacro()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
set(doc "${WORK_DIR}/doc.md")
file(WRITE "${doc}" "# qinta harness\n\nSome **bold** prose, `code`, and a [link](https://example.com).\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n```mermaid\ngraph TD\n  A --> B\n```\n\n```cpp\nint main() { return 0; }\n```\n")

function(run_qinta out_rc out_err)
    execute_process(COMMAND "${QINTA_EXE}" ${ARGN}
                    RESULT_VARIABLE rc ERROR_VARIABLE err OUTPUT_VARIABLE out
                    TIMEOUT 120)
    set(${out_rc} "${rc}" PARENT_SCOPE)
    set(${out_err} "${err}${out}" PARENT_SCOPE)
endfunction()

# --- --exportpdf -------------------------------------------------------------------------
set(pdf "${WORK_DIR}/out.pdf")
run_qinta(rc err --exportpdf "${pdf}" "${doc}")
set(pdf_head "")
set(pdf_pages 0)
if(EXISTS "${pdf}")
    # HEX, not text: a text read of these binary heads came back with a trailing line break,
    # so STREQUAL against "%PDF-" failed on a correct file.
    file(READ "${pdf}" pdf_head LIMIT 5 HEX)
    file(STRINGS "${pdf}" page_lines REGEX "/Type /Page[^s]|/Type /Page$")
    list(LENGTH page_lines pdf_pages)
endif()
message(STATUS "exportpdf rc=${rc} head=${pdf_head} pages=${pdf_pages}")
check("--exportpdf writes a PDF with pages and exits 0"
      rc EQUAL 0 AND pdf_head STREQUAL "255044462d" AND pdf_pages GREATER 0)

# --- --exporthtml ------------------------------------------------------------------------
set(html "${WORK_DIR}/out.html")
run_qinta(rc err --exporthtml "${html}" "${doc}")
set(html_text "")
if(EXISTS "${html}")
    file(READ "${html}" html_text)
endif()
string(FIND "${html_text}" "qinta harness" html_heading)
string(FIND "${html_text}" "<table" html_table)
message(STATUS "exporthtml rc=${rc} heading_at=${html_heading} table_at=${html_table}")
check("--exporthtml writes the heading and the table and exits 0"
      rc EQUAL 0 AND html_heading GREATER -1 AND html_table GREATER -1)

# --- --exportdocx ------------------------------------------------------------------------
set(docx "${WORK_DIR}/out.docx")
run_qinta(rc err --exportdocx "${docx}" "${doc}")
set(docx_head "")
set(docx_media -1)
if(EXISTS "${docx}")
    file(READ "${docx}" docx_head LIMIT 2 HEX)
    # Zip stores member names uncompressed, so the image part's NAME is in the bytes.
    file(STRINGS "${docx}" media_lines REGEX "word/media/")
    list(LENGTH media_lines docx_media)
endif()
message(STATUS "exportdocx rc=${rc} head=${docx_head} media_lines=${docx_media}")
check("--exportdocx writes a zip with the Mermaid diagram as an image part and exits 0"
      rc EQUAL 0 AND docx_head STREQUAL "504b" AND docx_media GREATER 0)

# --- --printpages ------------------------------------------------------------------------
set(pages_dir "${WORK_DIR}/pages")
run_qinta(rc err --printpages "${pages_dir}" "${doc}")
file(GLOB pngs "${pages_dir}/page-*.png")
list(LENGTH pngs png_count)
set(png_sig "")
if(EXISTS "${pages_dir}/page-1.png")
    file(READ "${pages_dir}/page-1.png" png_sig LIMIT 4 HEX)
endif()
message(STATUS "printpages rc=${rc} pngs=${png_count} sig=${png_sig}")
check("--printpages writes PNG pages and exits 0"
      rc EQUAL 0 AND png_count GREATER 0 AND png_sig STREQUAL "89504e47")

# --- CONTROL: the output guard -----------------------------------------------------------
set(victim "${WORK_DIR}/victim.md")
file(WRITE "${victim}" "# A document that must survive\n")
file(SHA256 "${victim}" victim_before)
run_qinta(rc err --exportpdf "${victim}" "${doc}")
file(SHA256 "${victim}" victim_after)
string(FIND "${err}" "refusing to write" refused_at)
message(STATUS "guard rc=${rc} refused_at=${refused_at} unchanged=${victim_before}")
check("CONTROL: --exportpdf onto an existing .md exits 2, says so, and leaves the file intact"
      rc EQUAL 2 AND refused_at GREATER -1 AND victim_before STREQUAL victim_after)

# --- CONTROL: no document ----------------------------------------------------------------
set(nodoc_pdf "${WORK_DIR}/nodoc.pdf")
run_qinta(rc err --exportpdf "${nodoc_pdf}" "${WORK_DIR}/missing.md")
if(EXISTS "${nodoc_pdf}")
    set(nodoc_written 1)
else()
    set(nodoc_written 0)
endif()
message(STATUS "nodoc rc=${rc} written=${nodoc_written}")
check("CONTROL: with no document the exporter exits 1 and writes nothing"
      rc EQUAL 1 AND nodoc_written EQUAL 0)

# --- A NON-ASCII PATH, in the document AND the output (2026-10-14) -------------------------
# main() used to parse its narrow argv, which on Windows is in the ANSI code page: on a cp1252
# host `café` arrived as the byte 0xE9 and the ideographs as `??`, and loadDocumentFromPath reads
# a std::string as UTF-8, so neither opened -- exit 1, no pages, against an ASCII control that
# printed. One name of each kind, because they fail differently: é exists in cp1252 and the
# ideographs do not. The output directory carries one too, since the four harness paths travel
# the same command line. Off Windows argv was already UTF-8, so there this is a control.
set(intl_dir "${WORK_DIR}/über")
file(MAKE_DIRECTORY "${intl_dir}")
foreach(name "café" "笔记")
    file(COPY_FILE "${doc}" "${intl_dir}/${name}.md")
    set(intl_pages "${intl_dir}/pages-${name}")
    run_qinta(rc err --printpages "${intl_pages}" "${intl_dir}/${name}.md")
    file(GLOB intl_pngs "${intl_pages}/page-*.png")
    list(LENGTH intl_pngs intl_count)
    message(STATUS "non-ascii '${name}' rc=${rc} pngs=${intl_count}")
    check("--printpages opens '${name}.md' under 'über' and writes its pages there"
          rc EQUAL 0 AND intl_count GREATER 0 AND intl_count EQUAL png_count)
endforeach()

message(STATUS "qinta_cli checks=${checks} failures=${failures}")
if(NOT failures EQUAL 0)
    message(FATAL_ERROR "qinta_cli: ${failures} of ${checks} checks failed")
endif()
