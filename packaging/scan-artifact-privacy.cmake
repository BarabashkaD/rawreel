# Artifact privacy scan (design M4 v2 D16): the rules of rawreel-workspace/tools/privacy-lint.cmake,
# narrowed to what may leak into a built package. Absolute drive paths outside user profiles (the
# runners' generic build roots, the subst drive of the local dry run) and the runner's own home
# folder are accepted.
#
#   rawreel_scan_privacy(FILE <file> MODE full|paths|notices [BINARY] [EXTRA_TERMS <term>...]
#                        FINDINGS <var>)
#
# MODE full: user-profile paths, /home/<name> other than runner, non-noreply e-mail addresses,
# IPv4 addresses and EXTRA_TERMS (3 or more characters, case-insensitive substrings). MODE paths:
# the two path rules and EXTRA_TERMS. MODE notices: the full rules on the lines outside licence blocks, the
# path rules on every line. BINARY: the ASCII strings and the UTF-16LE strings of the file.
# FINDINGS receives "<file name>: <kind>" items (each kind once per file), never the matched text.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/check-package.cmake")

# Exact strings that look like IPv4 addresses but are version numbers or data of a statically
# linked library. Each entry needs a comment naming its origin.
set(RAWREEL_PRIVACY_IPV4_ALLOW
    # wxWidgets (static): the Common-Controls assembly version in its Windows manifest
    # (wx/msw/wx_dpi_aware*.manifest, embedded through wx.rc).
    "6.0.0.0" # privacy-lint: allow
    # wxWidgets (static): number runs in the path data of its built-in SVG art (the last three
    # follow a dot or a minus sign there; the run starts at the first digit).
    "6.84.005.003" # privacy-lint: allow
    "7.016.011.005" # privacy-lint: allow
    "003.1.006.218" # privacy-lint: allow
    "002.002.001.029" # privacy-lint: allow
    "125.03.59.59") # privacy-lint: allow
# Exact public e-mail addresses of a statically linked library (same rule as above).
set(RAWREEL_PRIVACY_EMAIL_ALLOW
    # wxWidgets (static): its public developer list, named in the missing-manifest error message.
    "wx-dev@googlegroups.com") # privacy-lint: allow

# Any name after Users\, also one that starts with a non-ASCII character (file(STRINGS) may end the
# string right after the separator there).
set(_rawreel_profile_regex "[A-Za-z]:[\\/]+[Uu][Ss][Ee][Rr][Ss][\\/]+([^\\/ \t\"]|$)")
set(_rawreel_home_regex "/home/[A-Za-z0-9._-]+")
set(_rawreel_email_regex "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\\.[A-Za-z][A-Za-z]+")
set(_rawreel_ipv4_regex "[0-9]\\.[0-9]+\\.[0-9]+\\.[0-9]")

function(_rawreel_email_allowed email out)
    if(email MATCHES "@users\\.noreply\\.github\\.com$" OR email MATCHES "@example\\.(com|org|net)$"
       OR email STREQUAL "noreply@anthropic.com" OR email STREQUAL "noreply@github.com"
       OR email IN_LIST RAWREEL_PRIVACY_EMAIL_ALLOW)
        set(${out} ON PARENT_SCOPE)
    else()
        set(${out} OFF PARENT_SCOPE)
    endif()
endfunction()

# A case-insensitive regex for a literal term (CMake regexes have no case-insensitive flag).
function(_rawreel_term_regex term out)
    string(LENGTH "${term}" length)
    math(EXPR last "${length} - 1")
    set(regex "")
    foreach(index RANGE ${last})
        string(SUBSTRING "${term}" ${index} 1 char)
        string(TOLOWER "${char}" lower)
        string(TOUPPER "${char}" upper)
        if(NOT lower STREQUAL upper)
            string(APPEND regex "[${lower}${upper}]")
        elseif(char MATCHES "[A-Za-z0-9]")
            string(APPEND regex "${char}")
        elseif(char MATCHES "[]^[\\]")
            string(APPEND regex "\\${char}")
        else()
            string(APPEND regex "[${char}]")
        endif()
    endforeach()
    set(${out} "${regex}" PARENT_SCOPE)
endfunction()

# The kinds of finding in one line of text; FULL OFF applies the path rules and the terms only.
function(_rawreel_privacy_line text full terms out)
    set(kinds "")
    if(text MATCHES "${_rawreel_profile_regex}")
        list(APPEND kinds "Windows user profile path")
    endif()
    string(REGEX MATCHALL "${_rawreel_home_regex}" homes "${text}")
    foreach(home IN LISTS homes)
        if(NOT home STREQUAL "/home/runner") # privacy-lint: allow
            list(APPEND kinds "home directory path")
            break()
        endif()
    endforeach()
    if(full)
        string(REGEX MATCHALL "${_rawreel_email_regex}" emails "${text}")
        foreach(email IN LISTS emails)
            _rawreel_email_allowed("${email}" ok)
            if(NOT ok)
                list(APPEND kinds "e-mail address that is not a noreply address")
                break()
            endif()
        endforeach()
        # Every maximal run of digits and dots that starts with a digit is checked as a whole (the
        # repository lint's rule), so 1.2.3.4.5 and versions with a part above 255 are no addresses.
        string(REGEX MATCHALL "[0-9][0-9.]*" runs "${text}")
        foreach(run IN LISTS runs)
            string(REGEX REPLACE "\\.+$" "" run "${run}")
            if(NOT run MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
                continue()
            endif()
            set(octets "${CMAKE_MATCH_1}" "${CMAKE_MATCH_2}" "${CMAKE_MATCH_3}" "${CMAKE_MATCH_4}")
            if(run STREQUAL "127.0.0.1" OR run STREQUAL "0.0.0.0" OR run IN_LIST
               RAWREEL_PRIVACY_IPV4_ALLOW)
                continue()
            endif()
            set(valid ON)
            foreach(octet IN LISTS octets)
                string(LENGTH "${octet}" octet_length)
                if(octet_length GREATER 3 OR octet GREATER 255)
                    set(valid OFF)
                endif()
            endforeach()
            if(valid)
                list(APPEND kinds "IPv4 address")
                break()
            endif()
        endforeach()
    endif()
    if(terms)
        string(TOLOWER "${text}" lowered)
        foreach(term IN LISTS terms)
            string(TOLOWER "${term}" term)
            string(FIND "${lowered}" "${term}" position)
            if(NOT position EQUAL -1)
                list(APPEND kinds "local user or host name")
                break()
            endif()
        endforeach()
    endif()
    set(${out} "${kinds}" PARENT_SCOPE)
endfunction()

# The prefilter regex (one alternation of the rules) for file(STRINGS ... REGEX).
function(_rawreel_privacy_prefilter full terms out)
    set(parts "[A-Za-z]:[\\/]+[Uu][Ss][Ee][Rr][Ss][\\/]" "/home/")
    if(full)
        list(APPEND parts "@" "${_rawreel_ipv4_regex}")
    endif()
    foreach(term IN LISTS terms)
        _rawreel_term_regex("${term}" term_regex)
        list(APPEND parts "${term_regex}")
    endforeach()
    list(JOIN parts "|" regex)
    set(${out} "(${regex})" PARENT_SCOPE)
endfunction()

function(rawreel_scan_privacy)
    cmake_parse_arguments(PARSE_ARGV 0 arg "BINARY" "FILE;MODE;FINDINGS" "EXTRA_TERMS")
    if(NOT arg_MODE MATCHES "^(full|paths|notices)$")
        message(FATAL_ERROR "rawreel_scan_privacy: MODE must be full, paths or notices")
    endif()
    if(NOT EXISTS "${arg_FILE}")
        message(FATAL_ERROR "rawreel_scan_privacy: ${arg_FILE} does not exist")
    endif()
    set(terms "")
    foreach(term IN LISTS arg_EXTRA_TERMS)
        string(LENGTH "${term}" length)
        if(length GREATER_EQUAL 3)
            list(APPEND terms "${term}")
        endif()
    endforeach()
    set(full OFF)
    if(arg_MODE STREQUAL "full")
        set(full ON)
    endif()
    # MODE notices: the terms only outside the licence blocks (below).
    set(line_terms "${terms}")
    if(arg_MODE STREQUAL "notices")
        set(line_terms "")
    endif()
    _rawreel_privacy_prefilter(${full} "${line_terms}" prefilter)

    set(kinds "")
    set(encodings UTF-8)
    if(arg_BINARY)
        set(encodings DEFAULT UTF-16LE)
    endif()
    foreach(encoding IN LISTS encodings)
        if(encoding STREQUAL "DEFAULT")
            file(STRINGS "${arg_FILE}" hits REGEX "${prefilter}" LENGTH_MINIMUM 3)
        else()
            file(STRINGS "${arg_FILE}" hits REGEX "${prefilter}" LENGTH_MINIMUM 3
                 ENCODING ${encoding})
        endif()
        foreach(hit IN LISTS hits)
            _rawreel_privacy_line("${hit}" ${full} "${line_terms}" found)
            list(APPEND kinds ${found})
        endforeach()
    endforeach()

    if(arg_MODE STREQUAL "notices")
        # The full rules on every line outside the licence blocks (check-package.cmake's parser).
        _rawreel_notices_parse("${arg_FILE}" entries msvc outside)
        _rawreel_privacy_prefilter(ON "${terms}" full_prefilter)
        foreach(line IN LISTS outside)
            _rawreel_line_decode("${line}" line)
            if(line MATCHES "${full_prefilter}")
                _rawreel_privacy_line("${line}" ON "${terms}" found)
                list(APPEND kinds ${found})
            endif()
        endforeach()
    endif()

    list(REMOVE_DUPLICATES kinds)
    cmake_path(GET arg_FILE FILENAME name)
    set(findings "")
    foreach(kind IN LISTS kinds)
        list(APPEND findings "${name}: ${kind}")
    endforeach()
    set(${arg_FINDINGS} "${findings}" PARENT_SCOPE)
endfunction()
