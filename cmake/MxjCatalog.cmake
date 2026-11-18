# Builds the local test catalogue of MXJ plugins (phase 6) from resources/lua-plugins:
# one .mxjplugin package (a zip file) per example plugin, and index.json with the
# SHA-256 checksum and size of each package. The program reads it on its plugin
# page until the online catalogue opens with the release (phase 8).
#
# Sets MXJ_CATALOG_DIR to the folder that holds index.json and packages/.

set(MXJ_CATALOG_DIR "${PROJECT_BINARY_DIR}/catalog")
file(REMOVE_RECURSE "${MXJ_CATALOG_DIR}")
file(MAKE_DIRECTORY "${MXJ_CATALOG_DIR}/packages")

file(GLOB _mxj_plugin_dirs LIST_DIRECTORIES true "${PROJECT_SOURCE_DIR}/resources/lua-plugins/*")
list(SORT _mxj_plugin_dirs)
set(_mxj_entries "")

foreach(_dir IN LISTS _mxj_plugin_dirs)
    if(NOT EXISTS "${_dir}/plugin.json")
        continue()
    endif()

    # Re-run CMake when an example changes.
    file(GLOB _files RELATIVE "${_dir}" "${_dir}/*")
    foreach(_f IN LISTS _files)
        set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_dir}/${_f}")
    endforeach()

    file(READ "${_dir}/plugin.json" _json)
    foreach(_key id name version author license type category description minMxjVersion)
        string(JSON _${_key} ERROR_VARIABLE _err GET "${_json}" ${_key})
        if(_err)
            set(_${_key} "")
        endif()
    endforeach()
    string(JSON _permissions ERROR_VARIABLE _err GET "${_json}" permissions)
    if(_err)
        set(_permissions "[]")
    endif()

    set(_package "packages/${_id}-${_version}.mxjplugin")
    # Fixed file times, so the same sources always give the same package and checksum.
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${MXJ_CATALOG_DIR}/${_package}" --format=zip
                            "--mtime=2026-01-01 00:00:00" ${_files}
                    WORKING_DIRECTORY "${_dir}" RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "Could not make the plugin package ${_package}")
    endif()
    file(SHA256 "${MXJ_CATALOG_DIR}/${_package}" _sha)
    file(SIZE "${MXJ_CATALOG_DIR}/${_package}" _size)

    set(_entry "{}")
    foreach(_key id name version author license type category description minMxjVersion)
        string(REPLACE "\\" "\\\\" _value "${_${_key}}")
        string(REPLACE "\"" "\\\"" _value "${_value}")
        string(JSON _entry SET "${_entry}" ${_key} "\"${_value}\"")
    endforeach()
    string(JSON _entry SET "${_entry}" permissions "${_permissions}")
    string(JSON _entry SET "${_entry}" package "\"${_package}\"")
    string(JSON _entry SET "${_entry}" sha256 "\"${_sha}\"")
    string(JSON _entry SET "${_entry}" size "${_size}")
    list(APPEND _mxj_entries "${_entry}")
endforeach()

list(JOIN _mxj_entries ",\n    " _mxj_joined)
file(WRITE "${MXJ_CATALOG_DIR}/index.json"
"{
  \"format\": 1,
  \"name\": \"MXJ AUDIO test catalogue\",
  \"plugins\": [
    ${_mxj_joined}
  ]
}
")
