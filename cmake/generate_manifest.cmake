# Deterministic manifest generator (CC-FOUNDATION.md "Manifest deterministic
# 생성 규칙"): relative paths only, "/" separator, files sorted ascending by
# byte order, LF-only line endings, no absolute paths/hostname/username/
# timestamps, the manifest file itself excluded from its own hash list, each
# line formatted as "<sha256><two spaces><relative/path>", no blank lines or
# comments. Uses CMake's own file(SHA256) so the same CMake implementation
# computes hashes on macOS and Linux, rather than relying on whichever of
# shasum/sha256sum happens to be installed.
#
# Directory mode (hashes every file under MANIFEST_DIR):
#   cmake -DMANIFEST_ROOT=<repo root> -DMANIFEST_DIR=<dir relative to root> \
#         -DMANIFEST_OUT=<output path relative to root> -P cmake/generate_manifest.cmake
#
# Explicit file list mode:
#   cmake -DMANIFEST_ROOT=<repo root> -DMANIFEST_FILES="a;b;c" \
#         -DMANIFEST_OUT=<output path relative to root> -P cmake/generate_manifest.cmake

if(NOT DEFINED MANIFEST_ROOT OR NOT DEFINED MANIFEST_OUT)
    message(FATAL_ERROR "MANIFEST_ROOT and MANIFEST_OUT must be set")
endif()
if(NOT DEFINED MANIFEST_DIR AND NOT DEFINED MANIFEST_FILES)
    message(FATAL_ERROR "Set either MANIFEST_DIR or MANIFEST_FILES")
endif()

if(DEFINED MANIFEST_DIR)
    file(GLOB_RECURSE _rel_files RELATIVE "${MANIFEST_ROOT}" "${MANIFEST_ROOT}/${MANIFEST_DIR}/*")
else()
    set(_rel_files "${MANIFEST_FILES}")
endif()

set(_filtered "")
foreach(_f ${_rel_files})
    get_filename_component(_fname "${_f}" NAME)
    if(NOT _f STREQUAL MANIFEST_OUT AND NOT _fname STREQUAL ".DS_Store")
        list(APPEND _filtered "${_f}")
    endif()
endforeach()

list(SORT _filtered COMPARE STRING CASE SENSITIVE)

set(_lines "")
foreach(_f ${_filtered})
    file(SHA256 "${MANIFEST_ROOT}/${_f}" _hash)
    list(APPEND _lines "${_hash}  ${_f}")
endforeach()

set(_content "")
foreach(_line ${_lines})
    set(_content "${_content}${_line}\n")
endforeach()

file(WRITE "${MANIFEST_ROOT}/${MANIFEST_OUT}" "${_content}")
list(LENGTH _lines _n)
message(STATUS "generate_manifest.cmake: wrote ${MANIFEST_OUT} with ${_n} entries")
