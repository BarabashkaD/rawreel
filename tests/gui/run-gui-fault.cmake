# rawreel-gui --selftest smoke with an injected fault (RAWREEL_GUI_FAULT, set by the test's
# ENVIRONMENT): the run must exit 4 promptly with the expected message, without a leak report.
# Needs -DGUI=<rawreel-gui> -DEXPECT=<regex of the stderr message>.
if(NOT GUI OR NOT EXPECT)
    message(FATAL_ERROR "run with -DGUI=<rawreel-gui> -DEXPECT=<regex>")
endif()
execute_process(COMMAND "${GUI}" --selftest smoke
                RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(code EQUAL 77)
    message(STATUS "skipped: ${err}")
    cmake_language(EXIT 77)
endif()
if(err MATCHES "managed object\\(s\\) still alive at exit")
    message(FATAL_ERROR "leak report:\n${err}")
endif()
if(err MATCHES "X Window System error|BadWindow|BadDrawable|BadMatch")  # X errors (Linux)
    message(FATAL_ERROR "X error:\n${err}")
endif()
if(NOT code STREQUAL "4")
    message(FATAL_ERROR "exit ${code}, expected 4\nstdout:\n${out}\nstderr:\n${err}")
endif()
if(NOT err MATCHES "${EXPECT}")
    message(FATAL_ERROR "'${EXPECT}' not found in:\n${err}")
endif()
