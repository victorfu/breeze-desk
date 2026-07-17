cmake_minimum_required(VERSION 3.24)

if(NOT DEFINED BREEZEDESK_SOURCE_DIR)
    get_filename_component(BREEZEDESK_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()
if(NOT DEFINED BREEZEDESK_VERSION_OUTPUT)
    message(FATAL_ERROR "BREEZEDESK_VERSION_OUTPUT must name the output file")
endif()

file(READ "${BREEZEDESK_SOURCE_DIR}/CMakeLists.txt" _breezedesk_top_level_cmake)
string(REGEX MATCH
    "project\\([ \t\r\n]*BreezeDesk[ \t\r\n]+VERSION[ \t\r\n]+([0-9]+\\.[0-9]+\\.[0-9]+)"
    _breezedesk_project_match
    "${_breezedesk_top_level_cmake}")
if(NOT CMAKE_MATCH_1)
    message(FATAL_ERROR "Could not read project(BreezeDesk VERSION x.y.z) from CMakeLists.txt")
endif()

file(WRITE "${BREEZEDESK_VERSION_OUTPUT}" "${CMAKE_MATCH_1}\n")
