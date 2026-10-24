# Included by the run-cli-*.cmake scripts. Needs -DCLI=<rawreel-cli> and -DWORK_DIR=<dir>.
if(NOT CLI OR NOT WORK_DIR)
    message(FATAL_ERROR "run with -DCLI=<rawreel-cli> -DWORK_DIR=<scratch dir>")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

# run_cli(<expected exit code> <stdout var> <stderr var> <arguments>...): runs the CLI in
# WORK_DIR and fails unless it exits with the expected code. A leak report fails every run.
function(run_cli expected out_var err_var)
    execute_process(COMMAND "${CLI}" ${ARGN} WORKING_DIRECTORY "${WORK_DIR}"
                    RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(err MATCHES "managed object\\(s\\) still alive at exit")
        message(FATAL_ERROR "rawreel-cli ${ARGN}: leak report\n${err}")
    endif()
    if(NOT code STREQUAL "${expected}")
        message(FATAL_ERROR "rawreel-cli ${ARGN}: exit ${code}, expected ${expected}\n"
                            "stdout:\n${out}\nstderr:\n${err}")
    endif()
    set(${out_var} "${out}" PARENT_SCOPE)
    set(${err_var} "${err}" PARENT_SCOPE)
endfunction()

# converter_runs(<json> <name> <out var>): the runs of one converter in dump --json (0 if absent).
function(converter_runs json name out_var)
    string(JSON count LENGTH "${json}" converters)
    set(runs 0)
    if(count GREATER 0)
        math(EXPR last "${count} - 1")
        foreach(i RANGE ${last})
            string(JSON entry_name GET "${json}" converters ${i} name)
            if(entry_name STREQUAL name)
                string(JSON runs GET "${json}" converters ${i} runs)
            endif()
        endforeach()
    endif()
    set(${out_var} ${runs} PARENT_SCOPE)
endfunction()

# expect_no_validation(<stderr> <what>): fails if the CLI logged a counted Vulkan debug message.
# dvv-gpu logs them as "Vulkan <types> warning|error [<id>]: ..." (VUID and SYNC-HAZARD ids alike).
function(expect_no_validation err what)
    if(err MATCHES "Vulkan [a-z|-]+ (warning|error)")
        message(FATAL_ERROR "validation message for ${what}:\n${err}")
    endif()
endfunction()

function(expect_match text regex what)
    if(NOT text MATCHES "${regex}")
        message(FATAL_ERROR "${what}: '${regex}' not found in:\n${text}")
    endif()
endfunction()

# expect_json(<json> <expected> <key>...): fails unless the member at <key>... equals <expected>.
function(expect_json json expected)
    string(JSON value GET "${json}" ${ARGN})
    if(NOT value STREQUAL "${expected}")
        message(FATAL_ERROR "JSON ${ARGN}: '${value}', expected '${expected}' in:\n${json}")
    endif()
endfunction()

# expect_file(<file> <expected content>): fails unless WORK_DIR/<file> holds exactly the content.
function(expect_file file expected)
    file(READ "${WORK_DIR}/${file}" content)
    if(NOT content STREQUAL "${expected}")
        message(FATAL_ERROR "${file}: '${content}', expected '${expected}'")
    endif()
endfunction()
