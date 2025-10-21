// This include file is intentionally included inside class CliCore::Impl
// to keep member function definitions inline without changing public APIs.
// It groups plate-centering related helpers to reduce the size of CliCore.cpp.

// Center currently loaded plate content onto the bed center by adjusting plate_origin
bool center_plate_origin_to_bed_center()
{
    if (!model || !print || !config) return false;
    try {
        // Determine bed center (in mm) from printable area
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

        // Compute world-space exact bounding box of all objects
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
        std::cout << "DEBUG: center_on_bed => plate_origin set to (" << origin_x << "," << origin_y << ") using model center=(" << cx << "," << cy << ") and bed center=(" << bed_cx_mm << "," << bed_cy_mm << ")" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cout << "WARN: center_plate_origin_to_bed_center failed: " << e.what() << std::endl;
        return false;
    }
}

// Shift instances so their center aligns with bed center (used for non-BBL vendors when center_on_bed=true)
bool center_instances_on_bed_center()
{
    if (!model || !config) return false;
    try {
        // Determine bed center (in mm) from printable area
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

        // Compute world-space exact bounding box of all objects
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
        }

        std::cout << "DEBUG: center_on_bed (instances) => shifted " << adjusted << " instances by (" << dx << "," << dy << ") bed_center=(" << bed_cx_mm << "," << bed_cy_mm << ") model_center=(" << cx << "," << cy << ")" << std::endl;
        return adjusted > 0;
    } catch (const std::exception &e) {
        std::cout << "WARN: center_instances_on_bed_center failed: " << e.what() << std::endl;
        return false;
    }
}

