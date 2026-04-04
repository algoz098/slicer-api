#include "core/util/Utilities.hpp"

#include <iostream>
#include <fstream>
#include <cstdlib>

namespace OrcaSlicerCli { namespace util {

void dbg_log(const std::string& s)
{
    static std::ofstream __orcacli_dbg_file;
    static bool __orcacli_dbg_inited = false;
    if (!__orcacli_dbg_inited) {
        const char* p = std::getenv("ORCACLI_DEBUG_LOG_PATH");
        if (p && *p) {
            __orcacli_dbg_file.open(p, std::ios::app);
        }
        __orcacli_dbg_inited = true;
    }
    std::cout << s << std::endl;
    std::cout.flush();
    if (__orcacli_dbg_file.is_open()) {
        __orcacli_dbg_file << s << std::endl;
        __orcacli_dbg_file.flush();
    }
}

#if HAVE_LIBSLIC3R
std::string bed_temp_key_for(Slic3r::BedType type, bool first_layer)
{
    if (first_layer) {
        switch (type) {
            case Slic3r::btSuperTack: return "supertack_plate_temp_initial_layer";
            case Slic3r::btPC:        return "cool_plate_temp_initial_layer";
            case Slic3r::btPCT:       return "textured_cool_plate_temp_initial_layer";
            case Slic3r::btEP:        return "eng_plate_temp_initial_layer";
            case Slic3r::btPEI:       return "hot_plate_temp_initial_layer";
            case Slic3r::btPTE:       return "textured_plate_temp_initial_layer";
            default: return std::string();
        }
    } else {
        switch (type) {
            case Slic3r::btSuperTack: return "supertack_plate_temp";
            case Slic3r::btPC:        return "cool_plate_temp";
            case Slic3r::btPCT:       return "textured_cool_plate_temp";
            case Slic3r::btEP:        return "eng_plate_temp";
            case Slic3r::btPEI:       return "hot_plate_temp";
            case Slic3r::btPTE:       return "textured_plate_temp";
            default: return std::string();
        }
    }
}

// TODO: Implementação correta baseada no arquivo OrcaSlicer src/libslic3r/PresetBundle.cpp:2113
// PresetBundle::full_fff_config() usa exatamente a mesma ordem de aplicação:
//   1) FullPrintConfig::defaults()  2) prints  3) filaments.default_preset  4) printers  5) project_config
// ATENÇÃO: o OrcaSlicer depois aplica cada filamento individualmente (per-extruder overrides),
// o que aqui é simplificado usando apenas filaments.default_preset(). Isso pode causar divergência
// em configurações multi-material onde filamentos têm configs diferentes.
void safe_build_config(Slic3r::PresetBundle& preset_bundle, Slic3r::DynamicPrintConfig& config)
{
    try {
        Slic3r::DynamicPrintConfig out;
        out.apply(Slic3r::FullPrintConfig::defaults());
        try { out.apply(preset_bundle.prints.get_edited_preset().config); } catch (...) {}
        try { out.apply(preset_bundle.filaments.default_preset().config); } catch (...) {}
        try { out.apply(preset_bundle.printers.get_edited_preset().config); } catch (...) {}
        try { out.apply(preset_bundle.project_config, /*ignore_nonexistent=*/true); } catch (...) {}
        config = out;
    } catch (...) {
        // Keep existing config on failure
    }
}
#endif

}} // namespace OrcaSlicerCli::util

