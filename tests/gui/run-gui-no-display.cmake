# rawreel-gui's start-up without a usable X11 display (Linux). DISPLAY and
# WAYLAND_DISPLAY are unset for every run, so GTK never opens a display. Needs -DGUI=<rawreel-gui>.
#   --selftest smoke, no DISPLAY          -> 77 (a skip) and the "no X11 display" message
#   the same with DVV_REQUIRE_GPU=1       -> 4 and the same message
#   --selftest smoke, DISPLAY=:987        -> 77 and the "cannot open X display" message
#   --help, no DISPLAY                    -> 0 and the usage text
if(NOT GUI)
    message(FATAL_ERROR "run with -DGUI=<rawreel-gui>")
endif()

set(no_display "no X11 display \\(DISPLAY is not set\\); rawreel-gui needs X11 or XWayland")
set(cannot_open "cannot open X display ':987'; rawreel-gui needs X11 or XWayland")
set(failures "")

# run(<name> <expected exit> <regex in stdout+stderr> <env args...> -- <gui args...>)
function(run name expected pattern)
    set(env_args "")
    set(gui_args "")
    set(in_gui FALSE)
    foreach(arg IN LISTS ARGN)
        if(arg STREQUAL "--")
            set(in_gui TRUE)
        elseif(in_gui)
            list(APPEND gui_args "${arg}")
        else()
            list(APPEND env_args "${arg}")
        endif()
    endforeach()
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env --unset=DISPLAY --unset=WAYLAND_DISPLAY ${env_args}
                "${GUI}" ${gui_args}
        RESULT_VARIABLE code OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 60)
    set(problem "")
    if(NOT code STREQUAL "${expected}")
        string(APPEND problem "exit ${code}, expected ${expected}; ")
    endif()
    if(NOT "${out}${err}" MATCHES "${pattern}")
        string(APPEND problem "'${pattern}' not found; ")
    endif()
    if(problem)
        set(failures "${failures}\n${name}: ${problem}\nstdout:\n${out}\nstderr:\n${err}"
            PARENT_SCOPE)
    else()
        message(STATUS "${name}: exit ${code} as expected")
    endif()
endfunction()

run(no-display 77 "${no_display}" --unset=DVV_REQUIRE_GPU -- --selftest smoke)
run(no-display-required 4 "${no_display}" DVV_REQUIRE_GPU=1 -- --selftest smoke)
run(cannot-open 77 "${cannot_open}" --unset=DVV_REQUIRE_GPU DISPLAY=:987 -- --selftest smoke)
run(help 0 "Usage: rawreel-gui" --unset=DVV_REQUIRE_GPU -- --help)

if(failures)
    message(FATAL_ERROR "rawreel-gui start-up without a display:${failures}")
endif()
