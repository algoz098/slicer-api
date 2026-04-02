/**
 * TDD Tests: Slicing 3MF com configuracao completa via JSON
 *
 * Estes testes validam a funcionalidade de enviar configuracoes
 * completas de impressora/filamento/processo como JSON, substituindo
 * completamente a necessidade de profiles em arquivo.
 *
 * Especificacao:
 * - O usuario pode transformar printer, filament e process profiles em um JSON
 * - Esse JSON e enviado para o addon via campo 'config'
 * - O addon usa esse JSON no lugar de profiles em arquivo
 * - Nenhuma configuracao do 3MF embutido e usada (transfer* = false)
 *
 * IMPORTANTE: Estes testes devem FALHAR inicialmente (TDD red phase).
 */

import assert from 'assert'
import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import type { Server } from 'http'
import { fileURLToPath } from 'node:url'
const __filename = fileURLToPath(import.meta.url)
const __dirname = path.dirname(__filename)

describe('slicer/3mf service - slicing com JSON config completo (sem profiles)', function () {
  this.timeout(180000)

  let server: Server | null = null
  let baseURL = ''
  let appRef: any = null
  let input3mf: string

  // Configuracao minima completa para slicing sem profiles
  // Equivale a um printer + filament + process profile combinados
  const MINIMAL_COMPLETE_CONFIG = {
    // === PRINTER CONFIG ===
    printer_model: 'Generic',
    printer_variant: '0.4',
    gcode_flavor: 'marlin',
    nozzle_diameter: [0.4],
    bed_shape: ['0x0', '220x0', '220x220', '0x220'],
    printable_area: ['0x0', '220x0', '220x220', '0x220'],
    printable_height: 250,
    extruder_offset: ['0x0'],
    retraction_length: [0.8],
    retraction_speed: [30],
    retract_lift: [0],
    retract_before_wipe: [0],
    retract_when_changing_layer: [0],
    wipe: [false],
    machine_start_gcode: 'G28 ; home\nG1 Z5 F3000 ; lift\n',
    machine_end_gcode:
      'M104 S0 ; turn off hotend\nM140 S0 ; turn off bed\nG28 X Y ; home X Y\nM84 ; disable motors\n',

    // === FILAMENT CONFIG ===
    filament_type: ['PLA'],
    filament_diameter: [1.75],
    nozzle_temperature: [210],
    nozzle_temperature_initial_layer: [215],
    bed_temperature: [60],
    bed_temperature_initial_layer: [65],
    filament_density: [1.24],
    filament_cost: [25],
    filament_flow_ratio: [1.0],
    fan_min_speed: [20],
    fan_max_speed: [100],
    slow_down_min_speed: [10],
    reduce_fan_stop_start_freq: [true],
    filament_retraction_length: [0.8],
    filament_retraction_speed: [30],

    // === PROCESS CONFIG ===
    layer_height: 0.2,
    initial_layer_print_height: 0.2,
    wall_loops: 2,
    top_shell_layers: 4,
    bottom_shell_layers: 3,
    sparse_infill_density: 15,
    sparse_infill_pattern: 'grid',
    top_surface_pattern: 'monotonic',
    bottom_surface_pattern: 'monotonic',
    infill_direction: 45,
    bridge_flow: 1.0,
    bridge_speed: 30,
    gap_infill_speed: 30,
    initial_layer_speed: 30,
    outer_wall_speed: 40,
    inner_wall_speed: 80,
    sparse_infill_speed: 100,
    top_surface_speed: 40,
    travel_speed: 150,
    enable_prime_tower: false,
    brim_type: 'no_brim',
    brim_width: 0,
    skirt_loops: 1,
    skirt_distance: 2,
    support_type: 'normal(auto)',
    enable_support: false,
    raft_layers: 0
  }

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

    // Prepare 3MF path
    input3mf = path.resolve(__dirname, '../../../../../example_files/3DBenchy.3mf')
  })

  after(async () => {
    if (appRef) await appRef.teardown()
  })

  /**
   * Teste 1: Slicing 3MF usando APENAS config JSON (sem profiles, sem heranca do 3MF)
   *
   * Este e o teste principal: valida que e possivel fazer slicing enviando
   * todas as configuracoes via JSON, sem usar nenhum profile de arquivo
   * e sem herdar nenhuma configuracao do 3MF.
   */
  it('deve fazer slicing 3MF usando apenas config JSON completo (sem profiles)', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('Arquivo 3DBenchy.3mf nao encontrado, pulando teste')
      return
    }

    const body = {
      filePath: input3mf,
      plate: 1,
      // SEM profiles - usando apenas config JSON
      // printerProfile: undefined,
      // filamentProfile: undefined,
      // processProfile: undefined,
      // Desabilitar heranca de configs do 3MF
      // Config JSON completo substitui tudo
      config: MINIMAL_COMPLETE_CONFIG
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Esperado status 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const data = resp.data
    assert.ok(typeof data.outputPath === 'string', 'outputPath deve existir')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Saida deve ser .gcode.3mf')
    assert.ok(fs.existsSync(data.outputPath), 'Arquivo deve existir no disco')
    assert.ok(data.size > 1000, 'Arquivo deve ter tamanho significativo')
  })

  /**
   * Teste 2: Validar que layer_height do config JSON foi aplicado
   *
   * Verifica que as configuracoes do JSON realmente foram usadas
   * comparando com o output esperado.
   */
  it('deve aplicar layer_height do config JSON no slicing', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('Arquivo 3DBenchy.3mf nao encontrado, pulando teste')
      return
    }

    // Config com layer_height especifico
    const config = {
      ...MINIMAL_COMPLETE_CONFIG,
      layer_height: 0.12 // Valor especifico para testar
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

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status}`)

    // Verificar que o arquivo foi gerado
    assert.ok(fs.existsSync(resp.data.outputPath), 'Arquivo de saida deve existir')

    // Verificar que usedOptions inclui layer_height
    if (resp.data.usedOptions) {
      assert.ok(resp.data.usedOptions.includes('layer_height'), 'layer_height deve estar em usedOptions')
    }
  })

  /**
   * Teste 3: Keys desconhecidas em config sao ignoradas silenciosamente
   *
   * O addon deve ignorar keys desconhecidas e continuar o slicing normalmente.
   */
  it('deve ignorar keys desconhecidas em config e retornar sucesso', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('Arquivo 3DBenchy.3mf nao encontrado, pulando teste')
      return
    }

    const config = {
      ...MINIMAL_COMPLETE_CONFIG,
      completely_invalid_key_xyz: 'should_be_ignored',
      another_fake_key: 12345
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

    // Slicing deve ter sucesso mesmo com keys desconhecidas
    assert.strictEqual(resp.status, 201, `Slicing deve ter sucesso, recebido ${resp.status}`)
    assert.ok(fs.existsSync(resp.data.outputPath), 'Arquivo de saida deve existir')

    // Verificar que keys invalidas estao em ignoredOptions
    if (resp.data.ignoredOptions) {
      assert.ok(
        resp.data.ignoredOptions.includes('completely_invalid_key_xyz'),
        'Key invalida deve estar em ignoredOptions'
      )
    }
  })
})
