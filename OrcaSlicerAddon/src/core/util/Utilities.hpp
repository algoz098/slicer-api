#pragma once

#include <string>

#if HAVE_LIBSLIC3R
#include "libslic3r/PrintConfig.hpp" // For Slic3r::BedType
#include "libslic3r/PresetBundle.hpp"
#endif

namespace OrcaSlicerCli { namespace util {

// Debug logging helper: duplicates to stdout and to file if ORCACLI_DEBUG_LOG_PATH is set
void dbg_log(const std::string& s);

#if HAVE_LIBSLIC3R
// Map bed type and layer to the correct bed temperature config key used by libslic3r
std::string bed_temp_key_for(Slic3r::BedType type, bool first_layer);

// Build config safely without using full_config_secure() which can hang on certain preset configurations
void safe_build_config(Slic3r::PresetBundle& preset_bundle, Slic3r::DynamicPrintConfig& config);
#endif

}} // namespace OrcaSlicerCli::util

