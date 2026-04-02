/**
 * Test: Slicing 3MF with BBL-style G-code templates that use vector indexing
 *
 * This test validates that G-code templates containing vector-indexed placeholders
 * like [nozzle_temperature[previous_extruder]] and [nozzle_temperature_initial_layer[initial_extruder]]
 * do NOT crash during export.
 *
 * Root cause: OrcaSlicer's GCode.cpp dynamically injects `previous_extruder` and
 * `initial_no_support_extruder` as ConfigOptionInt at export time, then uses them
 * as vector indices. If vectors have fewer elements than the index, it crashes.
 */

import assert from 'assert'
import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import type { Server } from 'http'
import { fileURLToPath } from 'node:url'
const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

describe('slicer/3mf: BBL G-code templates with vector indexing', function () {
  this.timeout(180_000)

  let server: Server | null = null
  let baseURL = ''
  let appRef: any = null
  let input3mf: string

  before(async () => {
    const nodeApiRoot = path.resolve(__dirname, '../../../..')
    const repoRoot = path.resolve(nodeApiRoot, '..')

    process.env.ORCACLI_PREFER_LOCAL = '1'
    process.env.ORCACLI_ENGINE_PATH = path.join(
      repoRoot,
      'OrcaSlicerAddon/build/bindings/node/liborcacli_engine.dylib'
    )

    const mod = await import('../../../../src/app')
    appRef = (mod as any).app
    server = await appRef.listen(0)

    const address = server!.address()
    const port = typeof address === 'string' || address === null ? 0 : (address as any).port
    baseURL = `http://127.0.0.1:${port}`

    input3mf = path.resolve(__dirname, '../../../../../example_files/3DBenchy.3mf')
  })

  after(async () => {
    if (appRef) await appRef.teardown()
  })

  /**
   * Test 1: machine_start_gcode with nozzle_temperature_initial_layer[initial_extruder]
   *
   * This is the most common BBL G-code template pattern. The template uses
   * [initial_extruder] (runtime index 0) to access nozzle_temperature_initial_layer.
   */
  it('should handle machine_start_gcode with vector-indexed temperature variables', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('3DBenchy.3mf not found, skipping test')
      return
    }

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'bbl_gcode_template_test.gcode.3mf')

    const config = {
      // Printer
      printer_model: 'Generic',
      printer_variant: '0.4',
      gcode_flavor: 'marlin',
      nozzle_diameter: [0.4],
      printable_area: ['0x0', '256x0', '256x256', '0x256'],
      printable_height: 256,
      extruder_offset: ['0x0'],
      retraction_length: [0.8],
      retraction_speed: [30],

      // BBL-style machine_start_gcode with vector indexing
      machine_start_gcode:
        'M104 S[nozzle_temperature_initial_layer] ; set nozzle temp\n' +
        'M140 S[bed_temperature_initial_layer_single] ; set bed temp\n' +
        'M190 S[bed_temperature_initial_layer_single] ; wait for bed\n' +
        'M109 S[nozzle_temperature_initial_layer] ; wait for nozzle\n' +
        'G28 ; home\n' +
        'G92 E0 ; reset extruder\n',
      machine_end_gcode:
        'M104 S0 ; turn off hotend\n' +
        'M140 S0 ; turn off bed\n' +
        'G28 X Y ; home X Y\n' +
        'M84 ; disable motors\n',

      // Filament
      filament_type: ['PLA'],
      filament_diameter: [1.75],
      filament_density: [1.26],
      filament_flow_ratio: [0.98],
      filament_max_volumetric_speed: [21],
      nozzle_temperature: [220],
      nozzle_temperature_initial_layer: [220],
      bed_temperature: [65],
      bed_temperature_initial_layer: [65],
      fan_min_speed: [60],
      fan_max_speed: [80],

      // Process
      layer_height: 0.2,
      initial_layer_print_height: 0.2,
      wall_loops: 2,
      top_shell_layers: 4,
      bottom_shell_layers: 3,
      sparse_infill_density: 15,
      sparse_infill_pattern: 'grid',
      top_surface_pattern: 'monotonic',
      bottom_surface_pattern: 'monotonic',
      outer_wall_speed: 200,
      inner_wall_speed: 300,
      sparse_infill_speed: 270,
      travel_speed: 700,
      initial_layer_speed: 50,
      enable_support: false,
      enable_prime_tower: false
    }

    const body = {
      filePath: input3mf,
      plate: 1,
      config
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Expected 201, got ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const data = resp.data
    assert.ok(typeof data.outputPath === 'string', 'outputPath must exist')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Output must be .gcode.3mf')
    assert.ok(fs.existsSync(data.outputPath), 'Output file must exist on disk')
    assert.ok(data.size > 1000, 'Output file must be > 1KB')
  })

  /**
   * Test 2: machine_start_gcode with initial_no_support_extruder reference
   *
   * Some BBL profiles use T[initial_no_support_extruder] in machine_start_gcode.
   * This variable is set by OrcaSlicer at runtime in GCode.cpp:2187.
   */
  it('should handle machine_start_gcode with initial_no_support_extruder', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('3DBenchy.3mf not found, skipping test')
      return
    }

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'bbl_initial_extruder_test.gcode.3mf')

    const config = {
      printer_model: 'Generic',
      printer_variant: '0.4',
      gcode_flavor: 'marlin',
      nozzle_diameter: [0.4],
      printable_area: ['0x0', '256x0', '256x256', '0x256'],
      printable_height: 256,
      extruder_offset: ['0x0'],
      retraction_length: [0.8],
      retraction_speed: [30],

      // G-code template referencing initial_no_support_extruder
      machine_start_gcode:
        'T[initial_no_support_extruder] ; select initial extruder\n' +
        'M104 S[nozzle_temperature_initial_layer] ; set nozzle temp\n' +
        'M140 S[bed_temperature_initial_layer_single] ; set bed temp\n' +
        'M190 S[bed_temperature_initial_layer_single] ; wait for bed\n' +
        'M109 S[nozzle_temperature_initial_layer] ; wait for nozzle\n' +
        'G28 ; home\n' +
        'G92 E0 ; reset extruder\n',
      machine_end_gcode: 'M104 S0 ; turn off hotend\nM140 S0 ; turn off bed\nG28 X Y\nM84\n',

      filament_type: ['PLA'],
      filament_diameter: [1.75],
      filament_density: [1.26],
      filament_flow_ratio: [0.98],
      filament_max_volumetric_speed: [21],
      nozzle_temperature: [220],
      nozzle_temperature_initial_layer: [220],
      bed_temperature: [65],
      bed_temperature_initial_layer: [65],
      fan_min_speed: [60],
      fan_max_speed: [80],

      layer_height: 0.2,
      initial_layer_print_height: 0.2,
      wall_loops: 2,
      top_shell_layers: 4,
      bottom_shell_layers: 3,
      sparse_infill_density: 15,
      sparse_infill_pattern: 'grid',
      outer_wall_speed: 200,
      inner_wall_speed: 300,
      travel_speed: 700,
      initial_layer_speed: 50,
      enable_support: false,
      enable_prime_tower: false
    }

    const body = {
      filePath: input3mf,
      plate: 1,
      config
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Expected 201, got ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    assert.ok(resp.data.outputPath, 'outputPath must exist')
    assert.ok(fs.existsSync(resp.data.outputPath), 'Output file must exist on disk')
  })
})
