cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED BREEZEDESK_SOURCE_DIR)
    get_filename_component(BREEZEDESK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT DEFINED BREEZEDESK_IDENTITY_OUTPUT_DIR)
    message(FATAL_ERROR "BREEZEDESK_IDENTITY_OUTPUT_DIR must name the output directory")
endif()

file(READ "${BREEZEDESK_SOURCE_DIR}/CMakeLists.txt" _breezedesk_top_level_cmake)
file(MAKE_DIRECTORY "${BREEZEDESK_IDENTITY_OUTPUT_DIR}")

function(_breezedesk_write_identity variable_name file_name)
    string(REGEX MATCH
        "set\\([ \t\r\n]*${variable_name}[ \t\r\n]+\"([^\"]+)\""
        _breezedesk_identity_match
        "${_breezedesk_top_level_cmake}")
    if(NOT CMAKE_MATCH_1)
        message(FATAL_ERROR "Could not read ${variable_name} from CMakeLists.txt")
    endif()
    file(WRITE "${BREEZEDESK_IDENTITY_OUTPUT_DIR}/${file_name}" "${CMAKE_MATCH_1}\n")
endfunction()

_breezedesk_write_identity(BREEZEDESK_PRODUCT_NAME product-name.txt)
_breezedesk_write_identity(BREEZEDESK_RELEASE_EXECUTABLE_NAME release-executable-name.txt)
_breezedesk_write_identity(BREEZEDESK_WORKER_EXECUTABLE_NAME worker-executable-name.txt)
_breezedesk_write_identity(BREEZEDESK_CLI_EXECUTABLE_NAME cli-executable-name.txt)
_breezedesk_write_identity(BREEZEDESK_WINDOWS_PRODUCT_ID windows-product-id.txt)
