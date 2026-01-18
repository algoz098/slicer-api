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

