/**
 * TDD Tests: Slicing com configuracao completa via JSON
 *
 * Estes testes validam a nova funcionalidade de enviar configuracoes
 * completas de impressora/filamento/processo como JSON, sem depender
 * de perfis pre-existentes no disco.
 *
 * IMPORTANTE: Estes testes devem FALHAR inicialmente (TDD red phase).
 * A implementacao sera feita apos os testes estarem escritos.
 */

import assert from 'assert'
import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import type { Server } from 'http'

describe('slicer/stl service (config JSON completo - TDD)', function () {
  this.timeout(180000)

  let server: Server | null = null
  let baseURL = ''
  let appRef: any = null
  let stlPath: string

  before(async () => {
    // Setup environment
    const nodeApiRoot = path.resolve(__dirname, '../../../..')
    const repoRoot = path.resolve(nodeApiRoot, '..')

    process.env.ORCACLI_PREFER_LOCAL = '1'
    process.env.ORCACLI_ENGINE_PATH = path.join(
      repoRoot,
      'OrcaSlicerAddon/build/bindings/node/liborcacli_engine.dylib'
    )

    // Import app
    const mod = await import('../../../../src/app')
    appRef = (mod as any).app
    server = await appRef.listen(0)

    const address = server!.address()
    const port = typeof address === 'string' || address === null ? 0 : (address as any).port
    baseURL = `http://127.0.0.1:${port}`

    // Prepare STL path
    stlPath = path.resolve(__dirname, '../../../../../example_files/3DBenchy.stl')
    if (!fs.existsSync(stlPath)) {
      // Fallback: create minimal STL
      stlPath = path.join(__dirname, 'tmp_config_test.stl')
      const ascii = [
        'solid tri',
        ' facet normal 0 0 1',
        '  outer loop',
        '   vertex 0 0 0',
        '   vertex 10 0 0',
        '   vertex 5 10 5',
        '  endloop',
        ' endfacet',
        'endsolid tri'
      ].join('\n')
      fs.writeFileSync(stlPath, ascii, 'utf8')
    }
  })

  after(async () => {
    if (appRef) await appRef.teardown()
  })

  /**
   * Teste 1: Slicing com config JSON completo (sem perfis)
   *
   * Este teste valida que e possivel fazer slicing enviando apenas
   * um objeto `config` com todas as configuracoes necessarias,
   * sem precisar especificar printerProfile/filamentProfile/processProfile.
   */
  it('deve fazer slicing usando apenas config JSON, sem perfis pre-existentes', async () => {
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'tdd_config_json_only.gcode')

    // Config JSON completo com parametros essenciais para slicing
    // Estes sao os parametros minimos que o OrcaSlicer precisa
    const config = {
      // Parametros de impressora
      printer_model: 'Generic',
      nozzle_diameter: 0.4,
      bed_shape: '0x0,300x0,300x300,0x300',
      max_print_height: 300,
      printable_area: '0x0,300x0,300x300,0x300',

      // Parametros de filamento
      filament_type: 'PLA',
      filament_diameter: 1.75,
      temperature_vitrification: 60,
      nozzle_temperature: 220,
      nozzle_temperature_initial_layer: 220,
      bed_temperature: 60,
      bed_temperature_initial_layer: 60,

      // Parametros de processo
      layer_height: 0.2,
      initial_layer_height: 0.2,
      wall_loops: 2,
      top_shell_layers: 4,
      bottom_shell_layers: 3,
      sparse_infill_density: 15,
      sparse_infill_pattern: 'grid',
      initial_layer_speed: 30,
      outer_wall_speed: 60,
      inner_wall_speed: 80,
      sparse_infill_speed: 100,
      travel_speed: 200
    }

    const body = {
      filePath: stlPath,
      output: outTarget,
      // NOVO CAMPO: config JSON completo
      config
      // Note: SEM printerProfile, filamentProfile, processProfile
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    // Validacoes
    assert.strictEqual(
      resp.status,
      201,
      `Esperado status 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const data = resp.data
    assert.ok(typeof data === 'object' && data !== null, 'Resposta deve ser um objeto')
    assert.ok(typeof data.outputPath === 'string', 'outputPath deve existir')
    assert.ok(typeof data.gcode === 'string', 'gcode deve existir')
    assert.ok(data.gcode.length > 100, 'gcode deve ter conteudo significativo')

    // Verificar que as configuracoes foram aplicadas
    const gcode = data.gcode as string
    assert.ok(/layer_height\s*=\s*0\.2\b/.test(gcode), 'layer_height deve aparecer no G-code')
    assert.ok(/sparse_infill_density\s*=\s*15/.test(gcode), 'sparse_infill_density deve aparecer')

    // Verificar arquivo no disco
    assert.ok(fs.existsSync(outTarget), 'Arquivo de saida deve existir no disco')
  })

  /**
   * Teste 2: Config JSON com perfil base + sobrescrita
   *
   * Este teste valida que e possivel usar um perfil existente como base
   * e sobrescrever configuracoes especificas via config JSON.
   * A precedencia deve ser: config > options > profile
   */
  it('config JSON deve ter precedencia sobre perfis e options', async () => {
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'tdd_config_precedence.gcode')

    // Config base com parametros minimos
    const baseConfig = {
      printer_model: 'Bambu Lab A1',
      nozzle_diameter: 0.4,
      printable_area: '0x0,256x0,256x256,0x256',
      printable_height: 256,
      layer_height: 0.2,
      wall_loops: 2,
      sparse_infill_density: 15,
      filament_type: 'PLA',
      nozzle_temperature: 220,
      bed_temperature: 60
    }

    const body = {
      filePath: stlPath,
      output: outTarget,
      // Config base
      config: {
        ...baseConfig,
        // Config: define layer_height como 0.12 (deve ter MAIOR precedencia)
        layer_height: 0.12,
        sparse_infill_density: 25
      },
      // Options: tenta definir layer_height como 0.16 (config deve ter precedencia)
      options: {
        layer_height: 0.16
      }
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Esperado status 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const gcode = resp.data.gcode as string

    // Config deve ter precedencia: layer_height deve ser 0.12, nao 0.16 ou 0.20
    assert.ok(
      /layer_height\s*=\s*0\.12\b/.test(gcode),
      'layer_height deve ser 0.12 (definido em config, nao options ou profile)'
    )
    assert.ok(
      /sparse_infill_density\s*=\s*25/.test(gcode),
      'sparse_infill_density deve ser 25 (definido em config)'
    )
  })

  /**
   * Teste 3: Keys desconhecidas em config sao ignoradas silenciosamente
   *
   * O OrcaSlicer ignora silenciosamente keys desconhecidas e as lista
   * em ignoredOptions na resposta. Este e o comportamento esperado do engine.
   */
  it('deve ignorar silenciosamente keys desconhecidas em config e retornar sucesso', async () => {
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'tdd_config_unknown_keys.gcode')

    // Config base com parametros minimos
    const baseConfig = {
      printer_model: 'Bambu Lab A1',
      nozzle_diameter: 0.4,
      printable_area: '0x0,256x0,256x256,0x256',
      printable_height: 256,
      wall_loops: 2,
      sparse_infill_density: 15,
      filament_type: 'PLA',
      nozzle_temperature: 220,
      bed_temperature: 60
    }

    const body = {
      filePath: stlPath,
      output: outTarget,
      // Config com key invalida - sera ignorada
      config: {
        ...baseConfig,
        layer_height: 0.2,
        this_key_does_not_exist_in_orcaslicer: 'invalid_value',
        another_fake_key: 123
      }
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    // O slicing deve ter sucesso mesmo com keys desconhecidas
    assert.strictEqual(resp.status, 201, `Slicing deve ter sucesso, recebido ${resp.status}`)

    // Keys validas devem ser aplicadas
    const gcode = resp.data.gcode as string
    assert.ok(/layer_height\s*=\s*0\.2\b/.test(gcode), 'layer_height deve ser 0.2')
  })

  /**
   * Teste 4: Config JSON parcial (apenas alguns parametros)
   *
   * Este teste valida que e possivel enviar apenas alguns parametros
   * no config, e os demais serao preenchidos pelos defaults ou perfis.
   */
  it('deve aceitar config JSON parcial junto com perfis', async () => {
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'tdd_config_partial.gcode')

    // Config base com parametros minimos
    const baseConfig = {
      printer_model: 'Bambu Lab A1',
      nozzle_diameter: 0.4,
      printable_area: '0x0,256x0,256x256,0x256',
      printable_height: 256,
      layer_height: 0.2,
      sparse_infill_density: 15,
      filament_type: 'PLA',
      nozzle_temperature: 220,
      bed_temperature: 60
    }

    const body = {
      filePath: stlPath,
      output: outTarget,
      // Config com valores sobrescritos
      config: {
        ...baseConfig,
        wall_loops: 5,
        top_shell_layers: 8
      }
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status}`)

    const gcode = resp.data.gcode as string

    // Valores do config parcial devem estar presentes
    assert.ok(/wall_loops\s*=\s*5\b/.test(gcode), 'wall_loops deve ser 5')
    assert.ok(/top_shell_layers\s*=\s*8\b/.test(gcode), 'top_shell_layers deve ser 8')

    // Valores do perfil base devem estar presentes (nao sobrescritos)
    // layer_height do perfil 0.20mm Standard deve ser 0.2
    assert.ok(/layer_height\s*=\s*0\.2\b/.test(gcode), 'layer_height deve vir do perfil (0.2)')
  })

  /**
   * Teste 5: Config JSON completo baseado no perfil Bambu Lab A1 0.4 nozzle
   *
   * Este teste valida que a API aceita e aplica corretamente um config JSON
   * completo que representa todas as configuracoes do perfil Bambu Lab A1 0.4 nozzle
   * com todas as herancas resolvidas (machine + process + filament).
   */
  it('deve aplicar config JSON completo baseado no perfil Bambu Lab A1 0.4 nozzle', async () => {
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'tdd_bambu_a1_full_config.gcode')

    // Config JSON completo baseado no perfil Bambu Lab A1 0.4 nozzle
    // com todas as herancas resolvidas (fdm_machine_common -> fdm_bbl_3dp_001_common -> Bambu Lab A1 0.4 nozzle)
    const bambuA1Config = {
      // ===== MACHINE CONFIG =====
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

      // ===== PROCESS CONFIG (0.20mm Standard @BBL A1) =====
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

      // ===== FILAMENT CONFIG (Bambu PLA Basic @BBL A1) =====
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
      filePath: stlPath,
      output: outTarget,
      // Apenas config JSON, sem perfis
      config: bambuA1Config
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Esperado status 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data).substring(0, 500)}`
    )

    const gcode = resp.data.gcode as string
    assert.ok(gcode.length > 1000, 'G-code deve ter conteudo significativo')

    // Validar configuracoes de MACHINE
    assert.ok(/nozzle_diameter\s*=\s*0\.4/.test(gcode), 'nozzle_diameter deve ser 0.4')

    // Validar configuracoes de PROCESS
    assert.ok(/layer_height\s*=\s*0\.2\b/.test(gcode), 'layer_height deve ser 0.2')
    assert.ok(/wall_loops\s*=\s*2\b/.test(gcode), 'wall_loops deve ser 2')
    assert.ok(/top_shell_layers\s*=\s*5\b/.test(gcode), 'top_shell_layers deve ser 5')
    assert.ok(/sparse_infill_density\s*=\s*15/.test(gcode), 'sparse_infill_density deve ser 15%')
    assert.ok(/default_acceleration\s*=\s*6000/.test(gcode), 'default_acceleration deve ser 6000')
    assert.ok(/travel_speed\s*=\s*700/.test(gcode), 'travel_speed deve ser 700')

    // Validar configuracoes de FILAMENT
    assert.ok(/filament_type\s*=\s*PLA/.test(gcode), 'filament_type deve ser PLA')
    assert.ok(/nozzle_temperature\s*=\s*220/.test(gcode), 'nozzle_temperature deve ser 220')

    // Verificar arquivo no disco
    assert.ok(fs.existsSync(outTarget), 'Arquivo de saida deve existir no disco')
  })
})
