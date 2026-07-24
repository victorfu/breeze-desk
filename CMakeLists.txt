cmake_minimum_required(VERSION 3.24)

# Let compiler-cache integrations select an MSVC debug-information format
# without CMake injecting the legacy shared program database flags.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# CMake uses this value while enabling the compiler, so it must be established
# before project(). It is ignored by generators that do not target Apple
# platforms and remains overridable with -DCMAKE_OSX_DEPLOYMENT_TARGET=... .
if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum supported macOS version" FORCE)
endif()

project(BreezeDesk VERSION 1.0.0 LANGUAGES C CXX)

# Product identity lives here so rebranding does not require source edits.
set(BREEZEDESK_PRODUCT_NAME "BreezeDesk" CACHE STRING "User-visible product name")
set(BREEZEDESK_ORGANIZATION_NAME "BreezeDesk" CACHE STRING "Settings organization name")
set(BREEZEDESK_ORGANIZATION_DOMAIN "breezedesk.app" CACHE STRING "Settings organization domain")
set(BREEZEDESK_RELEASE_EXECUTABLE_NAME "BreezeDesk" CACHE STRING "Release executable name")
set(BREEZEDESK_DEBUG_EXECUTABLE_NAME "BreezeDesk-Debug" CACHE STRING "Debug executable name")
set(BREEZEDESK_WORKER_EXECUTABLE_NAME "breezedesk-asr-worker" CACHE STRING "ASR worker executable name")
set(BREEZEDESK_CLI_EXECUTABLE_NAME "breezedesk-cli" CACHE STRING "CLI executable name")
set(BREEZEDESK_RELEASE_BUNDLE_ID "com.victorfu.breezedesk" CACHE STRING "Release bundle identifier")
set(BREEZEDESK_DEBUG_BUNDLE_ID "com.victorfu.breezedesk.debug" CACHE STRING "Debug bundle identifier")
set(BREEZEDESK_WINDOWS_PRODUCT_ID "BreezeDesk" CACHE STRING "Windows registry and file-association identifier")
set(BREEZEDESK_RELEASE_DATA_DIRECTORY_NAME "BreezeDesk" CACHE STRING "Release data directory name")
set(BREEZEDESK_DEBUG_DATA_DIRECTORY_NAME "BreezeDesk-Debug" CACHE STRING "Debug data directory name")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_UNITY_BUILD OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(BREEZEDESK_BUILD_TESTS "Build BreezeDesk tests" ON)
option(BREEZEDESK_ENABLE_WHISPER "Build the native libwhisper ASR worker" ON)
option(BREEZEDESK_ENABLE_MODEL_INTEGRATION_TESTS "Enable tests which download a tiny whisper model" OFF)
option(BREEZEDESK_ENABLE_UPDATES "Enable native update framework adapters" OFF)
option(BREEZEDESK_WARNINGS_AS_ERRORS "Treat warnings in BreezeDesk code as errors" ON)
set(BREEZEDESK_WHISPER_CPP_REF "f049fff95a089aa9969deb009cdd4892b3e74916" CACHE STRING "Immutable whisper.cpp commit")
set(BREEZEDESK_WHISPER_CPP_SOURCE_DIR "" CACHE PATH "Optional local whisper.cpp checkout")
# Keep source builds usable with only the documented Visual Studio and Windows SDK
# prerequisites. Release packaging selects VULKAN explicitly and also builds a
# CPU fallback worker.
set(BREEZEDESK_WINDOWS_BACKEND "CPU" CACHE STRING "Windows worker backend: VULKAN or CPU")
set_property(CACHE BREEZEDESK_WINDOWS_BACKEND PROPERTY STRINGS VULKAN CPU)
set(BREEZEDESK_APPCAST_URL "" CACHE STRING "Sparkle/WinSparkle appcast URL")
set(BREEZEDESK_EDDSA_PUBLIC_KEY "" CACHE STRING "Sparkle EdDSA public key")
set(_breezedesk_default_windows_icon "")
if(WIN32)
    set(_breezedesk_default_windows_icon "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/breezedesk.ico")
endif()
set(BREEZEDESK_WINDOWS_ICON_PATH "${_breezedesk_default_windows_icon}" CACHE FILEPATH
    "Windows multi-resolution ICO generated from the canonical app icon")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(BreezeDeskCompilerCache)
include(BreezeDeskWarnings)
breezedesk_enable_compiler_cache()

find_package(Qt6 6.8 REQUIRED QUIET COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql Multimedia Network Concurrent Svg Widgets LinguistTools)
qt_standard_project_setup(REQUIRES 6.8 I18N_TRANSLATED_LANGUAGES en zh_TW)

configure_file(cmake/version.h.in generated/breezedesk/version.h @ONLY)
configure_file(cmake/app_config.h.in generated/breezedesk/app_config.h @ONLY)
configure_file(cmake/update_config.h.in generated/breezedesk/update_config.h @ONLY)
string(REGEX MATCH "^[0-9]+\\.[0-9]+" BREEZEDESK_QT_SERIES "${Qt6_VERSION}")
configure_file(cmake/qt_source_info.txt.in generated/Qt-SOURCE.txt @ONLY)

add_library(breezedesk_build_config INTERFACE)
target_include_directories(breezedesk_build_config INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_compile_features(breezedesk_build_config INTERFACE cxx_std_17)

if(BREEZEDESK_ENABLE_WHISPER)
    set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(GGML_METAL ON CACHE BOOL "" FORCE)
        # Apple Clang does not ship OpenMP. Avoid a known-failing optional probe;
        # Accelerate and Metal remain enabled for the macOS runtime.
        set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
    elseif(WIN32)
        if(BREEZEDESK_WINDOWS_BACKEND STREQUAL "VULKAN")
            set(GGML_VULKAN ON CACHE BOOL "" FORCE)
        else()
            set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
        endif()
    endif()

    # The pinned whisper.cpp revision still declares compatibility with old
    # CMake versions. Suppress that dependency-only deprecation notice while
    # retaining normal warnings for BreezeDesk itself.
    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.4")
        cmake_diagnostic(PUSH)
        cmake_diagnostic(SET CMD_DEPRECATED IGNORE)
    else()
        if(DEFINED CMAKE_WARN_DEPRECATED)
            set(_breezedesk_saved_cmake_warn_deprecated "${CMAKE_WARN_DEPRECATED}")
            set(_breezedesk_had_cmake_warn_deprecated ON)
        else()
            set(_breezedesk_had_cmake_warn_deprecated OFF)
        endif()
        set(CMAKE_WARN_DEPRECATED OFF)
    endif()

    if(BREEZEDESK_WHISPER_CPP_SOURCE_DIR)
        add_subdirectory("${BREEZEDESK_WHISPER_CPP_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/whisper.cpp" EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(whisper_cpp
            GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
            GIT_TAG "${BREEZEDESK_WHISPER_CPP_REF}"
            GIT_SHALLOW FALSE
            GIT_PROGRESS TRUE)
        FetchContent_MakeAvailable(whisper_cpp)
    endif()

    if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.4")
        cmake_diagnostic(POP)
    else()
        if(_breezedesk_had_cmake_warn_deprecated)
            set(CMAKE_WARN_DEPRECATED "${_breezedesk_saved_cmake_warn_deprecated}")
        else()
            unset(CMAKE_WARN_DEPRECATED)
        endif()
        unset(_breezedesk_saved_cmake_warn_deprecated)
        unset(_breezedesk_had_cmake_warn_deprecated)
    endif()

    if(NOT TARGET whisper)
        message(FATAL_ERROR
            "BREEZEDESK_ENABLE_WHISPER=ON, but the pinned whisper.cpp source did not define the whisper target")
    endif()
    message(STATUS
        "BreezeDesk whisper.cpp runtime: enabled (${BREEZEDESK_WHISPER_CPP_REF})")
else()
    message(STATUS
        "BreezeDesk whisper.cpp runtime: intentionally disabled; transcription will be unavailable")
endif()

add_subdirectory(src/core)
add_subdirectory(src/ipc)
add_subdirectory(src/database)
add_subdirectory(src/settings)
add_subdirectory(src/audio)
add_subdirectory(src/models)
add_subdirectory(src/glossary)
add_subdirectory(src/transcript)
add_subdirectory(src/jobs)
add_subdirectory(src/asr)
add_subdirectory(src/platform)
add_subdirectory(src/update)
add_subdirectory(src/ui)
add_subdirectory(src/worker)
add_subdirectory(src/app)
add_subdirectory(src/cli)

if(BREEZEDESK_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.8 REQUIRED QUIET COMPONENTS Test QuickTest)
    add_subdirectory(tests)
endif()

include(GNUInstallDirs)
install(TARGETS BreezeDesk breezedesk-asr-worker breezedesk-cli
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES resources/models/models.json THIRD_PARTY_NOTICES.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/breezedesk")
install(FILES resources/icons/lucide/LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/breezedesk/licenses"
    RENAME Lucide-LICENSE.txt)
install(FILES resources/icons/lucide/SOURCE.md
    DESTINATION "${CMAKE_INSTALL_DATADIR}/breezedesk/licenses"
    RENAME Lucide-SOURCE.md)
install(FILES
    docs/licenses/Qt-LGPL-NOTICE.md
    docs/licenses/LGPL-3.0-only.txt
    docs/licenses/GPL-3.0-only.txt
    "${CMAKE_CURRENT_BINARY_DIR}/generated/Qt-SOURCE.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/breezedesk/licenses")
install(FILES LICENSE
    DESTINATION "${CMAKE_INSTALL_DATADIR}/breezedesk/licenses"
    RENAME "${BREEZEDESK_PRODUCT_NAME}-MIT.txt")

set(CPACK_PACKAGE_NAME "${BREEZEDESK_PRODUCT_NAME}")
set(CPACK_PACKAGE_VENDOR "Victor Fu")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "${BREEZEDESK_PRODUCT_NAME} maintainers")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)
