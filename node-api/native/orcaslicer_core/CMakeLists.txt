cmake_minimum_required(VERSION 3.15)
project(orcaslicer_core)

# Use cmake-js provided toolchain variables
if(NOT DEFINED CMAKE_JS_INC)
  message(STATUS "CMAKE_JS_INC not defined. This CMake project is intended to be built via cmake-js.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_definitions(-DNAPI_VERSION=8)

include_directories(${CMAKE_JS_INC})

# node-addon-api headers will be resolved from node_modules
# We still add an include hint for common paths.
include_directories(${CMAKE_SOURCE_DIR}/../../node_modules/node-addon-api)

add_library(orcaslicer_core SHARED addon.cc ${CMAKE_JS_SRC})

# On macOS / Linux, Node addons should be named with the correct suffix
set_target_properties(orcaslicer_core PROPERTIES PREFIX "" SUFFIX ".node")

target_link_libraries(orcaslicer_core ${CMAKE_JS_LIB})

# Orca/Prusa integration
set(ORCA_SRC_DIR ${CMAKE_SOURCE_DIR}/../../../source_OrcaSlicer/src)
target_include_directories(orcaslicer_core PRIVATE ${ORCA_SRC_DIR})

# For now, we'll include minimal source files directly to avoid complex linking
# In production, we'd link against libslic3r.a or similar
target_sources(orcaslicer_core PRIVATE
  orca_minimal.cpp
)

