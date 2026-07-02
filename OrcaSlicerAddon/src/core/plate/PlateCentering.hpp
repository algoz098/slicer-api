#pragma once

// Helpers for centering plates and instances on the print bed.
// Independent from the public CliCore API; callable from implementation.

namespace Slic3r {
    class Model;
    class Print;
    class DynamicPrintConfig;
}

namespace OrcaSlicerCli {
namespace plate {

// Compute and set plate_origin from model instances (assembly offsets)
bool compute_and_set_plate_origin_from_model_instances(Slic3r::Model* model,
                                                       Slic3r::Print* print,
                                                       Slic3r::DynamicPrintConfig* config);

// Center current plate content by adjusting Print::plate_origin.
bool center_plate_origin_to_bed_center(Slic3r::Model* model,
                                       Slic3r::Print* print,
                                       Slic3r::DynamicPrintConfig* config);

// Shift instances to align model center with bed center (non-BBL vendors).
bool center_instances_on_bed_center(Slic3r::Model* model,
                                    Slic3r::DynamicPrintConfig* config);

// Drop floating instances (min Z > 0) onto the bed; sunken instances (min Z < 0)
// are preserved so the slicer cuts below Z=0 (GUI "cut bottom" behavior).
bool drop_floating_instances_to_bed(Slic3r::Model* model);

// Normalize model instances into plate-local coordinates by removing the logical grid stride
bool normalize_model_instances_to_plate_local(Slic3r::Model* model,
                                              Slic3r::DynamicPrintConfig* config);

} // namespace plate
} // namespace OrcaSlicerCli

