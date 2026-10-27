# Unit tests of packaging/check-package.cmake and packaging/scan-artifact-privacy.cmake (design M4
# v2 D16, §8.6). Run as cmake -DWORK_DIR=<dir> -P test_packaging_scripts.cmake (CTest
# rawreel.packaging_scripts). Every tree and file is built inside WORK_DIR, which is emptied first.
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
file(WRITE "${manifest}" "# design §8.6; kind path [sha256]\r\n\nours rawreel-@V@/a.exe\n"
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

function(scan_text name text mode out)
    file(WRITE "${WORK_DIR}/${name}" "${text}\n")
    rawreel_scan_privacy(FILE "${WORK_DIR}/${name}" MODE ${mode} ${ARGN} FINDINGS findings)
    set(${out} "${findings}" PARENT_SCOPE)
endfunction()

set(case "privacy: clean text")
scan_text(clean.txt "built in ${R1} and ${D1}, version ${I4}, ${N1}, ${N2}, ${I5}" full findings)
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
expect_count("${case}" "${all_findings}" 10)
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
rawreel_ffmpeg_version_line(TEXT "rawreel 1.0.0\nwxWidgets 3.3.4\n${missing_line}\n"
                            LINE line ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${line}" "${missing_line}")
rawreel_ffmpeg_version_line(TEXT "rawreel 1.0.0\r\nFFmpeg 8.0.1 (LGPL) from x\\ffmpeg\r\n"
                            LINE line ERRORS errors)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${line}" "FFmpeg 8.0.1 (LGPL) from x\\ffmpeg")
rawreel_ffmpeg_version_line(TEXT "FFmpeg a; b\nlibFFmpeg c\nFFmpeg d\n" LINE line ERRORS errors)
expect_item("${case}" "${errors}" "--version must print one line starting with FFmpeg, printed 2")
expect_count("${case}" "${errors}" 1)
rawreel_ffmpeg_version_line(TEXT "rawreel 1.0.0\n x FFmpeg\n" LINE line ERRORS errors)
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

set(case "package names: Windows zip, Linux tar.xz, unknown platform refused")
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
rawreel_package_files(VERSION 1.2.3 PLATFORM macos PACKAGE package SYMBOLS symbols CPACK cpack
                      EXTENSION extension GENERATOR generator ERRORS errors)
expect_item("${case}" "${errors}" "no package for the platform 'macos' (windows, linux)")
expect_equal("${case}" "${package}" "")
pass("${case}")

set(case "FFmpeg library names: the file the loader opens and its major")
set(names "x/avutil-60.dll;x/avutil.dll;x/avutil-60.lib;x/avcodec-62.dll;x/libavutil.so.60"
          "x/libavutil.so.60.26.103;x/libavutil.so;x/libswscale.so.9")
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
set(executables rawreel-1.0.0/rawreel-gui rawreel-1.0.0/ffmpeg/libavutil.so.60)
rawreel_tar_listing_errors(TEXT "${listing}" EXECUTABLES ${executables}
                           ERRORS errors OWNER_NAMES owners)
expect_empty("${case}" "${errors}")
expect_equal("${case}" "${owners}" "builder/staff")
pass("${case}")

set(case "modes: a 777 file, a 644 exe or a world-writable folder fails")
string(REPLACE "-rw-r--r-- builder/staff  1083" "-rwxrwxrwx builder/staff  1083" bad "${listing}")
string(REPLACE "-rwxr-xr-x builder/staff 9000000" "-rw-r--r-- builder/staff 9000000" bad "${bad}")
string(REPLACE "drwxr-xr-x builder/staff     0 2026-10-27 10:00 rawreel-1.0.0/ffmpeg/"
               "drwxrwxrwx builder/staff     0 2026-10-27 10:00 rawreel-1.0.0/ffmpeg/" bad "${bad}")
rawreel_tar_listing_errors(TEXT "${bad}" EXECUTABLES ${executables} ERRORS errors OWNER_NAMES owners)
expect_count("${case}" "${errors}" 3)
expect_item("${case}" "${errors}" "rawreel-1.0.0/LICENSE: mode rwxrwxrwx, expected rw-r--r--")
expect_item("${case}" "${errors}" "rawreel-1.0.0/rawreel-gui: mode rw-r--r--, expected rwxr-xr-x")
expect_item("${case}" "${errors}" "rawreel-1.0.0/ffmpeg/: world-writable folder (rwxrwxrwx)")
pass("${case}")

set(case "modes: links, unreadable lines and an empty listing fail")
set(bad "${listing}lrwxrwxrwx builder/staff 0 2026-10-27 10:00 rawreel-1.0.0/ffmpeg/libx.so -> libavutil.so.60\n")
string(APPEND bad "garbage line\n")
rawreel_tar_listing_errors(TEXT "${bad}" EXECUTABLES ${executables} ERRORS errors OWNER_NAMES owners)
expect_count("${case}" "${errors}" 2)
expect_item("${case}" "${errors}"
            "rawreel-1.0.0/ffmpeg/libx.so -> libavutil.so.60: entry type l (only files and folders)")
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

get_property(passed GLOBAL PROPERTY test_packaging_passed)
list(LENGTH passed passed_count)
message(STATUS "test_packaging_scripts: all ${passed_count} checks passed")
