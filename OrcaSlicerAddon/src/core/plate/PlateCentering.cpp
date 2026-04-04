#include "core/plate/PlateCentering.hpp"

#if HAVE_LIBSLIC3R

#include <limits>
#include <iostream>
#include <algorithm>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/BuildVolume.hpp"
#include <cmath>


namespace OrcaSlicerCli {
namespace plate {

// TODO: Implementação correta baseada no arquivo OrcaSlicer src/slic3r/GUI/PartPlate.cpp:54 e :3200-3201
// LOGICAL_PART_PLATE_GAP = 1/5 e formula origin(col * stride, -row * stride) são idênticos ao PartPlate.
// Esta função é necessária pois o GUI usa PartPlate para manter offsets de assembly; no CLI precisamos
// reconstruir o plate_origin a partir dos offsets de instância para que o G-code seja plate-local.
bool compute_and_set_plate_origin_from_model_instances(Slic3r::Model* model,
                                                       Slic3r::Print* print,
                                                       Slic3r::DynamicPrintConfig* config)
{
    if (!model || !print || !config) return false;
    try {
        Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
        if (bed_pts.empty()) return false;
        long minx = std::numeric_limits<long>::max();
        long maxx = std::numeric_limits<long>::min();
        long miny = std::numeric_limits<long>::max();
        long maxy = std::numeric_limits<long>::min();
        for (const auto &p : bed_pts) {
            if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x();
            if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
        }
        const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
        const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
        if (!(bed_w_mm > 0.0 && bed_d_mm > 0.0)) return false;
        constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
        const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
        const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);

        bool origin_found = false;
        double origin_x = 0.0, origin_y = 0.0;
        for (auto *obj : model->objects) {
            for (auto *inst : obj->instances) {
                Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                const double col = std::round(aoff(0) / stride_x);
                const double row = std::round(-aoff(1) / stride_y);
                origin_x = col * stride_x;
                origin_y = -row * stride_y; // GUI uses negative Y per row
                origin_found = true;
                break;
            }
            if (origin_found) break;
        }
        if (!origin_found) return false;

        print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
        std::cout << "DEBUG: plate_origin (from instance assembly offsets) => origin=(" << origin_x << "," << origin_y
                  << ") stride=(" << stride_x << "," << stride_y << ")" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cout << "WARN: compute_and_set_plate_origin_from_model_instances failed: " << e.what() << std::endl;
        return false;
    }
}

// TODO: verificar se podemos remover esse codigo
// OrcaSlicer GUI não usa esta abordagem diretamente (usa center_instances_on_bed_center abaixo ou
// PartPlate::set_plate_origin). Esta função seta plate_origin em vez de mover as instâncias.
// Ela não é chamada no caminho principal (AddonCore usa center_instances_on_bed_center para non-BBL).
bool center_plate_origin_to_bed_center(Slic3r::Model* model,
                                       Slic3r::Print* print,
                                       Slic3r::DynamicPrintConfig* config)
{
    if (!model || !print || !config) return false;
    try {
        Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
        if (bed_pts.empty()) return false;
        long minx = std::numeric_limits<long>::max();
        long maxx = std::numeric_limits<long>::min();
        long miny = std::numeric_limits<long>::max();
        long maxy = std::numeric_limits<long>::min();
        for (const auto &p : bed_pts) {
            if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x();
            if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
        }
        const double bed_cx_mm = Slic3r::unscale<double>(minx + (maxx - minx) / 2);
        const double bed_cy_mm = Slic3r::unscale<double>(miny + (maxy - miny) / 2);

        Slic3r::BoundingBoxf3 all_bb;
        bool all_bb_init = false;
        for (size_t oi = 0; oi < model->objects.size(); ++oi) {
            const Slic3r::ModelObject *obj = model->objects[oi];
            if (!obj) continue;
            const auto &bb = obj->bounding_box_exact();
            if (bb.defined) {
                if (!all_bb_init) { all_bb = bb; all_bb_init = true; }
                else { all_bb.merge(bb); }
            }
        }
        if (!all_bb_init) return false;
        const double cx = 0.5 * (all_bb.min.x() + all_bb.max.x());
        const double cy = 0.5 * (all_bb.min.y() + all_bb.max.y());

        const double origin_x = cx - bed_cx_mm;
        const double origin_y = cy - bed_cy_mm;
        print->set_plate_origin(Slic3r::Vec3d(origin_x, origin_y, 0.0));
        std::cout << "DEBUG: center_on_bed => plate_origin set to (" << origin_x << "," << origin_y
                  << ") using model center=(" << cx << "," << cy << ") and bed center=(" << bed_cx_mm
                  << "," << bed_cy_mm << ")" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cout << "WARN: center_plate_origin_to_bed_center failed: " << e.what() << std::endl;
        return false;
    }
}

// TODO: Implementação correta baseada no arquivo OrcaSlicer src/libslic3r/Model.cpp:686
// Model::center_instances_around_point() faz o mesmo: calcula delta entre centro do modelo
// e o ponto alvo (bed center) e aplica a todas as instâncias. Nossa implementação é equivalente.
// Necessário pois o GUI chama center_instances_around_point via Plater (AppConfig autocenter).
bool center_instances_on_bed_center(Slic3r::Model* model,
                                    Slic3r::DynamicPrintConfig* config)
{
    if (!model || !config) return false;
    try {
        Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
        if (bed_pts.empty()) return false;
        long minx = std::numeric_limits<long>::max();
        long maxx = std::numeric_limits<long>::min();
        long miny = std::numeric_limits<long>::max();
        long maxy = std::numeric_limits<long>::min();
        for (const auto &p : bed_pts) {
            if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x();
            if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
        }
        const double bed_cx_mm = Slic3r::unscale<double>(minx + (maxx - minx) / 2);
        const double bed_cy_mm = Slic3r::unscale<double>(miny + (maxy - miny) / 2);

        Slic3r::BoundingBoxf3 all_bb;
        bool all_bb_init = false;
        for (size_t oi = 0; oi < model->objects.size(); ++oi) {
            const Slic3r::ModelObject *obj = model->objects[oi];
            if (!obj) continue;
            const auto &bb = obj->bounding_box_exact();
            if (bb.defined) {
                if (!all_bb_init) { all_bb = bb; all_bb_init = true; }
                else { all_bb.merge(bb); }
            }
        }
        if (!all_bb_init) return false;
        const double cx = 0.5 * (all_bb.min.x() + all_bb.max.x());
        const double cy = 0.5 * (all_bb.min.y() + all_bb.max.y());

        const double dx = bed_cx_mm - cx;
        const double dy = bed_cy_mm - cy;

        size_t adjusted = 0;
        for (auto *obj : model->objects) {
            if (!obj) continue;
            for (auto *inst : obj->instances) {
                auto tf = inst->get_transformation();
                Slic3r::Vec3d toff = tf.get_offset();
                toff(0) += dx; toff(1) += dy;
                tf.set_offset(toff);
                inst->set_transformation(tf);
                ++adjusted;
            }
            // Invalidate bounding box so instance_bounding_box() returns updated values
            obj->invalidate_bounding_box();
        }

        std::cout << "DEBUG: center_on_bed (instances) => shifted " << adjusted
                  << " instances by (" << dx << "," << dy << ") bed_center=(" << bed_cx_mm
                  << "," << bed_cy_mm << ") model_center=(" << cx << "," << cy << ")" << std::endl;
        return adjusted > 0;
    } catch (const std::exception &e) {
        std::cout << "WARN: center_instances_on_bed_center failed: " << e.what() << std::endl;
        return false;
    }
}

// TODO: Implementação correta baseada no arquivo OrcaSlicer src/slic3r/GUI/PartPlate.cpp:3200-3201
// Usa a mesma constante LOGICAL_PART_PLATE_GAP = 1/5 e a mesma fórmula de stride que o PartPlate.
// Necessário pois no GUI o PartPlate mantém os assembly offsets de placa; no CLI precisamos subtrair
// o offset da placa para que as instâncias fiquem em coordenadas plate-local antes do slicing.
bool normalize_model_instances_to_plate_local(Slic3r::Model* model,
                                              Slic3r::DynamicPrintConfig* config)
{
    if (!model || !config) return false;
    try {
        Slic3r::Points bed_pts = Slic3r::get_bed_shape(*config);
        if (bed_pts.empty()) return false;
        long minx = std::numeric_limits<long>::max();
        long maxx = std::numeric_limits<long>::min();
        long miny = std::numeric_limits<long>::max();
        long maxy = std::numeric_limits<long>::min();
        for (const auto &p : bed_pts) {
            if (p.x() < minx) minx = p.x(); if (p.x() > maxx) maxx = p.x(); if (p.y() < miny) miny = p.y(); if (p.y() > maxy) maxy = p.y();
        }
        const double bed_w_mm = Slic3r::unscale<double>(maxx - minx);
        const double bed_d_mm = Slic3r::unscale<double>(maxy - miny);
        if (!(bed_w_mm > 0.0 && bed_d_mm > 0.0)) return false;
        constexpr double LOGICAL_PART_PLATE_GAP = 1.0 / 5.0;
        const double stride_x = bed_w_mm * (1.0 + LOGICAL_PART_PLATE_GAP);
        const double stride_y = bed_d_mm * (1.0 + LOGICAL_PART_PLATE_GAP);

        bool origin_found = false;
        double asm_origin_x = 0.0, asm_origin_y = 0.0;
        for (auto *obj : model->objects) {
            for (auto *inst : obj->instances) {
                Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                const double col = std::round(aoff(0) / stride_x);
                const double row = std::round(-aoff(1) / stride_y);
                asm_origin_x = col * stride_x;
                asm_origin_y = -row * stride_y; // GUI uses negative Y per row
                origin_found = true;
                break;
            }
            if (origin_found) break;
        }
        if (!origin_found) return false;

        size_t adjusted = 0;
        for (auto *obj : model->objects) {
            for (auto *inst : obj->instances) {
                Slic3r::Vec3d toff = inst->get_transformation().get_offset();
                toff(0) -= asm_origin_x; toff(1) -= asm_origin_y;
                inst->set_offset(toff);
                Slic3r::Vec3d aoff = inst->get_offset_to_assembly();
                aoff(0) -= asm_origin_x; aoff(1) -= asm_origin_y;
                inst->set_offset_to_assembly(aoff);
                ++adjusted;
            }
        }
        std::cout << "DEBUG: normalized instances to plate-local FROM assembly: asm_origin=(" << asm_origin_x << "," << asm_origin_y
                  << ") stride=(" << stride_x << "," << stride_y << ") adjusted_instances=" << adjusted << std::endl;
        return adjusted > 0;
    } catch (...) { return false; }
}

} // namespace plate
} // namespace OrcaSlicerCli

#else

namespace OrcaSlicerCli { namespace plate {
bool compute_and_set_plate_origin_from_model_instances(void*, void*, void*) { return false; }
bool center_plate_origin_to_bed_center(void*, void*, void*) { return false; }
bool center_instances_on_bed_center(void*, void*) { return false; }
bool normalize_model_instances_to_plate_local(void*, void*) { return false; }
}} // namespaces

#endif

