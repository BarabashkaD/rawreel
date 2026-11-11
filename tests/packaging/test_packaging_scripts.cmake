# Unit tests of packaging/check-package.cmake and packaging/scan-artifact-privacy.cmake, and the
# argument checks of packaging/dry-run.cmake. Run as
# cmake -DWORK_DIR=<dir> -P test_packaging_scripts.cmake (CTest rawreel.packaging_scripts). Every
# tree and file is built inside WORK_DIR, which is emptied first.
# Each case prints "PASS <case>"; the first failed assertion stops the script. Lines that hold a
# deliberate privacy pattern carry the repository lint's allow marker; the paths use the drive X:
# and made-up names.
cmake_minimum_required(VERSION 3.25)

if(NOT WORK_DIR)
    message(FATAL_ERROR "test_packaging_scripts: WORK_DIR is required")
endif()
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

include("${CMAKE_CURRENT_LIST_DIR}/../../packaging/check-package.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../packaging/source-txt.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/../../packaging/scan-artifact-privacy.cmake")

# --- helpers ---------------------------------------------------------------------------------

function(expect_empty case list)
    if(NOT "${list}" STREQUAL "")
        message(FATAL_ERROR "FAIL ${case}: expected no items, got: ${list}")
    endif()
endfunction()

function(expect_item case list item)
    list(FIND list "${item}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: expected item '${item}', got: ${list}")
    endif()
endfunction()

function(expect_count case list count)
    list(LENGTH list length)
    if(NOT length EQUAL count)
        message(FATAL_ERROR "FAIL ${case}: expected ${count} items, got ${length}: ${list}")
    endif()
endfunction()

function(expect_equal case actual expected)
    if(NOT "${actual}" STREQUAL "${expected}")
        message(FATAL_ERROR "FAIL ${case}: expected '${expected}', got '${actual}'")
    endif()
endfunction()

function(pass case)
    message(STATUS "PASS ${case}")
    set_property(GLOBAL APPEND PROPERTY test_packaging_passed "${case}")
endfunction()

# Every privacy finding of cases 11-18, checked by case 19.
set(all_findings "")

# --- manifest (cases 1-6) --------------------------------------------------------------------

set(tree "${WORK_DIR}/tree")
set(pkg "${tree}/rawreel-1.2.3")
file(WRITE "${pkg}/a.exe" "exe")
file(WRITE "${pkg}/THIRD_PARTY_NOTICES.txt" "notices")
file(WRITE "${pkg}/ffmpeg/x.dll" "library")
file(WRITE "${pkg}/ffmpeg/SOURCE.txt" "source offer")
file(SHA256 "${pkg}/ffmpeg/x.dll" x_sha)
set(manifest "${WORK_DIR}/manifest.txt")
# The comment holds a non-ASCII character (file(STRINGS) would split the line there), ';', '[',
# ']' and a CRLF ending, as the real manifest's header does.
file(WRITE "${manifest}" "# a comment; kind path [sha256]\r\n\nours rawreel-@V@/a.exe\n"
                         "notices rawreel-@V@/THIRD_PARTY_NOTICES.txt\n"
                         "ffmpeg rawreel-@V@/ffmpeg/x.dll ${x_sha}\n"
                         "ours-bundled rawreel-@V@/ffmpeg/SOURCE.txt\n")

set(case "manifest: exact tree passes")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors CLASSES classes)
expect_empty("${case}" "${errors}")
expect_item("${case}" "${classes}" "ours|rawreel-1.2.3/a.exe")
expect_item("${case}" "${classes}" "ffmpeg|rawreel-1.2.3/ffmpeg/x.dll")
expect_count("${case}" "${classes}" 4)
pass("${case}")

set(case "manifest: extra file fails")
file(WRITE "${pkg}/evil.dll" "evil")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_item("${case}" "${errors}" "unexpected file rawreel-1.2.3/evil.dll")
expect_count("${case}" "${errors}" 1)
file(REMOVE "${pkg}/evil.dll")
pass("${case}")

set(case "manifest: missing file fails")
file(RENAME "${pkg}/a.exe" "${WORK_DIR}/a.exe.saved")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_item("${case}" "${errors}" "missing file rawreel-1.2.3/a.exe")
expect_count("${case}" "${errors}" 1)
file(RENAME "${WORK_DIR}/a.exe.saved" "${pkg}/a.exe")
pass("${case}")

set(case "manifest: changed third-party file fails")
file(WRITE "${pkg}/ffmpeg/x.dll" "librarz")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_item("${case}" "${errors}" "SHA256 mismatch rawreel-1.2.3/ffmpeg/x.dll")
expect_count("${case}" "${errors}" 1)
file(WRITE "${pkg}/ffmpeg/x.dll" "library")
pass("${case}")

set(case "manifest: not bundled")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED OFF
                       ERRORS errors)
expect_item("${case}" "${errors}" "unexpected file rawreel-1.2.3/ffmpeg/x.dll")
expect_item("${case}" "${errors}" "unexpected file rawreel-1.2.3/ffmpeg/SOURCE.txt")
expect_count("${case}" "${errors}" 2)
file(RENAME "${pkg}/ffmpeg" "${WORK_DIR}/ffmpeg.saved")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.3 BUNDLED OFF
                       ERRORS errors)
expect_empty("${case}" "${errors}")
file(RENAME "${WORK_DIR}/ffmpeg.saved" "${pkg}/ffmpeg")
pass("${case}")

set(case "manifest: version placeholder")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${manifest}" VERSION 1.2.4 BUNDLED ON
                       ERRORS errors)
expect_item("${case}" "${errors}" "missing file rawreel-1.2.4/a.exe")
expect_item("${case}" "${errors}" "unexpected file rawreel-1.2.3/a.exe")
pass("${case}")

set(case "manifest: a vulkan file has a SHA256 and ships in both variants")
file(WRITE "${pkg}/lib/v.dylib" "driver")
file(SHA256 "${pkg}/lib/v.dylib" v_sha)
set(vulkan_manifest "${WORK_DIR}/manifest-vulkan.txt")
file(READ "${manifest}" vulkan_text)
file(WRITE "${vulkan_manifest}" "${vulkan_text}vulkan rawreel-@V@/lib/v.dylib ${v_sha}\n")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${vulkan_manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors CLASSES classes)
expect_empty("${case}" "${errors}")
expect_item("${case}" "${classes}" "vulkan|rawreel-1.2.3/lib/v.dylib")
file(RENAME "${pkg}/ffmpeg" "${WORK_DIR}/ffmpeg.saved")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${vulkan_manifest}" VERSION 1.2.3 BUNDLED OFF
                       ERRORS errors)
expect_empty("${case}" "${errors}")
file(RENAME "${WORK_DIR}/ffmpeg.saved" "${pkg}/ffmpeg")
file(WRITE "${pkg}/lib/v.dylib" "drivez")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${vulkan_manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_equal("${case}" "${errors}" "SHA256 mismatch rawreel-1.2.3/lib/v.dylib")
file(WRITE "${vulkan_manifest}" "vulkan rawreel-@V@/lib/v.dylib\n")
rawreel_check_manifest(ROOT "${tree}" MANIFEST "${vulkan_manifest}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_item("${case}" "${errors}"
            "bad manifest line (vulkan needs a SHA256): vulkan rawreel-@V@/lib/v.dylib")
file(REMOVE_RECURSE "${pkg}/lib")
pass("${case}")

# --- notices (cases 7-9, 22, 23) -------------------------------------------------------------

string(REPEAT "=" 62 sep)
string(CONCAT alpha_entry "\n${sep}\nalpha 1.0\nLicence: MIT\nSource: https://example.com/alpha\n"
              "\n--- LICENSE ---\n\nAlpha licence text\n=====\nMore alpha text\n")
string(CONCAT beta_head "\n${sep}\nbeta 2.0\nLicence: BSD-3-Clause\nSource: https://example.com/beta\n"
              "\n--- LICENSE ---\n\nBeta licence text\n==========\n")
set(beta_tail "More beta text\n")
string(CONCAT msvc_paragraph "\n${sep}\nMicrosoft Visual C++ runtime\n"
              "Files: vcruntime140.dll and the other Visual C++ runtime DLLs\n"
              "Licence: Microsoft Software License Terms for Microsoft Visual Studio\n"
              "Source: https://visualstudio.microsoft.com/license-terms/\n")
string(CONCAT notices_text "Third-party software in the rawreel packages\n" "${alpha_entry}"
              "${beta_head}" "${beta_tail}")
set(notices "${WORK_DIR}/notices.txt")
file(WRITE "${notices}" "${notices_text}")

set(case "notices: exact shipped set passes")
rawreel_check_notices(FILE "${notices}" SHIPPED alpha beta ERRORS errors)
expect_empty("${case}" "${errors}")
pass("${case}")

set(case "notices: missing entry fails")
rawreel_check_notices(FILE "${notices}" SHIPPED alpha beta gamma ERRORS errors)
expect_item("${case}" "${errors}" "missing notice gamma")
expect_count("${case}" "${errors}" 1)
pass("${case}")

set(case "notices: extra entry fails")
rawreel_check_notices(FILE "${notices}" SHIPPED alpha ERRORS errors)
expect_item("${case}" "${errors}" "unexpected notice beta")
expect_count("${case}" "${errors}" 1)
pass("${case}")

# --- privacy (cases 10-19) -------------------------------------------------------------------

set(P1 [[X:\Users\someone\src\a.cpp]]) # privacy-lint: allow
set(P2 [[x:/users/someone/a.cpp]]) # privacy-lint: allow
set(P3 [[X:\Users\bob\a]]) # privacy-lint: allow
set(H1 "/home/alice/src/a.cpp") # privacy-lint: allow
set(R1 "/home/runner/work/rawreel") # privacy-lint: allow
set(D1 [[X:\a\rawreel]]) # privacy-lint: allow
set(E1 "dev@corp.io") # privacy-lint: allow
set(E2 "jloup@gzip.org") # privacy-lint: allow
set(N1 "noreply@anthropic.com") # privacy-lint: allow
set(N2 "someone@users.noreply.github.com") # privacy-lint: allow
set(I1 "10.1.2.3") # privacy-lint: allow
set(I2 "300.1.2.3") # privacy-lint: allow
set(I3 "1.2.3")
set(I4 "1.2.3.4.5") # privacy-lint: allow
set(I5 "127.0.0.1") # privacy-lint: allow
set(U1 "/Users/alice/src/a.cpp") # privacy-lint: allow
set(U2 "/Users/runner/work/rawreel") # privacy-lint: allow

function(scan_text name text mode out)
    file(WRITE "${WORK_DIR}/${name}" "${text}\n")
    rawreel_scan_privacy(FILE "${WORK_DIR}/${name}" MODE ${mode} ${ARGN} FINDINGS findings)
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

set(case "privacy: clean text")
scan_text(clean.txt "built in ${R1}, ${U2} and ${D1}, version ${I4}, ${N1}, ${N2}, ${I5}" full
          findings)
expect_empty("${case}" "${findings}")
pass("${case}")

set(case "privacy: allowlisted wxWidgets strings pass")
scan_text(allow.txt "version=\"6.0.0.0\" d=\"l6.84.005.003 -7.016.011.005\" posting to wx-dev@googlegroups.com" full findings) # privacy-lint: allow
expect_empty("${case}" "${findings}")
pass("${case}")

# The allowlists match whole strings only: a longer run or address that contains an entry fails.
set(case "privacy: near misses of the allowlist fail")
scan_text(near1.txt "version=\"16.0.0.0\"" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "near1.txt: IPv4 address")
scan_text(near2.txt "posting to xwx-dev@googlegroups.com" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "near2.txt: e-mail address that is not a noreply address")
scan_text(near3.txt "posting to wx-dev@googlegroups.community" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "near3.txt: e-mail address that is not a noreply address")
pass("${case}")

set(case "privacy: the SVG number runs after a dot are allowlisted exactly")
scan_text(svg.txt "d=\"c1.997-.003.1.006.218 0-.125.03.59.59 .002.002.001.029\"" full findings) # privacy-lint: allow
expect_empty("${case}" "${findings}")
pass("${case}")

# A run after a dot is checked like any other (the repository lint's rule).
set(case "privacy: an address after a dot fails")
scan_text(dot1.txt "host x.10.0.0.5 up" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "dot1.txt: IPv4 address")
scan_text(dot2.txt "d=\"-.003.1.006.219\"" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "dot2.txt: IPv4 address")
pass("${case}")

# file(STRINGS) ends a string at a non-ASCII byte, so a profile path can end right after Users\.
set(case "privacy: a profile path with a non-ASCII or cut-off user name")
scan_text(u1.txt "X:\\Users\\" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "u1.txt: Windows user profile path")
scan_text(u2.txt "X:\\Users\\Ölaf\\a.obj" full findings) # privacy-lint: allow
expect_item("${case}" "${findings}" "u2.txt: Windows user profile path")
pass("${case}")

set(case "privacy: user profile path")
scan_text(p1.txt "source ${P1}" full findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "p1.txt: Windows user profile path")
expect_count("${case}" "${findings}" 1)
scan_text(p2.txt "source ${P2}" full findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "p2.txt: Windows user profile path")
expect_count("${case}" "${findings}" 1)
pass("${case}")

set(case "privacy: home path")
scan_text(h1.txt "source ${H1}" full findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "h1.txt: home directory path")
expect_count("${case}" "${findings}" 1)
pass("${case}")

# macOS: /Users/<name> like /home/<name>; the runner's home folder is accepted there too, but not a
# longer name that starts with "runner".
set(case "privacy: macOS home path")
scan_text(u1.txt "source ${U1}" full findings)
list(APPEND all_findings ${findings})
expect_equal("${case}" "${findings}" "u1.txt: home directory path")
scan_text(u2.txt "source ${U2}" paths findings)
expect_empty("${case}" "${findings}")
scan_text(u3.txt "source /Users/runner2/a.cpp" paths findings) # privacy-lint: allow
expect_equal("${case}" "${findings}" "u3.txt: home directory path")
pass("${case}")

set(case "privacy: e-mail")
scan_text(e1.txt "contact ${E1}" full findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "e1.txt: e-mail address that is not a noreply address")
expect_count("${case}" "${findings}" 1)
pass("${case}")

set(case "privacy: IPv4")
scan_text(i1.txt "server ${I1}" full findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "i1.txt: IPv4 address")
expect_count("${case}" "${findings}" 1)
scan_text(i2.txt "server ${I2} and ${I3}" full findings)
expect_empty("${case}" "${findings}")
pass("${case}")

set(case "privacy: extra term")
scan_text(t1.txt "built on MACHINEX-07" full findings EXTRA_TERMS machinex)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "t1.txt: local user or host name")
expect_count("${case}" "${findings}" 1)
scan_text(t2.txt "built on MACHINEX-07 by ab" full findings EXTRA_TERMS ab)
expect_empty("${case}" "${findings}")
pass("${case}")

set(case "privacy: paths mode")
scan_text(m1.txt "${E1} ${I1}" paths findings)
expect_empty("${case}" "${findings}")
scan_text(m2.txt "${P3}" paths findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "m2.txt: Windows user profile path")
expect_count("${case}" "${findings}" 1)
pass("${case}")

set(case "privacy: paths mode with extra terms")
scan_text(m3.txt "linked on MACHINEX-07" paths findings EXTRA_TERMS machinex)
expect_item("${case}" "${findings}" "m3.txt: local user or host name")
expect_count("${case}" "${findings}" 1)
pass("${case}")

set(case "privacy: notices mode")
string(CONCAT clean_notices "Third-party software in the rawreel packages\n" "${alpha_entry}"
              "Written by ${E2}\n" "${beta_head}" "${beta_tail}")
file(WRITE "${WORK_DIR}/n1.txt" "${clean_notices}")
rawreel_scan_privacy(FILE "${WORK_DIR}/n1.txt" MODE notices FINDINGS findings)
expect_empty("${case}" "${findings}")
string(CONCAT path_notices "Third-party software in the rawreel packages\n" "${alpha_entry}"
              "Written by ${E2} in ${P3}\n" "${beta_head}" "${beta_tail}")
file(WRITE "${WORK_DIR}/n2.txt" "${path_notices}")
rawreel_scan_privacy(FILE "${WORK_DIR}/n2.txt" MODE notices FINDINGS findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "n2.txt: Windows user profile path")
expect_count("${case}" "${findings}" 1)
string(REPLACE "Source: https://example.com/beta" "Source: ${E1}" mail_notices "${clean_notices}")
file(WRITE "${WORK_DIR}/n3.txt" "${mail_notices}")
rawreel_scan_privacy(FILE "${WORK_DIR}/n3.txt" MODE notices FINDINGS findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "n3.txt: e-mail address that is not a noreply address")
expect_count("${case}" "${findings}" 1)
pass("${case}")

set(case "privacy: UTF-16LE binary")
set(fixture "${CMAKE_CURRENT_LIST_DIR}/data/utf16le-user-path.bin")
rawreel_scan_privacy(FILE "${fixture}" MODE full BINARY FINDINGS findings)
list(APPEND all_findings ${findings})
expect_item("${case}" "${findings}" "utf16le-user-path.bin: Windows user profile path")
expect_count("${case}" "${findings}" 1)
rawreel_scan_privacy(FILE "${fixture}" MODE full FINDINGS findings)
expect_empty("${case}" "${findings}")
pass("${case}")

set(case "privacy: findings never repeat the text")
expect_count("${case}" "${all_findings}" 11)
foreach(secret IN ITEMS someone alice bob corp.io 10.1.2.3 machinex) # privacy-lint: allow
    string(TOLOWER "${all_findings}" lowered)
    string(FIND "${lowered}" "${secret}" position)
    if(NOT position EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: a finding repeats '${secret}'")
    endif()
endforeach()
pass("${case}")

# --- PDB reference (case 20), attestation (case 21), CLI FFmpeg line (case 24) ---------------

set(case "pdb reference")
file(WRITE "${WORK_DIR}/good.exe" "MZ header\nrawreel-gui.pdb\nmsvcrt.dll\n")
rawreel_check_pdb_reference(FILE "${WORK_DIR}/good.exe" ERRORS errors)
expect_empty("${case}" "${errors}")
file(WRITE "${WORK_DIR}/bad.exe" "MZ header\n${D1}\\rawreel-gui.pdb\n")
rawreel_check_pdb_reference(FILE "${WORK_DIR}/bad.exe" ERRORS errors)
expect_item("${case}" "${errors}" "the exe names its PDB with a directory")
expect_count("${case}" "${errors}" 1)
pass("${case}")

set(case "attestation: source commit and ref")
string(REPEAT "a" 40 digest)
set(json "[{\"verificationResult\":{\"signature\":{\"certificate\":{\"sourceRepositoryDigest\":\"${digest}\",\"sourceRepositoryRef\":\"refs/tags/v1.0.0\"}}}}]")
rawreel_attestation_source(JSON "${json}" COMMIT commit REF ref ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${commit}" "${digest}")
expect_equal("${case}" "${ref}" "refs/tags/v1.0.0")
rawreel_attestation_source(JSON "[]" COMMIT commit REF ref ERRORS errors)
expect_item("${case}" "${errors}" "attestation output has no sourceRepositoryDigest")
string(REPEAT "a" 39 short_digest)
string(REPLACE "${digest}" "${short_digest}" short_json "${json}")
rawreel_attestation_source(JSON "${short_json}" COMMIT commit REF ref ERRORS errors)
expect_item("${case}" "${errors}" "attestation output has no sourceRepositoryDigest")
pass("${case}")

# The not-available line holds a ';' (and '\', '[', ']'), which must not split it into two items.
set(case "cli: the FFmpeg line of --version")
string(CONCAT missing_line "FFmpeg: not available (FFmpeg not found (looked in x\\ffmpeg and "
              "x\\lib\\ffmpeg; set DVV_FFMPEG_DIR [a b]))")
rawreel_ffmpeg_version_line(TEXT "rawreel 0.9.0\nwxWidgets 3.3.4\n${missing_line}\n"
                            LINE line ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${line}" "${missing_line}")
rawreel_ffmpeg_version_line(TEXT "rawreel 0.9.0\r\nFFmpeg 8.0.1 (LGPL) from x\\ffmpeg\r\n"
                            LINE line ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${line}" "FFmpeg 8.0.1 (LGPL) from x\\ffmpeg")
rawreel_ffmpeg_version_line(TEXT "FFmpeg a; b\nlibFFmpeg c\nFFmpeg d\n" LINE line ERRORS errors)
expect_item("${case}" "${errors}" "--version must print one line starting with FFmpeg, printed 2")
expect_count("${case}" "${errors}" 1)
rawreel_ffmpeg_version_line(TEXT "rawreel 0.9.0\n x FFmpeg\n" LINE line ERRORS errors)
expect_item("${case}" "${errors}" "--version must print one line starting with FFmpeg, printed 0")
pass("${case}")

# --- licence supplements, work folder -----------------------------------------------------------

# The supplements ship as the pinned files, so a long = line inside a licence text stays.
set(case "supplements: the pinned files, a long = line inside a text included")
string(REPEAT "=" 80 underline)
file(WRITE "${WORK_DIR}/supp/a/LICENSE" "MIT text\n${underline}\n\nCopyright Example\nsecond notice\n")
file(WRITE "${WORK_DIR}/supp/b.txt" "BSD text\n")
file(SHA256 "${WORK_DIR}/supp/a/LICENSE" a_sha)
file(SHA256 "${WORK_DIR}/supp/b.txt" b_sha)
rawreel_licence_supplements(DIR "${WORK_DIR}/supp" SUPPLEMENTS "a/LICENSE|${a_sha}" "b.txt|${b_sha}"
                            PATHS paths ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${paths}" "a/LICENSE;b.txt")
file(WRITE "${WORK_DIR}/supp/a/LICENSE" "MIT text\n")
rawreel_licence_supplements(DIR "${WORK_DIR}/supp" SUPPLEMENTS "a/LICENSE|${a_sha}" "c.txt|${b_sha}"
                            "../b.txt|${b_sha}" PATHS paths ERRORS errors)
expect_item("${case}" "${errors}" "supplement a/LICENSE does not match its pin")
expect_item("${case}" "${errors}" "missing supplement c.txt")
expect_item("${case}" "${errors}" "supplement path ../b.txt is not relative")
expect_count("${case}" "${errors}" 3)
pass("${case}")

set(case "work folder: never a root or a folder that holds a protected path")
rawreel_work_dir_errors(DIR "${WORK_DIR}/w" PROTECT "${WORK_DIR}/out" ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_work_dir_errors(DIR "${WORK_DIR}" PROTECT "${WORK_DIR}/out" ERRORS errors)
expect_count("${case}" "${errors}" 1)
rawreel_work_dir_errors(DIR "${WORK_DIR}/out/" PROTECT "${WORK_DIR}/out" ERRORS errors)
expect_count("${case}" "${errors}" 1)
cmake_path(GET WORK_DIR ROOT_PATH root)
rawreel_work_dir_errors(DIR "${root}" ERRORS errors)
expect_count("${case}" "${errors}" 1)
if(CMAKE_HOST_WIN32)
    string(TOUPPER "${WORK_DIR}" upper)
    rawreel_work_dir_errors(DIR "${upper}" PROTECT "${WORK_DIR}/out" ERRORS errors)
    expect_count("${case}" "${errors}" 1)
endif()
pass("${case}")

# --- notices, continued (cases 22, 23) -------------------------------------------------------

set(case "notices: the Visual C++ runtime paragraph fails and is stripped")
file(WRITE "${WORK_DIR}/notices-msvc.txt" "${notices_text}${msvc_paragraph}")
rawreel_check_notices(FILE "${WORK_DIR}/notices-msvc.txt" SHIPPED alpha beta ERRORS errors)
expect_item("${case}" "${errors}" "unexpected notice Microsoft Visual C++ runtime")
expect_count("${case}" "${errors}" 1)
rawreel_strip_msvc_notice(IN "${WORK_DIR}/notices-msvc.txt" OUT "${WORK_DIR}/notices-stripped.txt")
file(READ "${WORK_DIR}/notices-stripped.txt" stripped)
expect_equal("${case}" "${stripped}" "${notices_text}")
rawreel_check_notices(FILE "${WORK_DIR}/notices-stripped.txt" SHIPPED alpha beta ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_strip_msvc_notice(IN "${notices}" OUT "${WORK_DIR}/notices-unchanged.txt")
file(READ "${WORK_DIR}/notices-unchanged.txt" unchanged)
expect_equal("${case}" "${unchanged}" "${notices_text}")
pass("${case}")

set(case "notices: a long = line inside a licence text is no entry")
string(CONCAT long_line_text "Third-party software in the rawreel packages\n" "${alpha_entry}"
              "${beta_head}" "${sep}\nCopyright 2020 Example\nSome licence text\n" "${beta_tail}")
file(WRITE "${WORK_DIR}/notices-long.txt" "${long_line_text}")
rawreel_check_notices(FILE "${WORK_DIR}/notices-long.txt" SHIPPED alpha beta ERRORS errors)
expect_empty("${case}" "${errors}")
pass("${case}")

set(case "notices: an appended entry is parsed with its licence block")
file(WRITE "${WORK_DIR}/notices-append.txt" "${notices_text}")
file(WRITE "${WORK_DIR}/lucide-licence.txt" "Lucide licence text\n---\nFeather licence text  \n\n")
rawreel_append_notice(FILE "${WORK_DIR}/notices-append.txt" NAME lucide VERSION 9.8.7
                      SPDX "ISC AND MIT" URL https://example.com/lucide
                      LICENCE "${WORK_DIR}/lucide-licence.txt")
file(READ "${WORK_DIR}/notices-append.txt" appended)
string(CONCAT lucide_entry "\n${sep}\nlucide 9.8.7\nLicence: ISC AND MIT\n"
              "Source: https://example.com/lucide\n\n--- LICENSE ---\n\n"
              "Lucide licence text\n---\nFeather licence text\n")
expect_equal("${case}" "${appended}" "${notices_text}${lucide_entry}")
_rawreel_notices_parse("${WORK_DIR}/notices-append.txt" entries msvc outside)
expect_equal("${case}" "${entries}" "alpha;beta;lucide")
expect_equal("${case}" "${msvc}" "OFF")
list(FIND outside "Lucide licence text" outside_index)
expect_equal("${case}" "${outside_index}" "-1")
list(FIND outside "lucide 9.8.7" head_index)
if(head_index EQUAL -1)
    message(FATAL_ERROR "FAIL ${case}: the entry's head is not outside a licence block")
endif()
rawreel_check_notices(FILE "${WORK_DIR}/notices-append.txt" SHIPPED alpha beta lucide
                      ERRORS errors)
expect_empty("${case}" "${errors}")
pass("${case}")

# --- no VC++ runtime in the zip (cases 24, 25) -----------------------------------------------

set(case "no VC++ runtime: the runtime DLLs are found case-insensitively")
set(vc_tree "${WORK_DIR}/vc-tree")
file(WRITE "${vc_tree}/rawreel-1.2.3/rawreel-gui.exe" "exe")
file(WRITE "${vc_tree}/rawreel-1.2.3/ffmpeg/avutil-60.dll" "library")
rawreel_vc_runtime_files(ROOT "${vc_tree}" FILES found)
expect_empty("${case}" "${found}")
foreach(name IN ITEMS VCRUNTIME140.dll vcruntime140_1.dll msvcp140_atomic_wait.dll concrt140.dll
                      vccorlib140.dll)
    file(WRITE "${vc_tree}/rawreel-1.2.3/${name}" "runtime")
endforeach()
file(WRITE "${vc_tree}/rawreel-1.2.3/ffmpeg/msvcp140.dll" "runtime")
rawreel_vc_runtime_files(ROOT "${vc_tree}" FILES found)
expect_count("${case}" "${found}" 6)
expect_item("${case}" "${found}" "rawreel-1.2.3/ffmpeg/msvcp140.dll")
expect_item("${case}" "${found}" "rawreel-1.2.3/VCRUNTIME140.dll")
pass("${case}")

set(case "redist minimum: one value, checked against the toolset and the documents")
file(WRITE "${WORK_DIR}/readme.md" "- Microsoft Visual C++ Redistributable (x64) 14.51 or newer\n")
file(WRITE "${WORK_DIR}/notes.md" "Install the Visual C++ Redistributable (x64) 14.44 or newer.\n")
rawreel_redist_minimum_errors(MINIMUM 14.51 MSVC_VERSION 19.51.36260.0
                              DOCS "${WORK_DIR}/readme.md" ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_redist_minimum_errors(MINIMUM 14.51 MSVC_VERSION 19.44.35211.0
                              DOCS "${WORK_DIR}/readme.md" ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_redist_minimum_errors(MINIMUM 14.51 MSVC_VERSION 19.52.36300 DOCS "${WORK_DIR}/readme.md"
                              ERRORS errors)
expect_item("${case}" "${errors}"
            "the build tools 14.52 are newer than the minimum VC++ Redistributable 14.51")
expect_count("${case}" "${errors}" 1)
rawreel_redist_minimum_errors(MINIMUM 14.51
                              DOCS "${WORK_DIR}/readme.md" "${WORK_DIR}/notes.md" ERRORS errors)
expect_item("${case}" "${errors}"
            "notes.md does not say \"Redistributable (x64) 14.51 or newer\"")
expect_count("${case}" "${errors}" 1)
rawreel_redist_minimum_errors(MINIMUM 14 MSVC_VERSION 20.1.0 DOCS "${WORK_DIR}/readme.md"
                              ERRORS errors)
expect_item("${case}" "${errors}" "the minimum VC++ Redistributable 14 is no 14.<minor> version")
expect_item("${case}" "${errors}" "the MSVC version 20.1.0 is no 19.<minor> version")
pass("${case}")

set(case "apt packages: the documents name the package list in one apt install line")
file(WRITE "${WORK_DIR}/apt-readme.md" "```sh\nsudo apt install liba1 libb2\n```\n")
file(WRITE "${WORK_DIR}/apt-notes.md" "`sudo apt install liba1`.\n")
rawreel_apt_packages_doc_errors(PACKAGES liba1 libb2 DOCS "${WORK_DIR}/apt-readme.md"
                                ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_apt_packages_doc_errors(PACKAGES liba1 libb2
                                DOCS "${WORK_DIR}/apt-readme.md" "${WORK_DIR}/apt-notes.md"
                                "${WORK_DIR}/apt-missing.md" ERRORS errors)
expect_item("${case}" "${errors}" "apt-notes.md does not say \"sudo apt install liba1 libb2\"")
expect_item("${case}" "${errors}" "apt-missing.md is missing")
expect_count("${case}" "${errors}" 2)
pass("${case}")

# --- package file names and FFmpeg library names per platform ---------------------------------

set(case "package names: Windows zip, Linux tar.xz, macOS dmg, unknown platform refused")
rawreel_package_files(VERSION 1.2.3 PLATFORM windows PACKAGE package SYMBOLS symbols CPACK cpack
                      EXTENSION extension GENERATOR generator ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${package}" "rawreel-1.2.3-windows-x64.zip")
expect_equal("${case}" "${symbols}" "rawreel-1.2.3-windows-x64-symbols.zip")
expect_equal("${case}" "${cpack}" "rawreel-1.2.3.zip")
expect_equal("${case}" "${extension}" "zip")
expect_equal("${case}" "${generator}" "ZIP")
rawreel_package_files(VERSION 1.2.3 PLATFORM linux PACKAGE package SYMBOLS symbols CPACK cpack
                      EXTENSION extension GENERATOR generator ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${package}" "rawreel-1.2.3-linux-x64.tar.xz")
expect_equal("${case}" "${symbols}" "rawreel-1.2.3-linux-x64-symbols.tar.xz")
expect_equal("${case}" "${cpack}" "rawreel-1.2.3.tar.xz")
expect_equal("${case}" "${extension}" "tar.xz")
expect_equal("${case}" "${generator}" "TXZ")
# The macOS package is no CPack output: make-packages.cmake builds it with hdiutil.
rawreel_package_files(VERSION 1.2.3 PLATFORM macos PACKAGE package SYMBOLS symbols CPACK cpack
                      EXTENSION extension GENERATOR generator ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${package}" "rawreel-1.2.3-macos-arm64.dmg")
expect_equal("${case}" "${symbols}" "rawreel-1.2.3-macos-arm64-symbols.zip")
expect_equal("${case}" "${cpack}" "")
expect_equal("${case}" "${extension}" "dmg")
expect_equal("${case}" "${generator}" "")
rawreel_package_files(VERSION 1.2.3 PLATFORM freebsd PACKAGE package SYMBOLS symbols CPACK cpack
                      EXTENSION extension GENERATOR generator ERRORS errors)
expect_item("${case}" "${errors}" "no package for the platform 'freebsd' (windows, linux, macos)")
expect_equal("${case}" "${package}" "")
pass("${case}")

set(case "package outputs: only the files of the run's package info, never older ones in OUT_DIR")
set(outputs_dir "${WORK_DIR}/outputs")
file(REMOVE_RECURSE "${outputs_dir}")
file(MAKE_DIRECTORY "${outputs_dir}")
file(WRITE "${outputs_dir}/rawreel-0.1.0-windows-x64.zip" "older run")
file(WRITE "${outputs_dir}/rawreel-0.1.0-windows-x64-symbols.zip" "older run")
file(WRITE "${outputs_dir}/package-info-windows.cmake"
     "set(RAWREEL_VERSION \"1.2.3\")\nset(RAWREEL_PACKAGE \"rawreel-1.2.3-windows-x64.zip\")\n"
     "set(RAWREEL_SYMBOLS \"rawreel-1.2.3-windows-x64-symbols.zip\")\n")
rawreel_package_outputs(INFO "${outputs_dir}/package-info-windows.cmake" FILES files)
expect_equal("${case}" "${files}"
             "rawreel-1.2.3-windows-x64.zip;rawreel-1.2.3-windows-x64-symbols.zip")
# The package info of an older rawreel still names its sources archive: it is not listed.
file(APPEND "${outputs_dir}/package-info-windows.cmake"
     "set(RAWREEL_SOURCES \"rawreel-1.2.3-sources.tar.xz\")\n")
rawreel_package_outputs(INFO "${outputs_dir}/package-info-windows.cmake" FILES files)
expect_equal("${case}" "${files}"
             "rawreel-1.2.3-windows-x64.zip;rawreel-1.2.3-windows-x64-symbols.zip")
pass("${case}")

set(case "FFmpeg library names: the file the loader opens and its major")
set(names "x/avutil-60.dll;x/avutil.dll;x/avutil-60.lib;x/avcodec-62.dll;x/libavutil.so.60"
          "x/libavutil.so.60.26.103;x/libavutil.so;x/libswscale.so.9;x/libavutil.60.dylib"
          "x/libavutil.60.26.103.dylib;x/libavutil.dylib;x/libswscale.9.dylib")
rawreel_ffmpeg_library_pattern(PLATFORM windows LIBRARY avutil GLOB glob REGEX regex)
expect_equal("${case}" "${glob}" "avutil-*.dll")
set(matched "${names}")
list(FILTER matched INCLUDE REGEX "${regex}")
expect_equal("${case}" "${matched}" "x/avutil-60.dll")
string(REGEX MATCH "${regex}" ignored "x/avutil-60.dll")
expect_equal("${case}" "${CMAKE_MATCH_1}" "60")
rawreel_ffmpeg_library_pattern(PLATFORM linux LIBRARY avutil GLOB glob REGEX regex)
expect_equal("${case}" "${glob}" "libavutil.so.*")
set(matched "${names}")
list(FILTER matched INCLUDE REGEX "${regex}")
expect_equal("${case}" "${matched}" "x/libavutil.so.60")
string(REGEX MATCH "${regex}" ignored "x/libavutil.so.60")
expect_equal("${case}" "${CMAKE_MATCH_1}" "60")
rawreel_ffmpeg_library_pattern(PLATFORM macos LIBRARY avutil GLOB glob REGEX regex)
expect_equal("${case}" "${glob}" "libavutil.*.dylib")
set(matched "${names}")
list(FILTER matched INCLUDE REGEX "${regex}")
expect_equal("${case}" "${matched}" "x/libavutil.60.dylib")
string(REGEX MATCH "${regex}" ignored "x/libavutil.60.dylib")
expect_equal("${case}" "${CMAKE_MATCH_1}" "60")
pass("${case}")

# --- the macOS app package: its manifest and write-manifest.cmake ------------------------------
# The app layout (plan Global Constraints): Contents/MacOS holds the two exes, Contents/Frameworks
# the Vulkan loader, KosmicKrisp and ffmpeg/ with the six FFmpeg libraries (major names),
# Contents/Resources the KosmicKrisp manifest and FFmpeg's texts; the .dmg root the documents.

set(ffmpeg_libraries avutil avcodec avformat avfilter swscale swresample)
set(app_fixed
    "ours|LICENSE" "ours|README.md" "notices|THIRD_PARTY_NOTICES.txt" "ours-bundled|SOURCE.txt"
    "link|Applications" "ours|rawreel.app/Contents/Info.plist" "ours|rawreel.app/Contents/MacOS/rawreel-cli"
    "ours|rawreel.app/Contents/MacOS/rawreel-gui"
    "ours|rawreel.app/Contents/_CodeSignature/CodeResources"
    "ours|rawreel.app/Contents/Resources/rawreel.icns"
    "ours|rawreel.app/Contents/Resources/vulkan/icd.d/libkosmickrisp_icd.json"
    "vulkan|rawreel.app/Contents/Frameworks/libvulkan.1.dylib"
    "vulkan|rawreel.app/Contents/Frameworks/libvulkan_kosmickrisp.dylib")

# Checks "<kind>|<path>" items against the layout: the fixed files, the six FFmpeg libraries in
# Frameworks/ffmpeg by their major names, FFmpeg's texts in Resources/ffmpeg, nothing else.
function(expect_app_layout case items)
    set(seen "")
    set(contents "rawreel\\.app/Contents")
    foreach(item IN LISTS items)
        if(item IN_LIST app_fixed)
            list(APPEND seen "${item}")
            continue()
        endif()
        if(item MATCHES "^ffmpeg\\|${contents}/Frameworks/ffmpeg/lib([a-z]+)\\.[0-9]+\\.dylib$")
            set(library "${CMAKE_MATCH_1}")
            if(library IN_LIST ffmpeg_libraries AND NOT "ffmpeg:${library}" IN_LIST seen)
                list(APPEND seen "ffmpeg:${library}")
                continue()
            endif()
        endif()
        if(item MATCHES "^ffmpeg\\|${contents}/Resources/ffmpeg/(build-info\\.txt|licenses/.+)$")
            continue()
        endif()
        message(FATAL_ERROR "FAIL ${case}: '${item}' is outside the app layout")
    endforeach()
    foreach(item IN LISTS app_fixed)
        if(NOT item IN_LIST seen)
            message(FATAL_ERROR "FAIL ${case}: '${item}' is missing")
        endif()
    endforeach()
    foreach(library IN LISTS ffmpeg_libraries)
        if(NOT "ffmpeg:${library}" IN_LIST seen)
            message(FATAL_ERROR "FAIL ${case}: lib${library}.<major>.dylib is missing")
        endif()
    endforeach()
endfunction()

set(case "macOS manifest: manifest-macos.txt matches the app layout")
set(macos_manifest "${CMAKE_CURRENT_LIST_DIR}/../../packaging/manifest-macos.txt")
if(NOT EXISTS "${macos_manifest}")
    message(FATAL_ERROR "FAIL ${case}: packaging/manifest-macos.txt does not exist")
endif()
file(STRINGS "${macos_manifest}" lines)
set(items "")
foreach(line IN LISTS lines)
    if(line MATCHES "^#" OR line STREQUAL "")
        continue()
    endif()
    if(NOT line MATCHES "^([a-z-]+) ([^ ]+)( [0-9a-f]+| /[^ ]+)?$")
        message(FATAL_ERROR "FAIL ${case}: bad line '${line}'")
    endif()
    list(APPEND items "${CMAKE_MATCH_1}|${CMAKE_MATCH_2}")
endforeach()
expect_app_layout("${case}" "${items}")
pass("${case}")

set(case "macOS manifest: write-manifest lists the app's libraries, driver and manifest")
set(dmg "${WORK_DIR}/dmg-root")
foreach(name IN ITEMS LICENSE README.md THIRD_PARTY_NOTICES.txt SOURCE.txt
                      rawreel.app/Contents/Info.plist rawreel.app/Contents/MacOS/rawreel-cli
                      rawreel.app/Contents/MacOS/rawreel-gui
                      rawreel.app/Contents/_CodeSignature/CodeResources
                      rawreel.app/Contents/Resources/rawreel.icns
                      rawreel.app/Contents/Resources/vulkan/icd.d/libkosmickrisp_icd.json
                      rawreel.app/Contents/Frameworks/libvulkan.1.dylib
                      rawreel.app/Contents/Frameworks/libvulkan_kosmickrisp.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libavutil.60.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libavcodec.62.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libavformat.62.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libavfilter.11.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libswscale.9.dylib
                      rawreel.app/Contents/Frameworks/ffmpeg/libswresample.6.dylib
                      rawreel.app/Contents/Resources/ffmpeg/build-info.txt
                      rawreel.app/Contents/Resources/ffmpeg/licenses/zlib/LICENSE)
    file(WRITE "${dmg}/${name}" "${name}")
endforeach()
file(CREATE_LINK "/Applications" "${dmg}/Applications" SYMBOLIC)
set(write_manifest "${CMAKE_CURRENT_LIST_DIR}/../../packaging/write-manifest.cmake")
function(run_write_manifest root out result_var error_var)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DROOT=${root}" -DVERSION=1.2.3 "-DOUT=${out}"
                            -DPLATFORM=macos -P "${write_manifest}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    string(REGEX REPLACE "[ \t\r\n]+" " " error "${error}")
    set(${result_var} "${result}" PARENT_SCOPE)
    set(${error_var} "${error}" PARENT_SCOPE)
endfunction()
set(written "${WORK_DIR}/manifest-macos-written.txt")
run_write_manifest("${dmg}" "${written}" result error)
expect_equal("${case}" "${result}" "0")
file(STRINGS "${written}" lines)
set(items "")
foreach(line IN LISTS lines)
    if(line MATCHES "^([a-z-]+) ([^ ]+)( ([0-9a-f]+|/[^ ]+))?$")
        set(kind "${CMAKE_MATCH_1}")
        set(path "${CMAKE_MATCH_2}")
        set(listed "${CMAKE_MATCH_4}")
        list(APPEND items "${kind}|${path}")
        if(kind MATCHES "^(ffmpeg|vulkan)$")
            file(SHA256 "${dmg}/${path}" hash)
            expect_equal("${case}" "${listed}" "${hash}")
        endif()
    endif()
endforeach()
expect_app_layout("${case}" "${items}")
foreach(item IN ITEMS "ffmpeg|rawreel.app/Contents/Frameworks/ffmpeg/libavutil.60.dylib"
                      "ffmpeg|rawreel.app/Contents/Frameworks/ffmpeg/libswresample.6.dylib"
                      "vulkan|rawreel.app/Contents/Frameworks/libvulkan.1.dylib"
                      "vulkan|rawreel.app/Contents/Frameworks/libvulkan_kosmickrisp.dylib"
                      "ours|rawreel.app/Contents/Resources/vulkan/icd.d/libkosmickrisp_icd.json")
    expect_item("${case}" "${items}" "${item}")
endforeach()
# The written manifest checks its own tree, and the OFF variant still needs the Vulkan files.
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED OFF
                       ERRORS errors)
expect_item("${case}" "${errors}"
            "unexpected file rawreel.app/Contents/Frameworks/ffmpeg/libavutil.60.dylib")
expect_item("${case}" "${errors}" "unexpected file SOURCE.txt")
expect_count("${case}" "${errors}" 9)
expect_item("${case}" "${lines}" "link Applications /Applications")
pass("${case}")

set(case "macOS manifest: the Applications link is a link to /Applications and nothing else is")
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON
                       ERRORS errors CLASSES classes)
expect_empty("${case}" "${errors}")
# The link is no file of any class: no check that reads files sees it.
foreach(item IN LISTS classes)
    if(item MATCHES "Applications")
        message(FATAL_ERROR "FAIL ${case}: the link is in the classes: ${item}")
    endif()
endforeach()
# Another target.
file(REMOVE "${dmg}/Applications")
file(CREATE_LINK "/Users" "${dmg}/Applications" SYMBOLIC)
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON ERRORS errors)
expect_equal("${case}" "${errors}" "symbolic link Applications points to /Users, not /Applications")
# A regular file in its place, and no link at all.
file(REMOVE "${dmg}/Applications")
file(WRITE "${dmg}/Applications" "not a link")
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON ERRORS errors)
expect_equal("${case}" "${errors}" "Applications is no symbolic link")
file(REMOVE "${dmg}/Applications")
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON ERRORS errors)
expect_equal("${case}" "${errors}" "missing file Applications")
# Another link is unexpected.
file(CREATE_LINK "/Applications" "${dmg}/Applications" SYMBOLIC)
file(CREATE_LINK "/Applications" "${dmg}/Extra" SYMBOLIC)
rawreel_check_manifest(ROOT "${dmg}" MANIFEST "${written}" VERSION 1.2.3 BUNDLED ON ERRORS errors)
expect_equal("${case}" "${errors}" "unexpected file Extra")
file(REMOVE "${dmg}/Extra")
pass("${case}")

set(case "macOS manifest: write-manifest refuses MoltenVK and libraries outside Frameworks")
foreach(stray IN ITEMS rawreel.app/Contents/Frameworks/libMoltenVK.dylib
                       rawreel.app/Contents/MacOS/libvulkan.1.dylib
                       rawreel.app/Contents/Resources/vulkan/icd.d/libvulkan_kosmickrisp.dylib
                       rawreel.app/Contents/Frameworks/ffmpeg/libavdevice.62.dylib)
    file(WRITE "${dmg}/${stray}" "stray")
    run_write_manifest("${dmg}" "${WORK_DIR}/manifest-stray.txt" result error)
    file(REMOVE "${dmg}/${stray}")
    if(result EQUAL 0)
        message(FATAL_ERROR "FAIL ${case}: ${stray} was accepted")
    endif()
    string(FIND "${error}" "${stray}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: the error does not name ${stray}: ${error}")
    endif()
endforeach()
file(REMOVE "${dmg}/SOURCE.txt")
run_write_manifest("${dmg}" "${WORK_DIR}/manifest-off.txt" result error)
string(FIND "${error}" "no bundled (ON) package" at)
if(result EQUAL 0 OR at EQUAL -1)
    message(FATAL_ERROR "FAIL ${case}: a package without SOURCE.txt was accepted: ${error}")
endif()
pass("${case}")

# --- Linux ELF checks (readelf and ldd text fixtures in data/) --------------------------------

set(data "${CMAKE_CURRENT_LIST_DIR}/data")
file(READ "${data}/readelf-d-exe.txt" exe_dynamic)
file(READ "${data}/readelf-d-library.txt" library_dynamic)
file(READ "${data}/readelf-V.txt" versions)
set(allowed libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6)

set(case "ELF: an ELF file is found by its magic, not by its name")
string(ASCII 127 del)
file(WRITE "${WORK_DIR}/elf/rawreel-cli" "${del}ELF rest")
file(WRITE "${WORK_DIR}/elf/libx.so.1" "not an ELF file")
file(WRITE "${WORK_DIR}/elf/short" "${del}E")
rawreel_is_elf(FILE "${WORK_DIR}/elf/rawreel-cli" RESULT elf)
expect_equal("${case}" "${elf}" "ON")
rawreel_is_elf(FILE "${WORK_DIR}/elf/libx.so.1" RESULT elf)
expect_equal("${case}" "${elf}" "OFF")
rawreel_is_elf(FILE "${WORK_DIR}/elf/short" RESULT elf)
expect_equal("${case}" "${elf}" "OFF")
pass("${case}")

set(case "ELF: readelf -d gives NEEDED, SONAME, RUNPATH and RPATH")
rawreel_readelf_dynamic(TEXT "${exe_dynamic}" NEEDED needed SONAME soname RUNPATH runpath
                        RPATH rpath)
expect_equal("${case}" "${needed}" "libstdc++.so.6;libm.so.6;libgcc_s.so.1;libc.so.6")
expect_equal("${case}" "${soname}" "")
expect_equal("${case}" "${runpath}" "")
expect_equal("${case}" "${rpath}" "")
rawreel_readelf_dynamic(TEXT "${library_dynamic}" NEEDED needed SONAME soname RUNPATH runpath
                        RPATH rpath)
expect_equal("${case}" "${needed}" "libavutil.so.60;libm.so.6;libc.so.6")
expect_equal("${case}" "${soname}" "libswresample.so.6")
expect_equal("${case}" "${runpath}" "$ORIGIN")
expect_equal("${case}" "${rpath}" "")
pass("${case}")

set(case "ELF: readelf -V gives the highest GLIBC_ and GLIBCXX_ versions, compared as numbers")
rawreel_readelf_max_versions(TEXT "${versions}" GLIBC glibc GLIBCXX glibcxx)
expect_equal("${case}" "${glibc}" "2.38")
expect_equal("${case}" "${glibcxx}" "3.4.32")
rawreel_readelf_max_versions(TEXT "no versions" GLIBC glibc GLIBCXX glibcxx)
expect_equal("${case}" "${glibc}" "")
expect_equal("${case}" "${glibcxx}" "")
pass("${case}")

set(elf_args ALLOWED ${allowed} GLIBC_MAX 2.39 GLIBCXX_MAX 3.4.33)
set(case "ELF: the good exe and library pass")
rawreel_elf_errors(NAME rawreel-cli KIND exe DYNAMIC "${exe_dynamic}" VERSIONS "${versions}"
                   ${elf_args} ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_elf_errors(NAME libswresample.so.6 KIND library DYNAMIC "${library_dynamic}"
                   VERSIONS "${versions}" ${elf_args} ERRORS errors)
expect_empty("${case}" "${errors}")
pass("${case}")

set(case "ELF: a symbol version above the baseline fails")
string(REPLACE "GLIBC_2.29" "GLIBC_2.99" bad "${versions}")
rawreel_elf_errors(NAME rawreel-cli KIND exe DYNAMIC "${exe_dynamic}" VERSIONS "${bad}"
                   ${elf_args} ERRORS errors)
expect_item("${case}" "${errors}" "rawreel-cli needs GLIBC_2.99, the baseline is 2.39")
expect_count("${case}" "${errors}" 1)
string(REPLACE "GLIBCXX_3.4.9 " "GLIBCXX_3.4.34 " bad "${versions}")
rawreel_elf_errors(NAME rawreel-cli KIND exe DYNAMIC "${exe_dynamic}" VERSIONS "${bad}"
                   ${elf_args} ERRORS errors)
expect_item("${case}" "${errors}" "rawreel-cli needs GLIBCXX_3.4.34, the baseline is 3.4.33")
expect_count("${case}" "${errors}" 1)
pass("${case}")

set(case "ELF: an exe with RUNPATH or RPATH fails")
string(REPLACE " 0x000000000000000c (INIT)"
               " 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN/x]\n 0x000000000000000c (INIT)"
               bad "${exe_dynamic}")
rawreel_elf_errors(NAME rawreel-cli KIND exe DYNAMIC "${bad}" VERSIONS "${versions}" ${elf_args}
                   ERRORS errors)
expect_item("${case}" "${errors}" "rawreel-cli has RUNPATH $ORIGIN/x (the exes need none)")
expect_count("${case}" "${errors}" 1)
string(REPLACE "RUNPATH)            Library runpath" "RPATH)              Library rpath" bad "${bad}")
rawreel_elf_errors(NAME rawreel-cli KIND exe DYNAMIC "${bad}" VERSIONS "${versions}" ${elf_args}
                   ERRORS errors)
expect_item("${case}" "${errors}" "rawreel-cli has RPATH $ORIGIN/x (the exes need none)")
expect_count("${case}" "${errors}" 1)
pass("${case}")

set(case "ELF: an exe that needs a soname outside the list or an FFmpeg library fails")
string(REPLACE "[libm.so.6]" "[libfoo.so.1]" bad "${exe_dynamic}")
rawreel_elf_errors(NAME rawreel-gui KIND exe DYNAMIC "${bad}" VERSIONS "${versions}" ${elf_args}
                   ERRORS errors)
expect_item("${case}" "${errors}" "rawreel-gui needs libfoo.so.1, which is not in the system list")
expect_count("${case}" "${errors}" 1)
string(REPLACE "[libm.so.6]" "[libavutil.so.60]" bad "${exe_dynamic}")
rawreel_elf_errors(NAME rawreel-gui KIND exe DYNAMIC "${bad}" VERSIONS "${versions}"
                   ALLOWED ${allowed} libavutil.so.60 GLIBC_MAX 2.39 GLIBCXX_MAX 3.4.33
                   ERRORS errors)
expect_item("${case}" "${errors}"
            "rawreel-gui links the FFmpeg library libavutil.so.60 (FFmpeg is loaded at run time)")
expect_count("${case}" "${errors}" 1)
pass("${case}")

set(case "ELF: a library without RUNPATH \$ORIGIN or named other than its SONAME fails")
rawreel_elf_errors(NAME libswresample.so.6.1.100 KIND library DYNAMIC "${library_dynamic}"
                   VERSIONS "${versions}" ${elf_args} ERRORS errors)
expect_item("${case}" "${errors}"
            "libswresample.so.6.1.100 is not named by its SONAME libswresample.so.6")
expect_count("${case}" "${errors}" 1)
string(REPLACE "[$ORIGIN]" "[$ORIGIN/../lib]" bad "${library_dynamic}")
rawreel_elf_errors(NAME libswresample.so.6 KIND library DYNAMIC "${bad}" VERSIONS "${versions}"
                   ${elf_args} ERRORS errors)
expect_item("${case}" "${errors}"
            "libswresample.so.6 has RUNPATH '$ORIGIN/../lib', expected exactly $ORIGIN")
expect_count("${case}" "${errors}" 1)
string(REPLACE " 0x000000000000001d (RUNPATH)            Library runpath: [$ORIGIN]\n" "" bad
               "${library_dynamic}")
string(REPLACE "(SONAME)             Library soname: [libswresample.so.6]" "(SYMBOLIC) 0x0" bad
               "${bad}")
rawreel_elf_errors(NAME libswresample.so.6 KIND library DYNAMIC "${bad}" VERSIONS "${versions}"
                   ${elf_args} ERRORS errors)
expect_item("${case}" "${errors}" "libswresample.so.6 has RUNPATH '', expected exactly $ORIGIN")
expect_item("${case}" "${errors}" "libswresample.so.6 has no SONAME")
expect_count("${case}" "${errors}" 2)
pass("${case}")

set(case "ELF: ldd 'not found' lines fail")
file(READ "${data}/ldd.txt" ldd)
rawreel_ldd_errors(NAME libavformat.so.62 TEXT "${ldd}" ERRORS errors)
expect_empty("${case}" "${errors}")
string(REPLACE "libavutil.so.60 => /opt/pkg/rawreel-1.2.3/ffmpeg/libavutil.so.60 (0x0000744b2e800000)"
               "libavutil.so.60 => not found" bad "${ldd}")
string(REPLACE "libm.so.6 => /lib/x86_64-linux-gnu/libm.so.6 (0x0000744b2eb17000)"
               "libm.so.6 => not found" bad "${bad}")
rawreel_ldd_errors(NAME libavformat.so.62 TEXT "${bad}" ERRORS errors)
expect_item("${case}" "${errors}" "libavformat.so.62: ldd finds no libavutil.so.60")
expect_item("${case}" "${errors}" "libavformat.so.62: ldd finds no libm.so.6")
expect_count("${case}" "${errors}" 2)
pass("${case}")

set(case "no runtime in the tar: C/C++ runtime and Vulkan libraries, .so outside ffmpeg/")
set(rt_tree "${WORK_DIR}/rt-tree")
file(WRITE "${rt_tree}/rawreel-1.2.3/rawreel-gui" "exe")
file(WRITE "${rt_tree}/rawreel-1.2.3/ffmpeg/libavutil.so.60" "library")
file(WRITE "${rt_tree}/rawreel-1.2.3/ffmpeg/licenses/zlib/LICENSE" "text")
rawreel_linux_runtime_files(ROOT "${rt_tree}" FILES found)
expect_empty("${case}" "${found}")
foreach(name IN ITEMS libbogus.so ffmpeg/libstdc++.so.6 ffmpeg/libgcc_s.so.1 ffmpeg/libc.so.6
                      ffmpeg/libvulkan.so.1 lib/libx.so.2)
    file(WRITE "${rt_tree}/rawreel-1.2.3/${name}" "runtime")
endforeach()
rawreel_linux_runtime_files(ROOT "${rt_tree}" FILES found)
expect_count("${case}" "${found}" 6)
expect_item("${case}" "${found}" "rawreel-1.2.3/libbogus.so")
expect_item("${case}" "${found}" "rawreel-1.2.3/ffmpeg/libvulkan.so.1")
expect_item("${case}" "${found}" "rawreel-1.2.3/lib/libx.so.2")
pass("${case}")

set(case "symbols: build ID, debuglink and compressed debug sections")
file(READ "${data}/readelf-n-and-debuglink.txt" notes)
rawreel_readelf_build_id(TEXT "${notes}" ID id)
expect_equal("${case}" "${id}" "de9410499bc003859e683e1d504a5e708d8d24d9")
rawreel_readelf_build_id(TEXT "no notes" ID id)
expect_equal("${case}" "${id}" "")
rawreel_readelf_debuglink(TEXT "${notes}" NAME link)
expect_equal("${case}" "${link}" "rawreel-cli.debug")
rawreel_readelf_debuglink(TEXT "no dump" NAME link)
expect_equal("${case}" "${link}" "")
file(READ "${data}/readelf-S.txt" sections)
rawreel_readelf_compressed_sections(TEXT "${sections}" SECTIONS compressed)
expect_empty("${case}" "${compressed}")
string(REPLACE "1465763 00      0" "1465763 00   C  0" bad "${sections}")
string(REPLACE "69c389 01  MS  0" "69c389 01 MSC  0" bad "${bad}")
rawreel_readelf_compressed_sections(TEXT "${bad}" SECTIONS compressed)
expect_equal("${case}" "${compressed}" ".debug_info;.debug_str")
pass("${case}")

# --- Linux tar modes and owners (GNU tar tvf text) -------------------------------------------

set(case "modes: a good listing passes and reports its owners")
file(READ "${data}/tar-tvf.txt" listing)
set(executables rawreel-0.9.0/rawreel-gui rawreel-0.9.0/ffmpeg/libavutil.so.60)
rawreel_tar_listing_errors(TEXT "${listing}" EXECUTABLES ${executables}
                           ERRORS errors OWNER_NAMES owners)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${owners}" "builder/staff")
pass("${case}")

set(case "modes: a 777 file, a 644 exe or a world-writable folder fails")
string(REPLACE "-rw-r--r-- builder/staff  1083" "-rwxrwxrwx builder/staff  1083" bad "${listing}")
string(REPLACE "-rwxr-xr-x builder/staff 9000000" "-rw-r--r-- builder/staff 9000000" bad "${bad}")
string(REPLACE "drwxr-xr-x builder/staff     0 2026-10-27 10:00 rawreel-0.9.0/ffmpeg/"
               "drwxrwxrwx builder/staff     0 2026-10-27 10:00 rawreel-0.9.0/ffmpeg/" bad "${bad}")
rawreel_tar_listing_errors(TEXT "${bad}" EXECUTABLES ${executables} ERRORS errors OWNER_NAMES owners)
expect_count("${case}" "${errors}" 3)
expect_item("${case}" "${errors}" "rawreel-0.9.0/LICENSE: mode rwxrwxrwx, expected rw-r--r--")
expect_item("${case}" "${errors}" "rawreel-0.9.0/rawreel-gui: mode rw-r--r--, expected rwxr-xr-x")
expect_item("${case}" "${errors}" "rawreel-0.9.0/ffmpeg/: world-writable folder (rwxrwxrwx)")
pass("${case}")

set(case "modes: links, unreadable lines and an empty listing fail")
set(bad "${listing}lrwxrwxrwx builder/staff 0 2026-10-27 10:00 rawreel-0.9.0/ffmpeg/libx.so -> libavutil.so.60\n")
string(APPEND bad "garbage line\n")
rawreel_tar_listing_errors(TEXT "${bad}" EXECUTABLES ${executables} ERRORS errors OWNER_NAMES owners)
expect_count("${case}" "${errors}" 2)
expect_item("${case}" "${errors}"
            "rawreel-0.9.0/ffmpeg/libx.so -> libavutil.so.60: entry type l (only files and folders)")
expect_item("${case}" "${errors}" "cannot read the tar listing line: garbage line")
rawreel_tar_listing_errors(TEXT "\n" ERRORS errors OWNER_NAMES owners)
expect_equal("${case}" "${errors}" "the tar listing is empty")
pass("${case}")

set(case "owners: only the allowed owners and groups pass when they are checked")
rawreel_tar_listing_errors(TEXT "${listing}" EXECUTABLES ${executables} OWNERS root runner
                           GROUPS root runner docker ERRORS errors OWNER_NAMES owners)
expect_count("${case}" "${errors}" 2)
expect_item("${case}" "${errors}" "owner builder is not allowed (allowed: root, runner)")
expect_item("${case}" "${errors}" "group staff is not allowed (allowed: root, runner, docker)")
string(REPLACE "builder/staff" "runner/docker" good "${listing}")
string(REPLACE "-rw-r--r-- runner/docker  1083" "-rw-r--r-- root/root  1083" good "${good}")
rawreel_tar_listing_errors(TEXT "${good}" EXECUTABLES ${executables} OWNERS root runner
                           GROUPS root runner docker ERRORS errors OWNER_NAMES owners)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${owners}" "runner/docker;root/root")
pass("${case}")

# --- macOS Mach-O checks (otool, codesign and dyld text fixtures in data/) ---------------------
# The fixtures come from the app in a mounted .dmg; the bundle path is the smoke's mount point
# below a neutral folder.

file(READ "${data}/otool-L-exe.txt" otool_exe)
file(READ "${data}/otool-L-library.txt" otool_library)
file(READ "${data}/otool-l-load-commands.txt" load_commands)
set(exe_rpath "@executable_path/../Frameworks")

set(case "Mach-O: a Mach-O file is found by its magic, not by its name")
string(ASCII 207 250 237 254 thin_magic)
string(ASCII 202 254 186 190 fat_magic)
file(WRITE "${WORK_DIR}/macho/rawreel-cli" "${thin_magic}rest")
file(WRITE "${WORK_DIR}/macho/libx.dylib" "${fat_magic}rest")
file(WRITE "${WORK_DIR}/macho/liby.dylib" "not a Mach-O file")
file(WRITE "${WORK_DIR}/macho/short" "${del}E")
rawreel_is_macho(FILE "${WORK_DIR}/macho/rawreel-cli" RESULT macho)
expect_equal("${case}" "${macho}" "ON")
rawreel_is_macho(FILE "${WORK_DIR}/macho/libx.dylib" RESULT macho)
expect_equal("${case}" "${macho}" "ON")
rawreel_is_macho(FILE "${WORK_DIR}/macho/liby.dylib" RESULT macho)
expect_equal("${case}" "${macho}" "OFF")
rawreel_is_macho(FILE "${WORK_DIR}/macho/short" RESULT macho)
expect_equal("${case}" "${macho}" "OFF")
pass("${case}")

set(case "Mach-O: otool -L gives the dependencies (a library's own install name first)")
rawreel_otool_dependencies(TEXT "${otool_exe}" DEPS deps)
expect_count("${case}" "${deps}" 18)
list(GET deps 0 first)
expect_equal("${case}" "${first}"
             "/System/Library/Frameworks/AudioToolbox.framework/Versions/A/AudioToolbox")
expect_item("${case}" "${deps}" "/usr/lib/libc++.1.dylib")
rawreel_otool_dependencies(TEXT "${otool_library}" DEPS deps)
list(GET deps 0 first)
expect_equal("${case}" "${first}" "@rpath/libavfilter.11.14.103.dylib")
expect_item("${case}" "${deps}" "@rpath/libavutil.60.dylib")
expect_item("${case}" "${deps}" "/usr/lib/libobjc.A.dylib")
string(CONCAT weak "x (architecture arm64):\n"
              "\t/a b/c.dylib (compatibility version 1.0.0, current version 1.0.0, weak)\n")
rawreel_otool_dependencies(TEXT "${weak}" DEPS deps)
expect_equal("${case}" "${deps}" "/a b/c.dylib")
pass("${case}")

set(case "Mach-O: otool -l gives the rpaths, the minimum macOS version and the UUID")
rawreel_otool_load_commands(TEXT "${load_commands}" RPATHS rpaths MINOS minos UUID uuid)
expect_equal("${case}" "${rpaths}" "${exe_rpath}")
expect_equal("${case}" "${minos}" "27.0")
expect_equal("${case}" "${uuid}" "DDDBC8EE-137C-3F33-933B-22C0E6FE591B")
string(CONCAT old_style "Load command 8\n      cmd LC_VERSION_MIN_MACOSX\n  cmdsize 16\n"
              "  version 10.13\n      sdk 10.14\n")
rawreel_otool_load_commands(TEXT "${old_style}" RPATHS rpaths MINOS minos)
expect_equal("${case}" "${rpaths}" "")
expect_equal("${case}" "${minos}" "10.13")
rawreel_otool_load_commands(TEXT "no load commands" RPATHS rpaths MINOS minos UUID uuid)
expect_equal("${case}" "${minos}" "")
expect_equal("${case}" "${uuid}" "")
pass("${case}")

# An exe's otool -l with one more LC_RPATH after the app's.
string(CONCAT extra_rpath "${load_commands}" "Load command 21\n          cmd LC_RPATH\n"
              "      cmdsize 32\n         path /opt/homebrew/lib (offset 12)\n")
rawreel_otool_dependencies(TEXT "${otool_exe}" DEPS exe_deps)
rawreel_otool_dependencies(TEXT "${otool_library}" DEPS library_deps)

set(case "Mach-O: the clean exe and library pass")
rawreel_macho_errors(NAME rawreel-gui KIND exe DEPS ${exe_deps} RPATHS "${exe_rpath}" MINOS 27.0
                     ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_macho_errors(NAME libavfilter.11.dylib KIND library DEPS ${library_deps}
                     RPATHS "@loader_path" MINOS 27.0 PREBUILT ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_macho_errors(NAME libvulkan.1.dylib KIND library DEPS "@rpath/libvulkan.1.dylib"
                     "/usr/lib/libSystem.B.dylib" MINOS 27.0 ERRORS errors)
expect_empty("${case}" "${errors}")
# A prebuilt third-party file may target an older macOS (the SDK's KosmicKrisp: minos 11.0).
rawreel_macho_errors(NAME libvulkan_kosmickrisp.dylib KIND library
                     DEPS "@rpath/libvulkan_kosmickrisp.dylib" "/usr/lib/libz.1.dylib"
                     MINOS 11.0 PREBUILT ERRORS errors)
expect_empty("${case}" "${errors}")
pass("${case}")

set(case "Mach-O: an exe with /opt/homebrew/lib in its rpaths or without the app's rpath fails")
rawreel_otool_load_commands(TEXT "${extra_rpath}" RPATHS rpaths MINOS minos)
expect_equal("${case}" "${rpaths}" "${exe_rpath};/opt/homebrew/lib")
rawreel_macho_errors(NAME rawreel-cli KIND exe DEPS ${exe_deps} RPATHS ${rpaths} MINOS ${minos}
                     ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli has the rpath /opt/homebrew/lib (expected only ${exe_rpath})")
rawreel_macho_errors(NAME rawreel-cli KIND exe DEPS ${exe_deps} MINOS 27.0 ERRORS errors)
expect_equal("${case}" "${errors}" "rawreel-cli lacks the rpath ${exe_rpath}")
# Exactly one LC_RPATH: the app's one twice fails too.
rawreel_macho_errors(NAME rawreel-cli KIND exe DEPS ${exe_deps} RPATHS "${exe_rpath}"
                     "${exe_rpath}" MINOS 27.0 ERRORS errors)
expect_equal("${case}" "${errors}" "rawreel-cli has 2 rpaths (expected exactly one, ${exe_rpath})")
pass("${case}")

set(case "Mach-O: an exe that links FFmpeg or a library outside the system fails")
rawreel_macho_errors(NAME rawreel-gui KIND exe DEPS ${exe_deps} "@rpath/libavutil.60.dylib"
                     "/opt/homebrew/lib/libwx_baseu-3.3.dylib" RPATHS "${exe_rpath}" MINOS 27.0
                     ERRORS errors)
expect_item("${case}" "${errors}"
            "rawreel-gui links the FFmpeg library @rpath/libavutil.60.dylib (FFmpeg is loaded at run time)")
expect_item("${case}" "${errors}"
            "rawreel-gui depends on /opt/homebrew/lib/libwx_baseu-3.3.dylib (outside the bundle and the system)")
expect_count("${case}" "${errors}" 2)
pass("${case}")

set(case "Mach-O: a library that depends on /usr/local/lib/libvulkan.1.dylib fails")
string(REPLACE "@rpath/libavutil.60.dylib" "/usr/local/lib/libvulkan.1.dylib" bad "${otool_library}")
rawreel_otool_dependencies(TEXT "${bad}" DEPS deps)
rawreel_macho_errors(NAME libavfilter.11.dylib KIND library DEPS ${deps} RPATHS "@loader_path"
                     MINOS 27.0 PREBUILT ERRORS errors)
expect_equal("${case}" "${errors}"
             "libavfilter.11.dylib depends on /usr/local/lib/libvulkan.1.dylib (outside the bundle and the system)")
rawreel_macho_errors(NAME libavfilter.11.dylib KIND library DEPS ${library_deps}
                     RPATHS "@loader_path" "/opt/homebrew/lib" MINOS 27.0 PREBUILT ERRORS errors)
expect_equal("${case}" "${errors}"
             "libavfilter.11.dylib has the rpath /opt/homebrew/lib (a library's rpaths start with @loader_path)")
# @executable_path/ is for the exes only: a library loaded by another program would miss it.
rawreel_macho_errors(NAME libvulkan.1.dylib KIND library DEPS "@rpath/libvulkan.1.dylib"
                     "@executable_path/../Frameworks/libx.dylib" MINOS 27.0 ERRORS errors)
expect_equal("${case}" "${errors}"
             "libvulkan.1.dylib depends on @executable_path/../Frameworks/libx.dylib (outside the bundle and the system)")
pass("${case}")

set(case "Mach-O: minos 28.0 fails; a file built from source has exactly 27.0")
string(REPLACE "minos 27.0" "minos 28.0" bad "${load_commands}")
rawreel_otool_load_commands(TEXT "${bad}" RPATHS rpaths MINOS minos)
rawreel_macho_errors(NAME rawreel-cli KIND exe DEPS ${exe_deps} RPATHS ${rpaths} MINOS ${minos}
                     ERRORS errors)
expect_equal("${case}" "${errors}" "rawreel-cli needs macOS 28.0, above the minimum 27.0")
rawreel_macho_errors(NAME libavutil.60.dylib KIND library DEPS "@rpath/libavutil.60.dylib"
                     MINOS 28.0 PREBUILT ERRORS errors)
expect_equal("${case}" "${errors}" "libavutil.60.dylib needs macOS 28.0, above the minimum 27.0")
rawreel_macho_errors(NAME libvulkan.1.dylib KIND library DEPS "@rpath/libvulkan.1.dylib"
                     MINOS 26.0 ERRORS errors)
expect_equal("${case}" "${errors}"
             "libvulkan.1.dylib has minos 26.0, expected 27.0 (a file built from source)")
rawreel_macho_errors(NAME libvulkan.1.dylib KIND library DEPS "@rpath/libvulkan.1.dylib"
                     ERRORS errors)
expect_equal("${case}" "${errors}" "libvulkan.1.dylib names no minimum macOS version")
pass("${case}")

set(case "codesign: ad hoc, the vendor's signature, the linker's signature only")
file(READ "${data}/codesign-adhoc.txt" adhoc)
file(READ "${data}/codesign-vendor.txt" vendor)
rawreel_codesign_errors(NAME rawreel-cli TEXT "${adhoc}" KIND adhoc
                        IDENTIFIER io.github.barabashkad.rawreel.cli ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_codesign_errors(NAME rawreel-cli TEXT "${adhoc}" KIND adhoc IDENTIFIER rawreel-cli
                        ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli has the identifier io.github.barabashkad.rawreel.cli, expected rawreel-cli")
rawreel_codesign_errors(NAME libvulkan_kosmickrisp.dylib TEXT "${vendor}" KIND vendor ERRORS errors)
expect_empty("${case}" "${errors}")
rawreel_codesign_errors(NAME libvulkan_kosmickrisp.dylib TEXT "${vendor}" KIND adhoc ERRORS errors)
expect_equal("${case}" "${errors}"
             "libvulkan_kosmickrisp.dylib is not signed ad hoc (Authority=Developer ID Application: LunarG, Inc. (4777T3625D))")
rawreel_codesign_errors(NAME rawreel-cli TEXT "${adhoc}" KIND vendor ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli has no vendor signature (no Developer ID Authority chain)")
string(REPLACE "flags=0x2(adhoc)" "flags=0x20002(adhoc,linker-signed)" linker "${adhoc}")
rawreel_codesign_errors(NAME rawreel-cli TEXT "${linker}" KIND adhoc ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli carries only the linker's signature (not signed after its last change)")
rawreel_codesign_errors(NAME x.dylib TEXT "x.dylib: code object is not signed at all\n" KIND adhoc
                        ERRORS errors)
expect_equal("${case}" "${errors}" "x.dylib is not signed")
pass("${case}")

file(READ "${data}/dyld-print-libraries.txt" trace)
set(bundle "/private/tmp/smoke/rawreel 2/Загрузки/rawreel.app")
set(icd "${bundle}/Contents/Resources/vulkan/icd.d/libkosmickrisp_icd.json")

set(case "loaded images: the bundle's and the system's pass")
rawreel_loaded_image_errors(NAME rawreel-gui TEXT "${trace}" BUNDLE "${bundle}" ERRORS errors
                            IMAGES images)
expect_empty("${case}" "${errors}")
expect_count("${case}" "${images}" 22)
expect_item("${case}" "${images}" "${bundle}/Contents/Frameworks/libvulkan_kosmickrisp.dylib")
expect_item("${case}" "${images}" "${bundle}/Contents/MacOS/rawreel-gui")
pass("${case}")

set(case "loaded images: /usr/local/lib/libMoltenVK.dylib and other outside images fail")
set(line "dyld[7085]: <0E4CF3E4-B0C2-312A-B147-0BD4978EF0B7>")
string(CONCAT bad "${trace}" "${line} /usr/local/lib/libMoltenVK.dylib\n"
              "${line} ${bundle}/../libvulkan.1.dylib\n" "${line} ${bundle}.old/x.dylib\n")
rawreel_loaded_image_errors(NAME rawreel-gui TEXT "${bad}" BUNDLE "${bundle}" ERRORS errors)
expect_item("${case}" "${errors}"
            "rawreel-gui loads /usr/local/lib/libMoltenVK.dylib (outside the bundle, /usr/lib and /System)")
expect_item("${case}" "${errors}"
            "rawreel-gui loads ${bundle}/../libvulkan.1.dylib (outside the bundle, /usr/lib and /System)")
expect_item("${case}" "${errors}"
            "rawreel-gui loads ${bundle}.old/x.dylib (outside the bundle, /usr/lib and /System)")
expect_count("${case}" "${errors}" 3)
# A dyld line glued to another writer's text is still audited; a path that is not absolute is
# reported.
string(CONCAT bad "${trace}" "INFO  selftest: passed [app.cpp:106]"
              "${line} /usr/local/lib/libMoltenVK.dylib\n" "${line} libfoo.dylib\n")
rawreel_loaded_image_errors(NAME rawreel-gui TEXT "${bad}" BUNDLE "${bundle}" ERRORS errors)
expect_item("${case}" "${errors}"
            "rawreel-gui loads /usr/local/lib/libMoltenVK.dylib (outside the bundle, /usr/lib and /System)")
expect_item("${case}" "${errors}"
            "rawreel-gui loads libfoo.dylib (no absolute path)")
expect_count("${case}" "${errors}" 2)
rawreel_loaded_image_errors(NAME rawreel-cli TEXT "no trace\n" BUNDLE "${bundle}" ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli: no loaded image in the output (is DYLD_PRINT_LIBRARIES=1 honoured?)")
pass("${case}")

set(case "Vulkan driver: the loader finds only the bundled manifest")
rawreel_vulkan_driver_errors(NAME rawreel-gui TEXT "${trace}" MANIFEST "${icd}" ERRORS errors)
expect_empty("${case}" "${errors}")
string(CONCAT bad "${trace}" "[Vulkan Loader] DRIVER:         Found ICD manifest file "
              "/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json, version 1.0.0\n")
rawreel_vulkan_driver_errors(NAME rawreel-gui TEXT "${bad}" MANIFEST "${icd}" ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-gui: the Vulkan loader found the driver manifest /opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json (not the bundled one)")
rawreel_vulkan_driver_errors(NAME rawreel-cli TEXT "no loader output\n" MANIFEST "${icd}"
                             ERRORS errors)
expect_equal("${case}" "${errors}"
             "rawreel-cli: the Vulkan loader names no driver manifest (is VK_LOADER_DEBUG=driver honoured?)")
pass("${case}")

# --- dry-run.cmake arguments (each run stops before any build) ---------------------------------
# Every run gets a scratch DRYRUN_ROOT, so a broken check never touches the developer's dry-run
# area.

function(run_dry_run case script expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DDRYRUN_ROOT=${WORK_DIR}/dryrun" ${ARGN}
                            -P "${script}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(result EQUAL 0)
        message(FATAL_ERROR "FAIL ${case}: the dry run started")
    endif()
    # CMake wraps the message text: compare with the white space collapsed.
    string(REGEX REPLACE "[ \t\r\n]+" " " err "${err}")
    string(FIND "${err}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: expected '${expected}', got: ${err}")
    endif()
endfunction()

set(dry_run "${CMAKE_CURRENT_LIST_DIR}/../../packaging/dry-run.cmake")
cmake_path(GET CMAKE_CURRENT_LIST_DIR PARENT_PATH tests_dir)
cmake_path(GET tests_dir PARENT_PATH clone_dir)
cmake_path(GET clone_dir FILENAME clone_name)

set(case "dry-run: a WORKSPACE_ROOT that does not hold this clone is refused")
# A folder of the clone's own name, so the real-path comparison is what refuses the root.
file(MAKE_DIRECTORY "${WORK_DIR}/other-root/${clone_name}")
run_dry_run("${case}" "${dry_run}" "is not the folder holding this clone"
            "-DWORKSPACE_ROOT=${WORK_DIR}/other-root" -DBUNDLE=OFF)
pass("${case}")

# A clone of another name without libs/: an accepted root stops at the submodule check.
set(fake "${WORK_DIR}/ws/any-name")
file(COPY "${dry_run}" DESTINATION "${fake}/packaging")
set(fake_dry_run "${fake}/packaging/dry-run.cmake")
set(missing "any-name/libs/dvv-build is missing")

set(case "dry-run: the default root (the clone's parent) is accepted for any clone name")
run_dry_run("${case}" "${fake_dry_run}" "${missing}" -DBUNDLE=OFF)
pass("${case}")

set(case "dry-run: the clone's parent as WORKSPACE_ROOT is accepted, with or without a slash")
run_dry_run("${case}" "${fake_dry_run}" "${missing}" "-DWORKSPACE_ROOT=${WORK_DIR}/ws"
            -DBUNDLE=OFF)
run_dry_run("${case}" "${fake_dry_run}" "${missing}" "-DWORKSPACE_ROOT=${WORK_DIR}/ws/"
            -DBUNDLE=OFF)
pass("${case}")

if(CMAKE_HOST_APPLE)
    set(case "dry-run: macOS refuses an unknown GUI_SMOKE before the build")
    file(MAKE_DIRECTORY "${fake}/libs/dvv-build/deps")
    run_dry_run("${case}" "${fake_dry_run}" "GUI_SMOKE must be off or macos" -DBUNDLE=OFF
                -DGUI_SMOKE=linux)
    pass("${case}")
endif()

# --- SOURCE.txt: the custom_ffmpeg tag of each package -------------------------------------

set(case "SOURCE.txt: the custom_ffmpeg tags it names")
set(source_tail "The complete corresponding source of FFmpeg is the tag v1.0.1 of the
custom_ffmpeg repository, with its submodules and build scripts (the configure line included):
https://github.com/BarabashkaD/custom_ffmpeg/tree/v1.0.1

LAME 3.100 (LGPL-2.0-or-later) is built into avcodec. That tag does not hold its source.
")
rawreel_source_txt_versions(TEXT "${source_tail}" VERSIONS named)
expect_equal("${case}" "${named}" "1.0.1")
rawreel_source_txt_versions(TEXT "see https://github.com/x/custom_ffmpeg/tree/v1.0.0. And\nhttps://github.com/x/custom_ffmpeg/tree/v1.0.1)" VERSIONS named)
expect_equal("${case}" "${named}" "1.0.0;1.0.1")
rawreel_source_txt_versions(TEXT "https://github.com/x/rawreel/releases/tag/v1.0.1" VERSIONS named)
expect_empty("${case}" "${named}")
pass("${case}")

# The rendered SOURCE.txt of each platform (packaging/source-txt.cmake).
function(render_source_txt platform custom_ffmpeg_version text_var)
    set(url "https://github.com/BarabashkaD/custom_ffmpeg/releases/download/v${custom_ffmpeg_version}/x.tar.xz")
    set(lame_url "https://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz")
    set(sha "ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e")
    set(out "${WORK_DIR}/source-txt-${platform}/SOURCE.txt")
    rawreel_render_source_txt(
        OUT "${out}" PLATFORM "${platform}" CUSTOM_FFMPEG_VERSION "${custom_ffmpeg_version}"
        CUSTOM_FFMPEG_URL "${url}" FFMPEG_VERSION "n8.1.3"
        LIBRARY_VERSIONS "avutil 60.26, avcodec 62.28, avformat 62.12, avfilter 11.14, swscale 9.5, swresample 6.3"
        LAME_URL "${lame_url}" LAME_SHA256 "${sha}"
        FFMPEG_DIR "rawreel.app/Contents/Frameworks/ffmpeg"
        TEXTS_DIR "rawreel.app/Contents/Resources/ffmpeg")
    file(READ "${out}" text)
    set(${text_var} "${text}" PARENT_SCOPE)
endfunction()

function(expect_text_has case text needle)
    string(FIND "${text}" "${needle}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: expected the text '${needle}' in:\n${text}")
    endif()
endfunction()

function(expect_text_lacks case text needle)
    string(FIND "${text}" "${needle}" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: unexpected the text '${needle}' in:\n${text}")
    endif()
endfunction()

foreach(spec IN ITEMS "windows|1.0.0" "linux|1.0.0" "macos|1.0.1")
    string(REPLACE "|" ";" spec "${spec}")
    list(GET spec 0 platform)
    list(GET spec 1 version)
    set(case "SOURCE.txt rendering: ${platform}")
    render_source_txt("${platform}" "${version}" text)
    rawreel_source_txt_versions(TEXT "${text}" VERSIONS named)
    expect_equal("${case}" "${named}" "${version}")
    expect_text_has("${case}" "${text}" "corresponding source of FFmpeg is the tag v${version} of the")
    # LAME: its own source release (URL and SHA256 printed), never the tag.
    expect_text_has("${case}" "${text}" "That tag does not hold its source")
    expect_text_has("${case}" "${text}" "lame-3.100.tar.gz:\nhttps://downloads.sourceforge.net/project/lame/lame/3.100/lame-3.100.tar.gz\n")
    expect_text_has("${case}" "${text}" "SHA256 ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e")
    expect_text_lacks("${case}" "${text}" "LGPL components")
    expect_text_lacks("${case}" "${text}" "FFmpeg and LAME")
    expect_text_lacks("${case}" "${text}" "LAME 3.100 is built from")
    # No line is over 100 columns.
    file(STRINGS "${WORK_DIR}/source-txt-${platform}/SOURCE.txt" lines)
    foreach(line IN LISTS lines)
        string(LENGTH "${line}" length)
        if(length GREATER 100)
            message(FATAL_ERROR "FAIL ${case}: a line of ${length} columns: ${line}")
        endif()
    endforeach()
    if(platform STREQUAL "macos")
        expect_text_lacks("${case}" "${text}" "AMF")
        expect_text_has("${case}" "${text}" "codesign --force --sign - <library>")
        expect_text_has("${case}" "${text}" "licenses folder of rawreel.app/Contents/Resources/ffmpeg,")
    else()
        expect_text_has("${case}" "${text}" "Not included: the AMD AMF public headers")
        expect_text_lacks("${case}" "${text}" "codesign")
        expect_text_has("${case}" "${text}" "in this folder")
    endif()
    pass("${case}")
endforeach()

set(case "SOURCE.txt: exactly the tag of the prefix's custom_ffmpeg release")
rawreel_ffmpeg_source_errors(NAME p.dmg CUSTOM_FFMPEG_VERSION 1.0.1 NAMED 1.0.1 ERRORS e)
expect_empty("${case}" "${e}")
rawreel_ffmpeg_source_errors(NAME p.dmg CUSTOM_FFMPEG_VERSION 1.0.1 NAMED 1.0.0 ERRORS e)
expect_equal("${case}" "${e}" "p.dmg: SOURCE.txt names the custom_ffmpeg tags '1.0.0', expected exactly v1.0.1 (the release its prefix pins)")
rawreel_ffmpeg_source_errors(NAME p.dmg CUSTOM_FFMPEG_VERSION 1.0.1 ERRORS e)
expect_equal("${case}" "${e}" "p.dmg: SOURCE.txt names the custom_ffmpeg tags '', expected exactly v1.0.1 (the release its prefix pins)")
rawreel_ffmpeg_source_errors(NAME p.dmg CUSTOM_FFMPEG_VERSION 1.0.1 NAMED 1.0.1 1.0.0 ERRORS e)
expect_count("${case}" "${e}" 1)
# No version (an empty value is the same as none).
rawreel_ffmpeg_source_errors(NAME p.zip NAMED 1.0.0 ERRORS e)
expect_equal("${case}" "${e}" "p.zip: the third-party prefix names no custom_ffmpeg version")
pass("${case}")

# check-release-ffmpeg.cmake over the package infos of a release; each platform its own release.
set(release_check "${CMAKE_CURRENT_LIST_DIR}/../../packaging/check-release-ffmpeg.cmake")
function(release_infos dist windows linux macos)
    file(REMOVE_RECURSE "${dist}")
    foreach(entry IN ITEMS "windows|zip|1.0.0|${windows}" "linux|tar.xz|1.0.0|${linux}"
                           "macos|dmg|1.0.1|${macos}")
        string(REPLACE "|" ";" entry "${entry}")
        list(GET entry 0 platform)
        list(GET entry 1 extension)
        list(GET entry 2 cf)
        list(GET entry 3 named)
        string(REPLACE "," ";" named "${named}")
        file(WRITE "${dist}/package-info-${platform}.cmake"
             "set(RAWREEL_PACKAGE \"rawreel-1.2.3-${platform}.${extension}\")\n"
             "set(RAWREEL_BUNDLED ON)\nset(RAWREEL_CUSTOM_FFMPEG_VERSION \"${cf}\")\n"
             "set(RAWREEL_SOURCE_TXT_CUSTOM_FFMPEG \"${named}\")\n")
    endforeach()
endfunction()
function(run_release_check case dist expected)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DDIST=${dist}" "-DPLATFORMS=windows;linux;macos"
                            -P "${release_check}"
                    RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX REPLACE "[ \t\r\n]+" " " err "${err}")
    if(expected STREQUAL "")
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "FAIL ${case}: the check failed: ${err}")
        endif()
        return()
    endif()
    if(result EQUAL 0)
        message(FATAL_ERROR "FAIL ${case}: the check passed, expected '${expected}'")
    endif()
    string(FIND "${err}" "${expected}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "FAIL ${case}: expected '${expected}', got: ${err}")
    endif()
endfunction()

set(dist "${WORK_DIR}/dist")
set(case "release FFmpeg sources: each package names its own custom_ffmpeg release")
release_infos("${dist}" 1.0.0 1.0.0 1.0.1)
run_release_check("${case}" "${dist}" "")
pass("${case}")

set(case "release FFmpeg sources: a package naming another release fails")
release_infos("${dist}" 1.0.0 1.0.0 1.0.0)
run_release_check("${case}" "${dist}" "rawreel-1.2.3-macos.dmg: SOURCE.txt names the custom_ffmpeg tags '1.0.0', expected exactly v1.0.1")
release_infos("${dist}" 1.0.0 1.0.1 1.0.1)
run_release_check("${case}" "${dist}" "rawreel-1.2.3-linux.tar.xz: SOURCE.txt names the custom_ffmpeg tags '1.0.1', expected exactly v1.0.0")
release_infos("${dist}" "" 1.0.0 1.0.1)
run_release_check("${case}" "${dist}" "rawreel-1.2.3-windows.zip: SOURCE.txt names the custom_ffmpeg tags '', expected exactly v1.0.0")
release_infos("${dist}" 1.0.0 1.0.0 "1.0.1,1.0.0")
run_release_check("${case}" "${dist}" "rawreel-1.2.3-macos.dmg: SOURCE.txt names the custom_ffmpeg tags '1.0.1, 1.0.0'")
pass("${case}")

set(case "release FFmpeg sources: a missing package info fails, a package without FFmpeg passes")
release_infos("${dist}" 1.0.0 1.0.0 1.0.1)
file(REMOVE "${dist}/package-info-linux.cmake")
run_release_check("${case}" "${dist}" "linux: no package-info-linux.cmake")
release_infos("${dist}" 1.0.0 1.0.0 "")
file(APPEND "${dist}/package-info-macos.cmake" "set(RAWREEL_BUNDLED OFF)\n")
run_release_check("${case}" "${dist}" "")
pass("${case}")

get_property(passed GLOBAL PROPERTY test_packaging_passed)
list(LENGTH passed passed_count)
message(STATUS "test_packaging_scripts: all ${passed_count} checks passed")
