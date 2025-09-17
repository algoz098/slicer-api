cmake_minimum_required(VERSION 3.13)

# Project definition
project(OrcaSlicerCli VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(ORCACLI_BUILD_TESTS "Build tests" OFF)
option(ORCACLI_STATIC_LINKING "Use static linking" ON)
# Optional: enable AddressSanitizer for local debugging
option(ORCACLI_ENABLE_ASAN "Enable AddressSanitizer (for local debug builds)" OFF)

# Require full OrcaSlicer libs (libslic3r/semver/etc.) to be present; if ON, CMake will fail if missing
if(NOT DEFINED ORCACLI_REQUIRE_LIBS AND DEFINED ENV{ORCACLI_REQUIRE_LIBS})
    set(ORCACLI_REQUIRE_LIBS "$ENV{ORCACLI_REQUIRE_LIBS}" CACHE BOOL "Fail if OrcaSlicer libs are not found" FORCE)
endif()
option(ORCACLI_REQUIRE_LIBS "Fail if OrcaSlicer libs are not found" OFF)

# Node addon option
option(ORCACLI_BUILD_NODE_ADDON "Build Node.js addon (.node) linking orcacli_core" OFF)

# Verify CMake version compatibility (same as OrcaSlicer)
if(((MSVC) OR (WIN32)) AND (${CMAKE_VERSION} VERSION_GREATER_EQUAL "4.0"))
    message(FATAL_ERROR "Only cmake versions between 3.13.x and 3.31.x is supported on windows. Detected version: ${CMAKE_VERSION}")
endif()

# Platform-specific settings
if(APPLE)
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "11.3")
    endif()
endif()

# Find OrcaSlicer installation or build directory
set(ORCASLICER_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../OrcaSlicer" CACHE PATH "Path to OrcaSlicer root directory")

if(NOT EXISTS "${ORCASLICER_ROOT_DIR}")
    message(FATAL_ERROR "OrcaSlicer root directory not found at: ${ORCASLICER_ROOT_DIR}")
endif()

# Set paths for OrcaSlicer components
set(ORCASLICER_SRC_DIR "${ORCASLICER_ROOT_DIR}/src")
set(ORCASLICER_BUILD_DIR "${ORCASLICER_ROOT_DIR}/build")

# Detect architecture-specific build directory and set ORCASLICER_ARCH
if(EXISTS "${ORCASLICER_BUILD_DIR}/arm64")
    set(ORCASLICER_BUILD_DIR "${ORCASLICER_BUILD_DIR}/arm64")
    set(ORCASLICER_ARCH "arm64")
    message(STATUS "Using ARM64 build directory: ${ORCASLICER_BUILD_DIR}")
elseif(EXISTS "${ORCASLICER_BUILD_DIR}/x64")
    set(ORCASLICER_BUILD_DIR "${ORCASLICER_BUILD_DIR}/x64")
    set(ORCASLICER_ARCH "x64")
    message(STATUS "Using x64 build directory: ${ORCASLICER_BUILD_DIR}")
else()
    # Some platforms keep everything directly under build/
    if(NOT DEFINED ORCASLICER_ARCH)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            set(ORCASLICER_ARCH "arm64")
        else()
            set(ORCASLICER_ARCH "x64")
        endif()
    endif()
endif()

# Check if OrcaSlicer is built
if(NOT EXISTS "${ORCASLICER_BUILD_DIR}")
    message(FATAL_ERROR "OrcaSlicer build directory not found at: ${ORCASLICER_BUILD_DIR}")
    message(FATAL_ERROR "Please build OrcaSlicer first using:")
    message(FATAL_ERROR "  cd ${ORCASLICER_ROOT_DIR}")
    message(FATAL_ERROR "  ./build_release_macos.sh  # or ./build_linux.sh")
endif()

# Check if OrcaSlicer dependencies are built (warning only for now)
if(NOT EXISTS "${ORCASLICER_BUILD_DIR}/deps")
    message(WARNING "OrcaSlicer dependencies not found at ${ORCASLICER_BUILD_DIR}/deps")
    message(WARNING "Some features may not work. Consider building OrcaSlicer dependencies first.")
endif()

# Add OrcaSlicer's cmake modules to path
list(APPEND CMAKE_MODULE_PATH "${ORCASLICER_ROOT_DIR}/cmake/modules")

# Include OrcaSlicer's dependencies
set(CMAKE_PREFIX_PATH "${ORCASLICER_BUILD_DIR}/deps/usr/local" ${CMAKE_PREFIX_PATH})

# Find required dependencies
find_package(Threads REQUIRED)

# Try to find Boost headers (required by libslic3r)
set(BOOST_ROOT "/opt/homebrew")
find_path(BOOST_INCLUDE_DIR boost/optional.hpp
    PATHS /opt/homebrew/include
          /usr/local/include
          /usr/include
)

if(BOOST_INCLUDE_DIR)
    message(STATUS "Found Boost headers at: ${BOOST_INCLUDE_DIR}")
    add_library(boost_headeronly INTERFACE)
    target_include_directories(boost_headeronly SYSTEM INTERFACE ${BOOST_INCLUDE_DIR})

    if (APPLE)
        # BOOST_ASIO_DISABLE_KQUEUE : prevents a Boost ASIO bug on OS X
        target_compile_definitions(boost_headeronly INTERFACE BOOST_ASIO_DISABLE_KQUEUE)
    endif()
else()
    message(WARNING "Boost headers not found - some features may not work")
endif()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ORCASLICER_SRC_DIR}
    ${ORCASLICER_SRC_DIR}/libslic3r
)

# Compiler flags
if(MSVC)
    add_compile_options(/W3)
    if(ORCACLI_STATIC_LINKING)
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    add_compile_options(-Wall -Wextra)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g -O0)
    else()
        add_compile_options(-O3 -DNDEBUG)
    endif()
endif()
# Global sanitizer flags (affect all targets) when enabled
if(ORCACLI_ENABLE_ASAN AND NOT MSVC)
    message(STATUS "ASAN enabled: adding -fsanitize=address")
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer -O1 -g)
    add_link_options(-fsanitize=address)
endif()



# Add core first
add_subdirectory(src)

# Node.js addon (optional)
if(ORCACLI_BUILD_NODE_ADDON)
    add_subdirectory(bindings/node)
endif()

# Tests
if(ORCACLI_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Installation
install(TARGETS orcaslicer-cli
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

# Install scripts
install(DIRECTORY scripts/
    DESTINATION share/orcaslicer-cli/scripts
    USE_SOURCE_PERMISSIONS
)

# Install examples
install(DIRECTORY examples/
    DESTINATION share/orcaslicer-cli/examples
)

# Install documentation
install(DIRECTORY docs/
    DESTINATION share/doc/orcaslicer-cli
)

# Package configuration
set(CPACK_PACKAGE_NAME "OrcaSlicerCli")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Extended CLI for OrcaSlicer")
set(CPACK_PACKAGE_VENDOR "OrcaSlicerCli Project")

include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "OrcaSlicerCli Configuration Summary:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Static linking: ${ORCACLI_STATIC_LINKING}")
message(STATUS "  Build tests: ${ORCACLI_BUILD_TESTS}")
message(STATUS "  OrcaSlicer root: ${ORCASLICER_ROOT_DIR}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
