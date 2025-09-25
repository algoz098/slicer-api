#!/usr/bin/env node
/*
  Upload a 3MF (multipart/form-data) to the slicer API using ONLY global constants.
  No CLI parâmetros são aceitos. Edite as constantes abaixo conforme necessário.

  Exemplo rápido: ajuste FILE/URL/OUT_BASE/ONLY_BASE64 e rode:
    node scripts/slice-3mf.js
*/

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');

// ===== Configuração através de variáveis globais (sem parâmetros de linha de comando) =====
const FILE = './example_files/3DBenchy.3mf';   // Caminho do arquivo 3MF de entrada
const URL = 'http://localhost:3030/slicer/3mf'; // Endpoint da API
const OUT_BASE = 'output';                      // Base do nome de saída (sem extensão)
const ONLY_BASE64 = false;                      // true para salvar apenas o .b64, false para salvar também o .3mf

// Hardcoded options overrides to be sent as the 'options' form field
const OPTIONS = { "adaptive_bed_mesh_margin": 0, "auxiliary_fan": 1, "bbl_use_printhost": 0, "bed_custom_model": "", "bed_custom_texture": "", "bed_exclude_area": "0x0", "bed_mesh_max": "99999,99999", "bed_mesh_min": "-99999,-99999", "bed_mesh_probe_distance": "50,50", "before_layer_change_gcode": ";BEFORE_LAYER_CHANGE\n;[layer_z]\nG92 E0\n", "best_object_pos": "0.5,0.5", "change_extrusion_role_gcode": "", "change_filament_gcode": "PAUSE", "cooling_tube_length": 5, "cooling_tube_retraction": 91.5, "default_filament_profile": "Creality HF Generic PLA", "default_print_profile": "0.20mm Standard @Creality K1Max (0.4 nozzle)", "deretraction_speed": 40, "disable_m73": 0, "emit_machine_limits_to_gcode": 1, "enable_filament_ramming": 1, "enable_long_retraction_when_cut": 0, "extra_loading_move": -2, "extruder_clearance_height_to_lid": 101, "extruder_clearance_height_to_rod": 45, "extruder_clearance_radius": 45, "extruder_colour": "#FCE94F", "extruder_offset": "0x0", "fan_kickstart": 0, "fan_speedup_overhangs": 1, "fan_speedup_time": 0, "from": "User", "gcode_flavor": "klipper", "high_current_on_filament_swap": 0, "host_type": "crealityprint", "inherits": "Creality K1 Max (0.4 nozzle)", "is_custom_defined": 0, "layer_change_gcode": "", "long_retractions_when_cut": 0, "machine_end_gcode": "END_PRINT", "machine_load_filament_time": 0, "machine_max_acceleration_e": 5000, "machine_max_acceleration_extruding": 20000, "machine_max_acceleration_retracting": 5000, "machine_max_acceleration_travel": 9000, "machine_max_acceleration_x": 20000, "machine_max_acceleration_y": 20000, "machine_max_acceleration_z": 500, "machine_max_jerk_e": 2.5, "machine_max_jerk_x": 12, "machine_max_jerk_y": 12, "machine_max_jerk_z": 2, "machine_max_speed_e": 100, "machine_max_speed_x": 800, "machine_max_speed_y": 800, "machine_max_speed_z": 20, "machine_min_extruding_rate": 0, "machine_min_travel_rate": 0, "machine_pause_gcode": "PAUSE", "machine_start_gcode": "M140 S0\nM104 S0 \nSTART_PRINT EXTRUDER_TEMP=[nozzle_temperature_initial_layer] BED_TEMP=[bed_temperature_initial_layer_single]", "machine_tool_change_time": 0, "machine_unload_filament_time": 0, "manual_filament_change": 1, "max_layer_height": 0.3, "min_layer_height": 0.08, "nozzle_diameter": 0.4, "nozzle_height": 2.5, "nozzle_hrc": 0, "nozzle_type": "hardened_steel", "nozzle_volume": 0, "parking_pos_retraction": 92, "pellet_modded_printer": 0, "preferred_orientation": 0, "printable_area": "0x0", "printable_height": 300, "printer_model": "Creality K1 Max", "printer_notes": "", "printer_settings_id": "K1 Max", "printer_structure": "undefine", "printer_technology": "FFF", "printer_variant": 0.4, "printhost_authorization_type": "key", "printhost_ssl_ignore_revoke": 1, "printing_by_object_gcode": "", "purge_in_prime_tower": 1, "retract_before_wipe": "0%", "retract_length_toolchange": 1, "retract_lift_above": 0, "retract_lift_below": 0, "retract_lift_enforce": "All Surfaces", "retract_on_top_layer": 1, "retract_restart_extra": 0, "retract_restart_extra_toolchange": 0, "retract_when_changing_layer": 1, "retraction_distances_when_cut": 18, "retraction_length": 0.6, "retraction_minimum_travel": 2, "retraction_speed": 40, "scan_first_layer": 0, "silent_mode": 0, "single_extruder_multi_material": 1, "support_air_filtration": 1, "support_chamber_temp_control": 0, "support_multi_bed_types": 1, "template_custom_gcode": "", "thumbnails": "100x100/PNG, 320x320/PNG", "thumbnails_format": "PNG", "time_cost": 0, "time_lapse_gcode": "", "travel_slope": 3, "use_firmware_retraction": 0, "use_relative_e_distances": 1, "wipe": 1, "wipe_distance": 2, "z_hop": 0.2, "z_hop_types": "Normal Lift", "z_offset": 0, "filament_flow_ratio": 1.0084, "filament_settings_id": "Voolt PLA", "filament_vendor": "Voolt", "from": "User", "hot_plate_temp": 75, "hot_plate_temp_initial_layer": 75, "inherits": "Generic PLA @System", "is_custom_defined": 0, "nozzle_temperature": 210, "nozzle_temperature_initial_layer": 210, "pressure_advance": 0.06, "textured_plate_temp": 75, "textured_plate_temp_initial_layer": 75 };

async function main() {
  const abs = path.resolve(FILE);

  try {
    await fsp.access(abs, fs.constants.R_OK);
  } catch {
    console.error(`Input file not found or not readable: ${abs}`);
    console.error('Atualize a constante FILE no scripts/slice-3mf.js');
    process.exit(1);
  }

  console.log(`Posting file to ${URL} ...`);

  // Use global FormData/Blob/fetch (Node 18+)
  const buf = await fsp.readFile(abs);
  const blob = new Blob([buf]);
  const form = new FormData();
  form.append('file', blob, path.basename(abs));

  // Append hardcoded options
  form.append('options', JSON.stringify(OPTIONS));

  const res = await fetch(URL, { method: 'POST', body: form });
  let bodyText = await res.text();
  if (!res.ok) {
    console.error(`Request failed. HTTP ${res.status}\nResponse: ${bodyText}`);
    process.exit(1);
  }

  let json;
  try {
    json = JSON.parse(bodyText);
  } catch (e) {
    console.error('Failed to parse JSON response:', e);
    console.error('Raw response:', bodyText);
    process.exit(1);
  }

  if (!json || !json.dataBase64) {
    console.error('Response did not contain dataBase64. Full response below:');
    console.error(JSON.stringify(json, null, 2));
    process.exit(1);
  }

  const b64Path = `${OUT_BASE}.b64`;
  await fsp.writeFile(b64Path, String(json.dataBase64), 'utf8');
  console.log(`Saved base64 to ${b64Path}`);

  if (!ONLY_BASE64) {
    const outPath = `${OUT_BASE}.3mf`;
    await fsp.writeFile(outPath, Buffer.from(String(json.dataBase64), 'base64'));
    console.log(`Saved decoded file to ${outPath}`);
  }

  if (json.outputPath) {
    console.log(`Server-side output path: ${json.outputPath}`);
  }
}

main().catch((err) => {
  console.error(err?.stack || err?.message || String(err));
  process.exit(1);
});
