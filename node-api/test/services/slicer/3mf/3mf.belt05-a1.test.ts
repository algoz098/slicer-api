/**
 * Teste: Fatiamento do arquivo Belt-05 com parametros Bambu Lab A1
 *
 * Este teste valida o fatiamento do arquivo Belt-05 (1).3mf usando
 * os parametros completos da impressora Bambu Lab A1 0.4 nozzle.
 */

import * as assert from 'node:assert'
import * as fs from 'node:fs'
import * as path from 'node:path'
import type { Server } from 'http'
import axios from 'axios'

describe('slicer/3mf: Belt-05 com A1 config', function () {
  this.timeout(300_000) // 5 minutos

  let server: Server | null = null
  let baseURL = ''
  let appRef: any = null

  // Arquivo de teste - Belt-05 (1).3mf
  const input3mf = '/Users/maosone/Downloads/Belt-05 (1).3mf'

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
  })

  after(async () => {
    if (appRef) await appRef.teardown()
  })

  it('deve fatiar Belt-05 usando config JSON A1 completo', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn(`Arquivo ${input3mf} nao encontrado, pulando teste`)
      return
    }

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'belt05_a1_config.gcode.3mf')

    // Config JSON completo da A1
    const bambuA1Config = {
      printer_technology: 'FFF',
      gcode_flavor: 'marlin',
      nozzle_diameter: ['0.4'],
      printer_model: 'Bambu Lab A1',
      printer_variant: '0.4',
      printer_structure: 'i3',
      printable_area: ['0x0', '256x0', '256x256', '0x256'],
      printable_height: '256',
      nozzle_type: 'stainless_steel',
      nozzle_volume: '92',
      auxiliary_fan: '0',
      single_extruder_multi_material: '1',
      machine_max_acceleration_x: ['12000', '12000'],
      machine_max_acceleration_y: ['12000', '12000'],
      machine_max_acceleration_z: ['1500', '1500'],
      machine_max_acceleration_extruding: ['12000', '12000'],
      machine_max_speed_z: ['30', '30'],
      retraction_length: ['0.8'],
      retraction_speed: ['30'],
      deretraction_speed: ['30'],
      retraction_minimum_travel: ['1'],
      z_hop: ['0.4'],
      layer_height: '0.2',
      initial_layer_print_height: '0.2',
      line_width: '0.42',
      initial_layer_line_width: '0.5',
      outer_wall_line_width: '0.42',
      inner_wall_line_width: '0.45',
      sparse_infill_line_width: '0.45',
      wall_loops: '2',
      top_shell_layers: '5',
      top_shell_thickness: '1.0',
      bottom_shell_layers: '3',
      sparse_infill_density: '15%',
      sparse_infill_pattern: 'crosshatch',
      top_surface_pattern: 'monotonicline',
      bottom_surface_pattern: 'monotonic',
      outer_wall_speed: '200',
      inner_wall_speed: '300',
      sparse_infill_speed: '270',
      internal_solid_infill_speed: '250',
      top_surface_speed: '200',
      gap_infill_speed: '250',
      travel_speed: '700',
      initial_layer_speed: '50',
      default_acceleration: '6000',
      initial_layer_acceleration: '500',
      outer_wall_acceleration: '5000',
      enable_support: '0',
      elefant_foot_compensation: '0.075',
      seam_position: 'aligned',
      enable_arc_fitting: '1',
      filament_type: ['PLA'],
      filament_vendor: ['Bambu Lab'],
      filament_density: ['1.26'],
      filament_diameter: ['1.75'],
      filament_flow_ratio: ['0.98'],
      filament_max_volumetric_speed: ['21'],
      nozzle_temperature: ['220'],
      nozzle_temperature_initial_layer: ['220'],
      nozzle_temperature_range_low: ['190'],
      nozzle_temperature_range_high: ['240'],
      hot_plate_temp: ['65'],
      hot_plate_temp_initial_layer: ['65'],
      fan_min_speed: ['60'],
      fan_max_speed: ['80'],
      fan_cooling_layer_time: ['80'],
      slow_down_layer_time: ['6'],
      close_fan_the_first_x_layers: ['1'],
      overhang_fan_speed: ['100']
    }

    const body = {
      filePath: input3mf,
      output: outTarget,
      plate: 1,
      config: bambuA1Config
    }

    console.log('[TEST] Iniciando fatiamento de Belt-05 com A1 config...')
    const startTime = Date.now()

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    const elapsed = Date.now() - startTime
    console.log(`[TEST] Fatiamento concluido em ${elapsed}ms`)
    console.log('[TEST] Response:', JSON.stringify(resp.data, null, 2))

    assert.strictEqual(
      resp.status,
      201,
      `Esperado status 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const data = resp.data
    assert.ok(typeof data.outputPath === 'string', 'outputPath deve existir')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Saida deve ser .gcode.3mf')
    assert.ok(fs.existsSync(data.outputPath), 'Arquivo deve existir no disco')

    console.log(`[TEST] Arquivo gerado: ${data.outputPath}`)
    console.log(`[TEST] Tamanho: ${data.size} bytes`)
    console.log(`[TEST] Tempo estimado: ${data.estimatedTimeSec}s`)
  })
})
