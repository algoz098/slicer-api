cmake_minimum_required(VERSION 3.13)
project(orcaslicer_node LANGUAGES C CXX)


# Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


# CMake for Node.js addon binding

# This subdirectory is added only when ORCACLI_BUILD_NODE_ADDON=ON
# Assumes the parent CMake project has already defined target `orcacli_core`.

# Allow skipping the core build when only the Node addon is desired (dlopen engine at runtime)
option(ORCACLI_SKIP_CORE_BUILD "Skip building orcacli core; addon dlopens engine at runtime" OFF)

# Try to reuse existing core target; if missing, include it directly from parent
if(NOT TARGET orcacli_core AND NOT ORCACLI_SKIP_CORE_BUILD)
    # Assume this CMake is invoked from bindings/node; include ../../src to define orcacli_core
    get_filename_component(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    get_filename_component(REPO_ROOT "${ROOT_DIR}/.." ABSOLUTE)
    # Provide OrcaSlicer root to core CMake (matches top-level expectation)
    if(NOT DEFINED ORCASLICER_ROOT_DIR)
        set(ORCASLICER_ROOT_DIR "${REPO_ROOT}/OrcaSlicer" CACHE PATH "Path to OrcaSlicer root directory" FORCE)
    endif()
    # In CI, require full libs and fail-hard if missing
    if(NOT DEFINED ORCACLI_REQUIRE_LIBS AND DEFINED ENV{GITHUB_ACTIONS})
        set(ORCACLI_REQUIRE_LIBS ON CACHE BOOL "Fail if OrcaSlicer libs are not found" FORCE)
    endif()
    # Ensure OrcaSlicer dirs are known when building from bindings directly
    if(NOT DEFINED ORCASLICER_SRC_DIR)
        set(ORCASLICER_SRC_DIR "${ORCASLICER_ROOT_DIR}/src")
    endif()
    if(NOT DEFINED ORCASLICER_BUILD_DIR)
        set(ORCASLICER_BUILD_DIR "${ORCASLICER_ROOT_DIR}/build")
        if(EXISTS "${ORCASLICER_BUILD_DIR}/arm64")
            set(ORCASLICER_BUILD_DIR "${ORCASLICER_BUILD_DIR}/arm64")
            set(ORCASLICER_ARCH "arm64")
            message(STATUS "Using ARM64 build directory (bindings): ${ORCASLICER_BUILD_DIR}")
        elseif(EXISTS "${ORCASLICER_BUILD_DIR}/x64")
            set(ORCASLICER_BUILD_DIR "${ORCASLICER_BUILD_DIR}/x64")
            set(ORCASLICER_ARCH "x64")
            message(STATUS "Using x64 build directory (bindings): ${ORCASLICER_BUILD_DIR}")
        else()
            if(NOT DEFINED ORCASLICER_ARCH)
                if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
                    set(ORCASLICER_ARCH "arm64")
                else()
                    set(ORCASLICER_ARCH "x64")
                endif()
            endif()
        endif()
    endif()
    # Help downstream find deps installed prefix from OrcaSlicer build
    set(CMAKE_PREFIX_PATH "${ORCASLICER_BUILD_DIR}/deps/usr/local" ${CMAKE_PREFIX_PATH})

    # Ensure common dependencies expected by core are available
    find_package(Threads REQUIRED)
    add_subdirectory("${ROOT_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}/orcaslicercli_core")
endif()

# Try to locate Node headers/libraries.
# Preferred path: using cmake-js, which defines CMAKE_JS_INC and CMAKE_JS_LIB
# Alternatively, modern CMake might provide FindNodeJS.

set(HAVE_NODE_INC OFF)
set(HAVE_NODE_LIB OFF)
set(EXTRA_NODE_INC "")

if(DEFINED CMAKE_JS_INC)
    set(HAVE_NODE_INC ON)
endif()
if(DEFINED CMAKE_JS_LIB)
    set(HAVE_NODE_LIB ON)
endif()

find_package(NodeJS QUIET)
if(NodeJS_FOUND)
    set(HAVE_NODE_INC ON)
    set(HAVE_NODE_LIB ON)
endif()

# Also allow dev dependency node-api-headers if present (installed via npm)
set(_NODE_API_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/node_modules/node-api-headers/include")
if(EXISTS "${_NODE_API_HEADERS_DIR}/node/node_api.h")
    set(EXTRA_NODE_INC "${_NODE_API_HEADERS_DIR}/node")
    message(STATUS "Adding Node-API headers from devDependency: ${EXTRA_NODE_INC}")
elseif(EXISTS "${_NODE_API_HEADERS_DIR}/node_api.h")
    set(EXTRA_NODE_INC "${_NODE_API_HEADERS_DIR}")
    message(STATUS "Adding Node-API headers from devDependency: ${EXTRA_NODE_INC}")
endif()

if(NOT HAVE_NODE_INC AND NOT EXTRA_NODE_INC)
    message(FATAL_ERROR "Node.js headers not found. Build with cmake-js or provide NodeJS package or add devDependency 'node-api-headers'.")
endif()

add_library(orcaslicer_node MODULE
    src/addon.cc
)

# Ensure the engine is built before the addon (addon dlopens the engine at runtime)
if(TARGET orcacli_engine)
    add_dependencies(orcaslicer_node orcacli_engine)
endif()

# Output must be .node with no lib prefix
set_target_properties(orcaslicer_node PROPERTIES
    PREFIX ""
    SUFFIX ".node"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bindings/node"
)

# Includes: our core headers and Node headers
if(NodeJS_FOUND)
    target_include_directories(orcaslicer_node PRIVATE ${NodeJS_INCLUDE_DIRS})
endif()
if(DEFINED CMAKE_JS_INC)
    target_include_directories(orcaslicer_node PRIVATE ${CMAKE_JS_INC})
endif()
if(EXTRA_NODE_INC)
    target_include_directories(orcaslicer_node PRIVATE ${EXTRA_NODE_INC})
endif()
# Note: The addon no longer links directly to the core; it dlopens liborcacli_engine at runtime.

# Link Node libs if provided (cmake-js or FindNodeJS)
if(DEFINED CMAKE_JS_LIB)
    target_link_libraries(orcaslicer_node PRIVATE ${CMAKE_JS_LIB})
endif()
if(NodeJS_FOUND)
    if(TARGET NodeJS::NodeJS)
        target_link_libraries(orcaslicer_node PRIVATE NodeJS::NodeJS)
    elseif(DEFINED NodeJS_LIBRARIES)
        target_link_libraries(orcaslicer_node PRIVATE ${NodeJS_LIBRARIES})
    endif()
endif()

# On macOS, ensure proper linking
if(APPLE)
    # Resolve Node-API symbols at runtime from the host Node binary
    target_link_options(orcaslicer_node PRIVATE "-undefined dynamic_lookup" "-Wl,-undefined,dynamic_lookup")
    # Ensure C++ stdlib is linked
    target_link_libraries(orcaslicer_node PRIVATE "-lc++")
endif()

