# OrcaSlicerCli source directory

# Core library sources (only existing files)
set(ORCACLI_CORE_SOURCES
    core/CliCore.cpp
    core/CliCore.hpp
)

# Command sources (placeholder - will be implemented later)
set(ORCACLI_COMMAND_SOURCES
    # commands/BaseCommand.cpp
    # commands/BaseCommand.hpp
    # commands/SliceCommand.cpp
    # commands/SliceCommand.hpp
    # commands/InfoCommand.cpp
    # commands/InfoCommand.hpp
    # commands/VersionCommand.cpp
    # commands/VersionCommand.hpp
    # commands/HelpCommand.cpp
    # commands/HelpCommand.hpp
)

# Utility sources
set(ORCACLI_UTIL_SOURCES
    utils/ArgumentParser.cpp
    utils/ArgumentParser.hpp
    utils/Logger.cpp
    utils/Logger.hpp
    utils/ErrorHandler.cpp
    utils/ErrorHandler.hpp
    nanosvg_impl.cpp
)

# Main executable source
set(ORCACLI_MAIN_SOURCES
    main.cpp
    Application.cpp
    Application.hpp
)

# Create core library
add_library(orcacli_core STATIC
    ${ORCACLI_CORE_SOURCES}
    ${ORCACLI_COMMAND_SOURCES}
    ${ORCACLI_UTIL_SOURCES}
)

# Ensure static core can be linked into shared engine (Linux/UNIX needs PIC)
set_property(TARGET orcacli_core PROPERTY POSITION_INDEPENDENT_CODE ON)


# Always include nanosvg headers for nanosvg_impl.cpp regardless of libslic3r availability
# This prevents build failures when HAVE_LIBSLIC3R=0 or before libs are detected.
target_include_directories(orcacli_core PRIVATE
    ${ORCASLICER_ROOT_DIR}/deps_src/nanosvg
)


# Link required dependencies
target_link_libraries(orcacli_core
    # Basic system libraries
    Threads::Threads
)

# Link Boost headers if available
if(BOOST_INCLUDE_DIR)
    target_link_libraries(orcacli_core boost_headeronly)
endif()

# Try to find and link OrcaSlicer libraries if they exist (support single-config Ninja and multi-config "Release")
set(_LIBSLIC3R_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/src/libslic3r/Release/liblibslic3r.a"
    "${ORCASLICER_BUILD_DIR}/src/libslic3r/liblibslic3r.a"
)
set(_LIBSLIC3R_CGAL_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/src/libslic3r/Release/liblibslic3r_cgal.a"
    "${ORCASLICER_BUILD_DIR}/src/libslic3r/liblibslic3r_cgal.a"
)
set(_SEMVER_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/lib/Release/libsemver.a"
    "${ORCASLICER_BUILD_DIR}/lib/libsemver.a"
    # Also check deps install prefixes produced by OrcaSlicer/deps build
    "${ORCASLICER_BUILD_DIR}/deps/usr/local/lib/libsemver.a"
    "${ORCASLICER_ROOT_DIR}/deps/build/destdir/usr/local/lib/libsemver.a"
    "${ORCASLICER_ROOT_DIR}/deps/build/${ORCASLICER_ARCH}/OrcaSlicer_dep/usr/local/lib/libsemver.a"
)
set(LIBSLIC3R_PATH "")
foreach(p IN LISTS _LIBSLIC3R_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBSLIC3R_PATH "${p}")
        break()
    endif()
endforeach()
set(LIBSLIC3R_CGAL_PATH "")
foreach(p IN LISTS _LIBSLIC3R_CGAL_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBSLIC3R_CGAL_PATH "${p}")
        break()
    endif()
endforeach()
set(SEMVER_PATH "")
foreach(p IN LISTS _SEMVER_CANDIDATES)
    if(EXISTS "${p}")
        set(SEMVER_PATH "${p}")
        break()
    endif()
endforeach()

# Check for required dependencies
set(ENABLE_LIBSLIC3R OFF)
if(LIBSLIC3R_PATH AND SEMVER_PATH)
    set(ENABLE_LIBSLIC3R ON)
    message(STATUS "Found libslic3r at: ${LIBSLIC3R_PATH}")
    message(STATUS "Found semver at: ${SEMVER_PATH}")

    # Link libraries
    target_link_libraries(orcacli_core ${LIBSLIC3R_PATH})
    target_link_libraries(orcacli_core ${SEMVER_PATH})
    target_compile_definitions(orcacli_core PRIVATE HAVE_LIBSLIC3R=1)

    # Resolve deps install prefix from common OrcaSlicer layouts (Linux destdir; arch-specific; build tree)
    set(_DEPS_PREFIX_CANDIDATE1 "${ORCASLICER_BUILD_DIR}/deps/usr/local")
    set(_DEPS_PREFIX_CANDIDATE2 "${ORCASLICER_ROOT_DIR}/deps/build/destdir/usr/local")
    set(_DEPS_PREFIX_CANDIDATE3 "${ORCASLICER_ROOT_DIR}/deps/build/${ORCASLICER_ARCH}/OrcaSlicer_dep/usr/local")
    if(EXISTS "${_DEPS_PREFIX_CANDIDATE1}")
        set(ORCASLICER_DEPS_PREFIX "${_DEPS_PREFIX_CANDIDATE1}")
    elseif(EXISTS "${_DEPS_PREFIX_CANDIDATE2}")
        set(ORCASLICER_DEPS_PREFIX "${_DEPS_PREFIX_CANDIDATE2}")
    else()
        set(ORCASLICER_DEPS_PREFIX "${_DEPS_PREFIX_CANDIDATE3}")
    endif()

    # Common prefix for 3rd party static libs installed by deps build
    set(ORCASLICER_DEPS_LIBDIR "${ORCASLICER_DEPS_PREFIX}/lib")

    # Add additional include directories
    target_include_directories(orcacli_core PRIVATE
        ${ORCASLICER_ROOT_DIR}/deps_src
        ${ORCASLICER_ROOT_DIR}/deps_src/eigen
        ${ORCASLICER_ROOT_DIR}/deps_src/libigl
        ${ORCASLICER_ROOT_DIR}/deps_src/nanosvg
        ${ORCASLICER_ROOT_DIR}/deps_src/qoi
        ${ORCASLICER_DEPS_PREFIX}/include
        ${ORCASLICER_DEPS_PREFIX}/include/opencascade
        ${ORCASLICER_BUILD_DIR}/src/libslic3r
    )

    message(STATUS "libslic3r integration enabled")

else()
    message(FATAL_ERROR "Required OrcaSlicer libraries not found (libslic3r/semver). Placeholders are not allowed — build must link libs successfully.")
endif()

if(EXISTS "${LIBSLIC3R_CGAL_PATH}")
    message(STATUS "Found libslic3r_cgal at: ${LIBSLIC3R_CGAL_PATH}")
    if(ENABLE_LIBSLIC3R)
        target_link_libraries(orcacli_core ${LIBSLIC3R_CGAL_PATH})
    endif()
endif()

# Find additional required libraries for libslic3r
# Core dep library locations (prefer deps install prefix, fallback to arch-specific cache)
set(LIBTBB_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libtbb.a")
set(LIBTBBMALLOC_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libtbbmalloc.a")
# miniz (both multi-config and single-config layouts)
set(_MINIZ_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/miniz/Release/libminiz_static.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/miniz/libminiz_static.a"
)
set(LIBMINIZ_PATH "")
foreach(p IN LISTS _MINIZ_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBMINIZ_PATH "${p}")
        break()
    endif()
endforeach()

set(LIBPNG_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libpng16.a")
set(LIBQHULL_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libqhullstatic_r.a")
set(LIBMPFR_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libmpfr.a")
set(LIBGMPXX_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libgmpxx.a")
set(LIBGMP_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libgmp.a")

# qoi
set(_QOI_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/qoi/Release/libqoi.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/qoi/libqoi.a"
)
set(LIBQOI_PATH "")
foreach(p IN LISTS _QOI_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBQOI_PATH "${p}")
        break()
    endif()
endforeach()

# glu-libtess
set(_GLU_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/glu-libtess/Release/libglu-libtess.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/glu-libtess/libglu-libtess.a"
)
set(LIBGLU_PATH "")
foreach(p IN LISTS _GLU_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBGLU_PATH "${p}")
        break()
    endif()
endforeach()

# mcut
set(_MCUT_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/mcut/Release/libmcut.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/mcut/libmcut.a"
)
set(LIBMCUT_PATH "")
foreach(p IN LISTS _MCUT_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBMCUT_PATH "${p}")
        break()
    endif()
endforeach()
set(LIBJPEG_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libjpeg.a")
set(LIBQHULL_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libqhullstatic_r.a")
set(QHULLCPP_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libqhullcpp.a")

# Fallback arch-specific cache locations
set(LIBTBB_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libtbb.a")
set(LIBTBBMALLOC_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libtbbmalloc.a")
set(LIBPNG_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libpng16.a")
set(LIBQHULL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libqhullstatic_r.a")
set(LIBMPFR_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libmpfr.a")
set(LIBGMPXX_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libgmpxx.a")
set(LIBGMP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libgmp.a")
set(LIBJPEG_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libjpeg.a")

# Boost libraries
set(BOOST_FILESYSTEM_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_filesystem.a")
set(BOOST_LOG_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_log.a")
set(BOOST_LOG_SETUP_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_log_setup.a")
set(BOOST_THREAD_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_thread.a")
set(BOOST_LOCALE_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_locale.a")
set(BOOST_NOWIDE_PATH2 "${ORCASLICER_DEPS_LIBDIR}/libboost_nowide.a")

set(BOOST_FILESYSTEM_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_filesystem.a")
set(BOOST_LOG_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_log.a")
set(BOOST_LOG_SETUP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_log_setup.a")
set(BOOST_THREAD_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_thread.a")

# OpenCASCADE libraries (most essential ones)
set(TKKERNEL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKernel.a")
set(TKBO_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBO.a")
set(TKSTEP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKSTEP.a")
set(TKSTEPBASE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKSTEPBase.a")
set(TKXCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXCAF.a")
set(TKBINL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBinL.a")
set(TKXMLL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXmlL.a")
set(TKCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKCAF.a")
set(TKLCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKLCAF.a")
set(TKBREP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBRep.a")
set(TKTOPALGO_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKTopAlgo.a")
set(TKSERVICE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKService.a")
set(TKV3D_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKV3d.a")
set(TKGEOMALGO_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKGeomAlgo.a")
set(TKGEOMBASE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKGeomBase.a")
set(TKMESH_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKMesh.a")
set(TKPRIM_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKPrim.a")
set(TKG3D_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKG3d.a")
set(TKMATH_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKMath.a")
set(TKSHHEALING_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKShHealing.a")
set(TKG2D_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKG2d.a")
set(TKHLR_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKHLR.a")
set(BOOST_LOCALE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_locale.a")
set(BOOST_NOWIDE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libboost_nowide.a")
set(TKCDF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKCDF.a")


set(TKXDESTEP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXDESTEP.a")
set(TKXMLXCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXmlXCAF.a")
set(TKBINXCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBinXCAF.a")
set(TKVCAF_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKVCAF.a")
set(QHULLCPP_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libqhullcpp.a")
set(TKXSBASE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXSBase.a")
set(TKBIN_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBin.a")
set(TKBOOL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKBool.a")
set(TKIGES_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKIGES.a")
set(TKSTEP209_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKSTEP209.a")
set(TKSTEPATTR_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKSTEPAttr.a")
set(TKSTL_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKSTL.a")
set(TKRWMESH_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKRWMesh.a")
set(TKVRML_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKVRML.a")
set(TKXDEIGES_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXDEIGES.a")
set(TKXML_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libTKXml.a")
set(ADMESH_PATH "${ORCASLICER_ROOT_DIR}/build/arm64/deps_src/admesh/Release/libadmesh.a")
set(FREETYPE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/libfreetype.a")

# Noise library (prefer deps install prefix; support multiple possible names)
set(_LIBNOISE_CANDIDATES2
    "${ORCASLICER_DEPS_LIBDIR}/liblibnoise_static.a"
    "${ORCASLICER_DEPS_LIBDIR}/libnoise_static.a"
    "${ORCASLICER_DEPS_LIBDIR}/libnoise.a"
)
set(LIBNOISE_PATH2 "")
foreach(p IN LISTS _LIBNOISE_CANDIDATES2)
    if(EXISTS "${p}")
        set(LIBNOISE_PATH2 "${p}")
        break()
    endif()
endforeach()

# Fallback (arch-specific cache)
set(LIBNOISE_PATH "${ORCASLICER_ROOT_DIR}/deps/build/arm64/OrcaSlicer_dep/usr/local/lib/liblibnoise_static.a")

# Clipper library (support both multi/single-config)
set(_CLIPPER_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/clipper/Release/libclipper.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/clipper/libclipper.a"
)
set(LIBCLIPPER_PATH "")
foreach(p IN LISTS _CLIPPER_CANDIDATES)
    if(EXISTS "${p}")
        set(LIBCLIPPER_PATH "${p}")
        break()
    endif()
endforeach()

# TBB
if(EXISTS "${LIBTBB_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libtbb at: ${LIBTBB_PATH2}")
    target_link_libraries(orcacli_core ${LIBTBB_PATH2})
elseif(EXISTS "${LIBTBB_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libtbb at: ${LIBTBB_PATH}")
    target_link_libraries(orcacli_core ${LIBTBB_PATH})
endif()

# TBB malloc
if(EXISTS "${LIBTBBMALLOC_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libtbbmalloc at: ${LIBTBBMALLOC_PATH2}")
    target_link_libraries(orcacli_core ${LIBTBBMALLOC_PATH2})
elseif(EXISTS "${LIBTBBMALLOC_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libtbbmalloc at: ${LIBTBBMALLOC_PATH}")
    target_link_libraries(orcacli_core ${LIBTBBMALLOC_PATH})
endif()

# miniz
if(EXISTS "${LIBMINIZ_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libminiz at: ${LIBMINIZ_PATH}")
    target_link_libraries(orcacli_core ${LIBMINIZ_PATH})
endif()

# PNG (prefixed to prusaslicer_)
if(EXISTS "${LIBPNG_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libpng at: ${LIBPNG_PATH2}")
    target_link_libraries(orcacli_core ${LIBPNG_PATH2})
elseif(EXISTS "${LIBPNG_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libpng at: ${LIBPNG_PATH}")
    target_link_libraries(orcacli_core ${LIBPNG_PATH})
endif()

if(EXISTS "${LIBQHULL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libqhull at: ${LIBQHULL_PATH}")
    target_link_libraries(orcacli_core ${LIBQHULL_PATH})
endif()

# MPFR/GMP: prefer deps install prefix, include gmpxx when available. Order matters for static linking.
if(ENABLE_LIBSLIC3R)
    if(EXISTS "${LIBMPFR_PATH2}")
        message(STATUS "Found libmpfr at: ${LIBMPFR_PATH2}")
        target_link_libraries(orcacli_core ${LIBMPFR_PATH2})
    elseif(EXISTS "${LIBMPFR_PATH}")
        message(STATUS "Found libmpfr at: ${LIBMPFR_PATH}")
        target_link_libraries(orcacli_core ${LIBMPFR_PATH})
    endif()

    if(EXISTS "${LIBGMPXX_PATH2}")
        message(STATUS "Found libgmpxx at: ${LIBGMPXX_PATH2}")
        target_link_libraries(orcacli_core ${LIBGMPXX_PATH2})
    elseif(EXISTS "${LIBGMPXX_PATH}")
        message(STATUS "Found libgmpxx at: ${LIBGMPXX_PATH}")
        target_link_libraries(orcacli_core ${LIBGMPXX_PATH})
    endif()

    if(EXISTS "${LIBGMP_PATH2}")
        message(STATUS "Found libgmp at: ${LIBGMP_PATH2}")
        target_link_libraries(orcacli_core ${LIBGMP_PATH2})
    elseif(EXISTS "${LIBGMP_PATH}")
        message(STATUS "Found libgmp at: ${LIBGMP_PATH}")
        target_link_libraries(orcacli_core ${LIBGMP_PATH})
    endif()
endif()

if(EXISTS "${LIBQOI_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libqoi at: ${LIBQOI_PATH}")
    target_link_libraries(orcacli_core ${LIBQOI_PATH})
endif()

if(EXISTS "${LIBGLU_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libglu at: ${LIBGLU_PATH}")
    target_link_libraries(orcacli_core ${LIBGLU_PATH})
endif()

if(EXISTS "${LIBMCUT_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libmcut at: ${LIBMCUT_PATH}")
    target_link_libraries(orcacli_core ${LIBMCUT_PATH})
endif()

# JPEG
if(EXISTS "${LIBJPEG_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libjpeg at: ${LIBJPEG_PATH2}")
    target_link_libraries(orcacli_core ${LIBJPEG_PATH2})
elseif(EXISTS "${LIBJPEG_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libjpeg at: ${LIBJPEG_PATH}")
    target_link_libraries(orcacli_core ${LIBJPEG_PATH})
endif()

# Link Boost libraries (prefer deps install prefix)

# Prefer OCCT libs from deps install prefix if present (override paths)
set(_OCCT_LIBDIR "${ORCASLICER_DEPS_LIBDIR}")
if(EXISTS "${_OCCT_LIBDIR}/libTKernel.a")
    set(TKKERNEL_PATH "${_OCCT_LIBDIR}/libTKernel.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBO.a")
    set(TKBO_PATH "${_OCCT_LIBDIR}/libTKBO.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKSTEP.a")
    set(TKSTEP_PATH "${_OCCT_LIBDIR}/libTKSTEP.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKSTEPBase.a")
    set(TKSTEPBASE_PATH "${_OCCT_LIBDIR}/libTKSTEPBase.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXCAF.a")
    set(TKXCAF_PATH "${_OCCT_LIBDIR}/libTKXCAF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBinL.a")
    set(TKBINL_PATH "${_OCCT_LIBDIR}/libTKBinL.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXmlL.a")
    set(TKXMLL_PATH "${_OCCT_LIBDIR}/libTKXmlL.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKCAF.a")
    set(TKCAF_PATH "${_OCCT_LIBDIR}/libTKCAF.a")


endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKLCAF.a")
    set(TKLCAF_PATH "${_OCCT_LIBDIR}/libTKLCAF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBRep.a")
    set(TKBREP_PATH "${_OCCT_LIBDIR}/libTKBRep.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKTopAlgo.a")
    set(TKTOPALGO_PATH "${_OCCT_LIBDIR}/libTKTopAlgo.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKService.a")
    set(TKSERVICE_PATH "${_OCCT_LIBDIR}/libTKService.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKV3d.a")
    set(TKV3D_PATH "${_OCCT_LIBDIR}/libTKV3d.a")
endif()

# admesh (support both multi/single-config)
set(_ADMESH_CANDIDATES
    "${ORCASLICER_BUILD_DIR}/deps_src/admesh/Release/libadmesh.a"
    "${ORCASLICER_BUILD_DIR}/deps_src/admesh/libadmesh.a"
)
set(ADMESH_PATH2 "")
foreach(p IN LISTS _ADMESH_CANDIDATES)
    if(EXISTS "${p}")
        set(ADMESH_PATH2 "${p}")
        break()
    endif()
endforeach()

if(EXISTS "${_OCCT_LIBDIR}/libTKGeomAlgo.a")
    set(TKGEOMALGO_PATH "${_OCCT_LIBDIR}/libTKGeomAlgo.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKGeomBase.a")
    set(TKGEOMBASE_PATH "${_OCCT_LIBDIR}/libTKGeomBase.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKMesh.a")
    set(TKMESH_PATH "${_OCCT_LIBDIR}/libTKMesh.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKPrim.a")
    set(TKPRIM_PATH "${_OCCT_LIBDIR}/libTKPrim.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKG3d.a")
    set(TKG3D_PATH "${_OCCT_LIBDIR}/libTKG3d.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKMath.a")
    set(TKMATH_PATH "${_OCCT_LIBDIR}/libTKMath.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKShHealing.a")
    set(TKSHHEALING_PATH "${_OCCT_LIBDIR}/libTKShHealing.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKG2d.a")
    set(TKG2D_PATH "${_OCCT_LIBDIR}/libTKG2d.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKHLR.a")
    set(TKHLR_PATH "${_OCCT_LIBDIR}/libTKHLR.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKCDF.a")
    set(TKCDF_PATH "${_OCCT_LIBDIR}/libTKCDF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXDESTEP.a")
    set(TKXDESTEP_PATH "${_OCCT_LIBDIR}/libTKXDESTEP.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXmlXCAF.a")
    set(TKXMLXCAF_PATH "${_OCCT_LIBDIR}/libTKXmlXCAF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBinXCAF.a")
    set(TKBINXCAF_PATH "${_OCCT_LIBDIR}/libTKBinXCAF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKVCAF.a")
    set(TKVCAF_PATH "${_OCCT_LIBDIR}/libTKVCAF.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXSBase.a")
    set(TKXSBASE_PATH "${_OCCT_LIBDIR}/libTKXSBase.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBin.a")
    set(TKBIN_PATH "${_OCCT_LIBDIR}/libTKBin.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKBool.a")
    set(TKBOOL_PATH "${_OCCT_LIBDIR}/libTKBool.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKIGES.a")
    set(TKIGES_PATH "${_OCCT_LIBDIR}/libTKIGES.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKSTEP209.a")
    set(TKSTEP209_PATH "${_OCCT_LIBDIR}/libTKSTEP209.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKSTEPAttr.a")
    set(TKSTEPATTR_PATH "${_OCCT_LIBDIR}/libTKSTEPAttr.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKSTL.a")
    set(TKSTL_PATH "${_OCCT_LIBDIR}/libTKSTL.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKRWMesh.a")
    set(TKRWMESH_PATH "${_OCCT_LIBDIR}/libTKRWMesh.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKVRML.a")
    set(TKVRML_PATH "${_OCCT_LIBDIR}/libTKVRML.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXDEIGES.a")
    set(TKXDEIGES_PATH "${_OCCT_LIBDIR}/libTKXDEIGES.a")
endif()
if(EXISTS "${_OCCT_LIBDIR}/libTKXml.a")
    set(TKXML_PATH "${_OCCT_LIBDIR}/libTKXml.a")
endif()

# Prefer Freetype from deps install prefix
if(EXISTS "${ORCASLICER_DEPS_LIBDIR}/libfreetype.a")
    set(FREETYPE_PATH "${ORCASLICER_DEPS_LIBDIR}/libfreetype.a")
endif()

if(EXISTS "${BOOST_FILESYSTEM_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_filesystem at: ${BOOST_FILESYSTEM_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_FILESYSTEM_PATH2})
elseif(EXISTS "${BOOST_FILESYSTEM_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_filesystem at: ${BOOST_FILESYSTEM_PATH}")
    target_link_libraries(orcacli_core ${BOOST_FILESYSTEM_PATH})
endif()

if(EXISTS "${BOOST_LOG_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_log at: ${BOOST_LOG_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_LOG_PATH2})
elseif(EXISTS "${BOOST_LOG_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_log at: ${BOOST_LOG_PATH}")
    target_link_libraries(orcacli_core ${BOOST_LOG_PATH})


endif()

if(EXISTS "${BOOST_LOG_SETUP_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_log_setup at: ${BOOST_LOG_SETUP_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_LOG_SETUP_PATH2})
elseif(EXISTS "${BOOST_LOG_SETUP_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_log_setup at: ${BOOST_LOG_SETUP_PATH}")
    target_link_libraries(orcacli_core ${BOOST_LOG_SETUP_PATH})
endif()

if(EXISTS "${BOOST_THREAD_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_thread at: ${BOOST_THREAD_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_THREAD_PATH2})
elseif(EXISTS "${BOOST_THREAD_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_thread at: ${BOOST_THREAD_PATH}")
    target_link_libraries(orcacli_core ${BOOST_THREAD_PATH})
endif()

# Begin linker group to handle static OCCT cyclic deps
if(ENABLE_LIBSLIC3R)
    target_link_libraries(orcacli_core -Wl,--start-group)
endif()

# Link OpenCASCADE libraries
if(EXISTS "${TKKERNEL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKernel at: ${TKKERNEL_PATH}")
    target_link_libraries(orcacli_core ${TKKERNEL_PATH})


endif()

if(EXISTS "${TKBO_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBO at: ${TKBO_PATH}")
    target_link_libraries(orcacli_core ${TKBO_PATH})
endif()

if(EXISTS "${TKSTEP_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKSTEP at: ${TKSTEP_PATH}")
    target_link_libraries(orcacli_core ${TKSTEP_PATH})
endif()

if(EXISTS "${TKSTEPBASE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKSTEPBase at: ${TKSTEPBASE_PATH}")
    target_link_libraries(orcacli_core ${TKSTEPBASE_PATH})
endif()

if(EXISTS "${TKXCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXCAF at: ${TKXCAF_PATH}")
    target_link_libraries(orcacli_core ${TKXCAF_PATH})
endif()

if(EXISTS "${TKBINL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBinL at: ${TKBINL_PATH}")
    target_link_libraries(orcacli_core ${TKBINL_PATH})
endif()

if(EXISTS "${TKXMLL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXmlL at: ${TKXMLL_PATH}")
    target_link_libraries(orcacli_core ${TKXMLL_PATH})
endif()

# libnoise
if(ENABLE_LIBSLIC3R)
    if(EXISTS "${LIBNOISE_PATH2}")
        message(STATUS "Found libnoise at: ${LIBNOISE_PATH2}")
        target_link_libraries(orcacli_core ${LIBNOISE_PATH2})
    elseif(EXISTS "${LIBNOISE_PATH}")
        message(STATUS "Found libnoise at: ${LIBNOISE_PATH}")
        target_link_libraries(orcacli_core ${LIBNOISE_PATH})
    endif()
endif()

if(EXISTS "${LIBCLIPPER_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found libclipper at: ${LIBCLIPPER_PATH}")
    target_link_libraries(orcacli_core ${LIBCLIPPER_PATH})
endif()

if(EXISTS "${TKCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKCAF at: ${TKCAF_PATH}")
    target_link_libraries(orcacli_core ${TKCAF_PATH})
endif()

if(EXISTS "${TKLCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKLCAF at: ${TKLCAF_PATH}")
    target_link_libraries(orcacli_core ${TKLCAF_PATH})
endif()

if(EXISTS "${TKBREP_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBRep at: ${TKBREP_PATH}")
    target_link_libraries(orcacli_core ${TKBREP_PATH})
endif()

if(EXISTS "${TKTOPALGO_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKTopAlgo at: ${TKTOPALGO_PATH}")
    target_link_libraries(orcacli_core ${TKTOPALGO_PATH})
endif()

if(EXISTS "${TKSERVICE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKService at: ${TKSERVICE_PATH}")
    target_link_libraries(orcacli_core ${TKSERVICE_PATH})
endif()

if(EXISTS "${TKV3D_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKV3d at: ${TKV3D_PATH}")
    target_link_libraries(orcacli_core ${TKV3D_PATH})
endif()

if(EXISTS "${TKGEOMALGO_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKGeomAlgo at: ${TKGEOMALGO_PATH}")
    target_link_libraries(orcacli_core ${TKGEOMALGO_PATH})
endif()

if(EXISTS "${TKGEOMBASE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKGeomBase at: ${TKGEOMBASE_PATH}")
    target_link_libraries(orcacli_core ${TKGEOMBASE_PATH})
endif()

if(EXISTS "${TKMESH_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKMesh at: ${TKMESH_PATH}")
    target_link_libraries(orcacli_core ${TKMESH_PATH})
endif()

if(EXISTS "${TKPRIM_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKPrim at: ${TKPRIM_PATH}")
    target_link_libraries(orcacli_core ${TKPRIM_PATH})
endif()

if(EXISTS "${TKG3D_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKG3d at: ${TKG3D_PATH}")
    target_link_libraries(orcacli_core ${TKG3D_PATH})
endif()

if(EXISTS "${TKMATH_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKMath at: ${TKMATH_PATH}")
    target_link_libraries(orcacli_core ${TKMATH_PATH})
endif()

if(EXISTS "${TKSHHEALING_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKShHealing at: ${TKSHHEALING_PATH}")
    target_link_libraries(orcacli_core ${TKSHHEALING_PATH})
endif()

if(EXISTS "${TKG2D_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKG2d at: ${TKG2D_PATH}")
    target_link_libraries(orcacli_core ${TKG2D_PATH})
endif()

if(EXISTS "${TKHLR_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKHLR at: ${TKHLR_PATH}")
    target_link_libraries(orcacli_core ${TKHLR_PATH})
endif()

if(EXISTS "${BOOST_LOCALE_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_locale at: ${BOOST_LOCALE_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_LOCALE_PATH2})
elseif(EXISTS "${BOOST_LOCALE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_locale at: ${BOOST_LOCALE_PATH}")
    target_link_libraries(orcacli_core ${BOOST_LOCALE_PATH})
endif()

if(EXISTS "${BOOST_NOWIDE_PATH2}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_nowide at: ${BOOST_NOWIDE_PATH2}")
    target_link_libraries(orcacli_core ${BOOST_NOWIDE_PATH2})
elseif(EXISTS "${BOOST_NOWIDE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found boost_nowide at: ${BOOST_NOWIDE_PATH}")
    target_link_libraries(orcacli_core ${BOOST_NOWIDE_PATH})
endif()

if(EXISTS "${TKCDF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKCDF at: ${TKCDF_PATH}")
    target_link_libraries(orcacli_core ${TKCDF_PATH})
endif()

if(EXISTS "${TKXDESTEP_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXDESTEP at: ${TKXDESTEP_PATH}")
    target_link_libraries(orcacli_core ${TKXDESTEP_PATH})
endif()

if(EXISTS "${TKXMLXCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXmlXCAF at: ${TKXMLXCAF_PATH}")
    target_link_libraries(orcacli_core ${TKXMLXCAF_PATH})
endif()

if(EXISTS "${TKBINXCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBinXCAF at: ${TKBINXCAF_PATH}")
    target_link_libraries(orcacli_core ${TKBINXCAF_PATH})
endif()

if(EXISTS "${TKVCAF_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKVCAF at: ${TKVCAF_PATH}")
    target_link_libraries(orcacli_core ${TKVCAF_PATH})
endif()

if(ENABLE_LIBSLIC3R)
    if(EXISTS "${QHULLCPP_PATH2}")
        message(STATUS "Found qhullcpp at: ${QHULLCPP_PATH2}")
        target_link_libraries(orcacli_core ${QHULLCPP_PATH2})
    elseif(EXISTS "${QHULLCPP_PATH}")
        message(STATUS "Found qhullcpp at: ${QHULLCPP_PATH}")
        target_link_libraries(orcacli_core ${QHULLCPP_PATH})
    endif()
endif()


if(ENABLE_LIBSLIC3R)
    if(EXISTS "${LIBQHULL_PATH2}")
        message(STATUS "Found qhullstatic_r at: ${LIBQHULL_PATH2}")
        target_link_libraries(orcacli_core ${LIBQHULL_PATH2})
    elseif(EXISTS "${LIBQHULL_PATH}")
        message(STATUS "Found qhullstatic_r at: ${LIBQHULL_PATH}")
        target_link_libraries(orcacli_core ${LIBQHULL_PATH})
    endif()
endif()

if(EXISTS "${TKXSBASE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXSBase at: ${TKXSBASE_PATH}")
    target_link_libraries(orcacli_core ${TKXSBASE_PATH})
endif()

if(EXISTS "${TKBIN_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBin at: ${TKBIN_PATH}")
    target_link_libraries(orcacli_core ${TKBIN_PATH})
endif()

if(EXISTS "${TKBOOL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKBool at: ${TKBOOL_PATH}")
    target_link_libraries(orcacli_core ${TKBOOL_PATH})
endif()

if(EXISTS "${TKIGES_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKIGES at: ${TKIGES_PATH}")
    target_link_libraries(orcacli_core ${TKIGES_PATH})
endif()

if(EXISTS "${TKSTEP209_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKSTEP209 at: ${TKSTEP209_PATH}")
    target_link_libraries(orcacli_core ${TKSTEP209_PATH})
endif()

if(EXISTS "${TKSTEPATTR_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKSTEPAttr at: ${TKSTEPATTR_PATH}")
    target_link_libraries(orcacli_core ${TKSTEPATTR_PATH})
endif()

if(EXISTS "${TKSTL_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKSTL at: ${TKSTL_PATH}")
    target_link_libraries(orcacli_core ${TKSTL_PATH})
endif()

if(EXISTS "${TKRWMESH_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKRWMesh at: ${TKRWMESH_PATH}")
    target_link_libraries(orcacli_core ${TKRWMESH_PATH})
endif()

if(EXISTS "${TKVRML_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKVRML at: ${TKVRML_PATH}")
    target_link_libraries(orcacli_core ${TKVRML_PATH})
endif()

if(EXISTS "${TKXDEIGES_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXDEIGES at: ${TKXDEIGES_PATH}")
    target_link_libraries(orcacli_core ${TKXDEIGES_PATH})
endif()

if(EXISTS "${TKXML_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found TKXml at: ${TKXML_PATH}")
    target_link_libraries(orcacli_core ${TKXML_PATH})
endif()

if(ENABLE_LIBSLIC3R)
    if(EXISTS "${ADMESH_PATH2}")
        message(STATUS "Found admesh at: ${ADMESH_PATH2}")
        target_link_libraries(orcacli_core ${ADMESH_PATH2})
    elseif(EXISTS "${ADMESH_PATH}")
        message(STATUS "Found admesh at: ${ADMESH_PATH}")
        target_link_libraries(orcacli_core ${ADMESH_PATH})
    endif()
endif()

if(EXISTS "${FREETYPE_PATH}" AND ENABLE_LIBSLIC3R)
    message(STATUS "Found freetype at: ${FREETYPE_PATH}")
    target_link_libraries(orcacli_core ${FREETYPE_PATH})
endif()

# Close the linker group started before OCCT libs
if(ENABLE_LIBSLIC3R)
    target_link_libraries(orcacli_core -Wl,--end-group)
endif()

# Link system libraries for XML and crypto
if(ENABLE_LIBSLIC3R)
    find_library(EXPAT_LIB expat)


    if(EXPAT_LIB)
        message(STATUS "Found system expat at: ${EXPAT_LIB}")
        target_link_libraries(orcacli_core ${EXPAT_LIB})
    endif()

    # Link system crypto library for MD5
    find_library(CRYPTO_LIB crypto)
    if(CRYPTO_LIB)
        message(STATUS "Found system crypto at: ${CRYPTO_LIB}")
        target_link_libraries(orcacli_core ${CRYPTO_LIB})
    else()
        target_link_libraries(orcacli_core "-framework Security")
    endif()
endif()

# Link system zlib for PNG support
if(ENABLE_LIBSLIC3R)
    find_package(ZLIB REQUIRED)
    target_link_libraries(orcacli_core ${ZLIB_LIBRARIES})
endif()

# Platform-specific libraries
if(APPLE)
    find_library(FOUNDATION Foundation REQUIRED)
    find_library(MODELIO ModelIO REQUIRED)
    target_link_libraries(orcacli_core ${FOUNDATION} ${MODELIO})
    target_link_libraries(orcacli_core "-liconv -framework IOKit" "-framework CoreFoundation" -lc++)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(orcacli_core ${CMAKE_DL_LIBS})
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FONTCONFIG REQUIRED fontconfig)
    target_link_libraries(orcacli_core ${FONTCONFIG_LIBRARIES})
endif()

# Set include directories for core library
target_include_directories(orcacli_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${ORCASLICER_SRC_DIR}
    ${ORCASLICER_SRC_DIR}/libslic3r
    ${ORCASLICER_BUILD_DIR}/src/libslic3r  # For generated headers
)

# Add Boost include directories if found
if(Boost_FOUND)
    target_include_directories(orcacli_core PUBLIC ${Boost_INCLUDE_DIRS})
endif()

# Compiler definitions
target_compile_definitions(orcacli_core PRIVATE
    SLIC3R_VERSION="1.0.0"  # Default version
)

# Add Boost definitions if found
if(Boost_FOUND)
    target_compile_definitions(orcacli_core PRIVATE BOOST_LOG_DYN_LINK)
endif()


# Shared engine library for dynamic loading by the Node addon (delays libslic3r static inits)
add_library(orcacli_engine SHARED
    engine/EngineAPI.cpp
)

# Link the core into the engine
target_link_libraries(orcacli_engine PRIVATE orcacli_core)

# Define version string for the engine API (consumed by Node addon)
target_compile_definitions(orcacli_engine PRIVATE ORCACLI_VERSION_STRING=\"${PROJECT_VERSION}\")

# On macOS, produce liborcacli_engine.dylib under build/src by default
set_target_properties(orcacli_engine PROPERTIES
    OUTPUT_NAME "orcacli_engine"
)

# Also copy the engine library next to the Node addon output to simplify runtime loading
add_custom_command(TARGET orcacli_engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bindings/node"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:orcacli_engine>" "${CMAKE_BINARY_DIR}/bindings/node"
)

# Create main executable
add_executable(orcaslicer-cli ${ORCACLI_MAIN_SOURCES})

# Link core library to executable
target_link_libraries(orcaslicer-cli orcacli_core)

# Set executable properties
set_target_properties(orcaslicer-cli PROPERTIES
    OUTPUT_NAME "orcaslicer-cli"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Platform-specific executable settings
if(WIN32)
    set_target_properties(orcaslicer-cli PROPERTIES
        WIN32_EXECUTABLE FALSE  # Console application
    )
elseif(APPLE)
    set_target_properties(orcaslicer-cli PROPERTIES
        MACOSX_BUNDLE FALSE  # Command line tool
    )
endif()

# Install targets
install(TARGETS orcacli_core orcaslicer-cli
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)
