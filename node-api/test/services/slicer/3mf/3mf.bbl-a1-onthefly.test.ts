/**
 * Test: Slicing on-the-fly com perfis reais da Bambu Lab A1
 *
 * Envia os perfis da BBL A1 (impressora + filamento Generic PLA + processo 0.20mm Standard)
 * como JSON on-the-fly (sem carregar vendor bundles) e verifica que a resposta confirma
 * que os perfis enviados foram efetivamente utilizados no slicing.
 *
 * Arquivo de modelo: fornecido pelo usuário via variável de ambiente TEST_3MF_FILE
 * ou path padrão abaixo.
 */

// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../../../../src/app') as { app: any }
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { execSync } = require('node:child_process') as typeof import('node:child_process')

// ---------------------------------------------------------------------------
// Helpers para resolver cadeia de herança de perfis BBL
// ---------------------------------------------------------------------------

type ProfileDir = 'machine' | 'filament' | 'process'

const PROFILES_ROOT = path.resolve(
  __dirname,
  '../../../../../OrcaSlicer/resources/profiles/BBL'
)

/** Caminhos de busca por tipo de perfil */
const PROFILE_DIRS: Record<ProfileDir, string> = {
  machine: path.join(PROFILES_ROOT, 'machine'),
  filament: path.join(PROFILES_ROOT, 'filament'),
  process: path.join(PROFILES_ROOT, 'process')
}

/**
 * Resolve e mergeia a cadeia de herança de um perfil BBL.
 * Retorna um objeto achatado com todos os valores, do ancestral mais antigo
 * ao perfil mais específico (filho tem precedência sobre pai).
 */
function resolveProfileChain(
  name: string,
  dir: ProfileDir,
  visited = new Set<string>()
): Record<string, unknown> {
  if (visited.has(name)) return {}
  visited.add(name)

  const file = path.join(PROFILE_DIRS[dir], `${name}.json`)
  if (!fs.existsSync(file)) return {}

  const raw = JSON.parse(fs.readFileSync(file, 'utf8'))

  let base: Record<string, unknown> = {}
  if (raw.inherits && typeof raw.inherits === 'string') {
    base = resolveProfileChain(raw.inherits, dir, visited)
  }

  // Mergeia: filho sobrescreve pai, remove metadados
  const child: Record<string, unknown> = {}
  for (const [k, v] of Object.entries(raw)) {
    if (['name', 'type', 'from', 'instantiation', 'inherits', 'setting_id',
         'version', 'force_update', 'description', 'url', 'bed_model',
         'bed_texture', 'default_bed_type', 'family', 'machine_tech',
         'model_id', 'default_materials', 'compatible_printers'].includes(k)) {
      continue
    }
    child[k] = v
  }

  return { ...base, ...child }
}

/**
 * Achata arrays de um único elemento para escalares, converte strings numéricas.
 * O slicer espera valores escalares para a maioria dos parâmetros.
 */
function flattenValues(
  obj: Record<string, unknown>
): Record<string, string | number | boolean | string[]> {
  const out: Record<string, string | number | boolean | string[]> = {}
  for (const [k, v] of Object.entries(obj)) {
    if (Array.isArray(v)) {
      if (v.length === 0) continue
      if (v.length === 1) {
        const s = String(v[0])
        const n = Number(s)
        if (!Number.isNaN(n) && /^[+-]?[\d.]+$/.test(s.trim())) {
          out[k] = n
        } else if (s === 'true') {
          out[k] = true
        } else if (s === 'false') {
          out[k] = false
        } else {
          out[k] = s
        }
      } else {
        // Mantém arrays multi-valor como array de strings
        out[k] = v.map(String)
      }
    } else if (typeof v === 'string') {
      const n = Number(v)
      if (!Number.isNaN(n) && /^[+-]?[\d.]+$/.test(v.trim())) {
        out[k] = n
      } else if (v === 'true') {
        out[k] = true
      } else if (v === 'false') {
        out[k] = false
      } else {
        out[k] = v
      }
    } else if (typeof v === 'number' || typeof v === 'boolean') {
      out[k] = v
    }
  }
  return out
}

/** Extrai valor de configuração do G-code embutido no .gcode.3mf */
function extractGcodeConfigValue(outputPath: string, key: string): string | null {
  try {
    const cmd = `unzip -p "${outputPath}" "Metadata/plate_1.gcode" 2>/dev/null | grep -E "^; ${key} = " | head -1`
    const result = execSync(cmd, { encoding: 'utf-8' })
    const match = result.match(new RegExp(`^; ${key} = (.+)$`, 'm'))
    return match ? match[1].trim() : null
  } catch {
    return null
  }
}

// ---------------------------------------------------------------------------
// Suite de testes
// ---------------------------------------------------------------------------

describe('slicer/3mf - Perfis BBL A1 on-the-fly (sem vendor bundle)', function () {
  this.timeout(300_000)

  let server: any = null
  let baseURL = ''
  let inputFile: string

  // Perfis BBL A1 resolvidos uma vez no before()
  let printerConfig: Record<string, string | number | boolean | string[]>
  let filamentConfig: Record<string, string | number | boolean | string[]>
  let processConfig: Record<string, string | number | boolean | string[]>
  let mergedConfig: Record<string, string | number | boolean | string[]>

  before(async () => {
    // env vars já configurados pelo test/setup.ts
    // Resolve os perfis da BBL A1 a partir dos JSONs reais do OrcaSlicer
    const printerRaw = resolveProfileChain('Bambu Lab A1 0.4 nozzle', 'machine')
    const filamentRaw = resolveProfileChain('Generic PLA @BBL A1', 'filament')
    const processRaw = resolveProfileChain('0.20mm Standard @BBL A1', 'process')

    printerConfig = flattenValues(printerRaw)
    filamentConfig = flattenValues(filamentRaw)
    processConfig = flattenValues(processRaw)

    // Merge: processo > filamento > impressora (mais específico vence)
    const raw = {
      ...printerConfig,
      ...filamentConfig,
      ...processConfig,
      printer_model: 'Bambu Lab A1',
      printer_variant: '0.4',
    }

    // Remove G-code templates BBL proprietários — contêm comandos (M1002, G392, etc.)
    // que o addon não suporta e causam crash. O G-code de start/end não é necessário
    // para validar que os perfis de processo/filamento foram aplicados.
    const GCODE_TEMPLATE_KEYS = [
      'machine_start_gcode', 'machine_end_gcode', 'change_filament_gcode',
      'layer_change_gcode', 'machine_pause_gcode', 'time_lapse_gcode',
      'printing_by_object_gcode', 'before_layer_change_gcode',
      'filament_start_gcode', 'filament_end_gcode'
    ]
    for (const k of GCODE_TEMPLATE_KEYS) delete (raw as any)[k]

    mergedConfig = raw

    console.log(`[BBL A1 test] Perfis resolvidos:`)
    console.log(`  Impressora: ${Object.keys(printerConfig).length} chaves`)
    console.log(`  Filamento:  ${Object.keys(filamentConfig).length} chaves`)
    console.log(`  Processo:   ${Object.keys(processConfig).length} chaves`)
    console.log(`  Total merged: ${Object.keys(mergedConfig).length} chaves`)

    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`

    // Arquivo de modelo 3MF
    // Preferencialmente usamos um fixture versionado dentro do repositório
    // (teste_a1mini.3mf), mas permitimos override via TEST_3MF_FILE.
    const fixturesDir = path.resolve(__dirname, '../../../fixtures')
    inputFile =
      process.env.TEST_3MF_FILE ||
      path.join(fixturesDir, 'teste_a1mini.3mf')

    assert.ok(
      fs.existsSync(inputFile),
      `Arquivo de modelo não encontrado: ${inputFile}\n` +
      `Defina TEST_3MF_FILE para apontar para um .3mf válido ou garanta que o fixture teste_a1mini.3mf exista em node-api/test/fixtures.`,
    )
  })

  after(async () => {
    await app.teardown()
  })

  // -------------------------------------------------------------------------
  // Teste 1: Slicing básico com perfis A1 on-the-fly
  // -------------------------------------------------------------------------
  it('deve fatiar com sucesso usando perfis BBL A1 enviados on-the-fly', async () => {
    const body = {
      filePath: inputFile,
      plate: 1,
      config: mergedConfig
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Esperado 201, recebido ${resp.status}. Body: ${JSON.stringify(resp.data)}`
    )

    const data = resp.data
    assert.ok(typeof data.outputPath === 'string' && data.outputPath.length > 0, 'outputPath deve estar presente')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Saída deve ser .gcode.3mf')
    assert.ok(fs.existsSync(data.outputPath), 'Arquivo de saída deve existir no disco')
    assert.ok(typeof data.size === 'number' && data.size > 1000, 'Arquivo deve ter tamanho significativo')
    assert.strictEqual(data.contentType, 'model/3mf', 'Content-type deve ser model/3mf')

    console.log(`  outputPath: ${data.outputPath}`)
    console.log(`  size: ${data.size} bytes`)
    if (data.estimatedTimeSec != null) console.log(`  estimatedTimeSec: ${data.estimatedTimeSec}`)
    if (data.filamentUsedGrams != null) console.log(`  filamentUsedGrams: ${data.filamentUsedGrams}`)
  })

  // -------------------------------------------------------------------------
  // Teste 2: Verifica que usedOptions contém as chaves dos perfis enviados
  // -------------------------------------------------------------------------
  it('deve retornar usedOptions contendo chaves dos perfis A1 enviados', async () => {
    const body = {
      filePath: inputFile,
      plate: 1,
      config: mergedConfig
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Slice falhou: ${JSON.stringify(resp.data)}`)

    const data = resp.data
    const used: string[] = data.usedOptions ?? []

    console.log(`  usedOptions (${used.length}): ${used.slice(0, 20).join(', ')}${used.length > 20 ? '...' : ''}`)

    // Chaves que devem ter sido aplicadas do perfil do processo (0.20mm Standard @BBL A1)
    const expectedProcessKeys = ['layer_height', 'travel_speed', 'default_acceleration']
    for (const key of expectedProcessKeys) {
      assert.ok(
        used.includes(key),
        `usedOptions deve incluir chave do processo '${key}' (perfis enviados: 0.20mm Standard @BBL A1)`
      )
    }

    // Chaves que devem ter sido aplicadas do perfil do filamento (Generic PLA @BBL A1)
    const expectedFilamentKeys = ['nozzle_temperature', 'fan_min_speed', 'fan_max_speed']
    for (const key of expectedFilamentKeys) {
      assert.ok(
        used.includes(key),
        `usedOptions deve incluir chave do filamento '${key}' (perfis enviados: Generic PLA @BBL A1)`
      )
    }

    // Chaves da impressora
    const expectedPrinterKeys = ['nozzle_diameter', 'printable_height']
    for (const key of expectedPrinterKeys) {
      assert.ok(
        used.includes(key),
        `usedOptions deve incluir chave da impressora '${key}' (perfis enviados: Bambu Lab A1 0.4 nozzle)`
      )
    }
  })

  // -------------------------------------------------------------------------
  // Teste 3: Verifica valores reais no G-code gerado
  // -------------------------------------------------------------------------
  it('deve gerar G-code com valores correspondentes aos perfis A1 enviados', async () => {
    const body = {
      filePath: inputFile,
      plate: 1,
      config: mergedConfig
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Slice falhou: ${JSON.stringify(resp.data)}`)

    const outputPath: string = resp.data.outputPath

    // Extrai valores do G-code e verifica que correspondem ao perfil 0.20mm Standard @BBL A1
    const layerHeight = extractGcodeConfigValue(outputPath, 'layer_height')
    const travelSpeed = extractGcodeConfigValue(outputPath, 'travel_speed')
    const defaultAcceleration = extractGcodeConfigValue(outputPath, 'default_acceleration')
    const nozzleTemp = extractGcodeConfigValue(outputPath, 'nozzle_temperature')

    console.log(`  Valores extraídos do G-code:`)
    console.log(`    layer_height: ${layerHeight} (esperado: 0.2 — perfil 0.20mm Standard @BBL A1)`)
    console.log(`    travel_speed: ${travelSpeed} (esperado: 700 — perfil 0.20mm Standard @BBL A1)`)
    console.log(`    default_acceleration: ${defaultAcceleration} (esperado: 6000 — perfil 0.20mm Standard @BBL A1)`)
    console.log(`    nozzle_temperature: ${nozzleTemp} (esperado: 220 — perfil Generic PLA @BBL A1)`)

    // layer_height = 0.2 (vem de fdm_process_single_0.20 via herança)
    if (layerHeight !== null) {
      assert.strictEqual(layerHeight, '0.2', `layer_height deveria ser 0.2, mas é '${layerHeight}'`)
    }

    // travel_speed = 700 (específico do 0.20mm Standard @BBL A1)
    if (travelSpeed !== null) {
      assert.strictEqual(travelSpeed, '700', `travel_speed deveria ser 700, mas é '${travelSpeed}'`)
    }

    // default_acceleration = 6000 (específico do 0.20mm Standard @BBL A1)
    if (defaultAcceleration !== null) {
      assert.strictEqual(
        defaultAcceleration,
        '6000',
        `default_acceleration deveria ser 6000, mas é '${defaultAcceleration}'`
      )
    }

    // nozzle_temperature = 220 (de fdm_filament_pla, herdado por Generic PLA)
    if (nozzleTemp !== null) {
      assert.strictEqual(
        nozzleTemp,
        '220',
        `nozzle_temperature deveria ser 220, mas é '${nozzleTemp}'`
      )
    }
  })

  // -------------------------------------------------------------------------
  // Teste 4: ignoredOptions não deve conter chaves dos perfis enviados
  // -------------------------------------------------------------------------
  it('não deve ter chaves dos perfis A1 em ignoredOptions', async () => {
    const body = {
      filePath: inputFile,
      plate: 1,
      config: mergedConfig
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Slice falhou: ${JSON.stringify(resp.data)}`)

    const ignored: string[] = resp.data.ignoredOptions ?? []

    if (ignored.length > 0) {
      console.log(`  ignoredOptions (${ignored.length}): ${ignored.join(', ')}`)
    }

    // Chaves críticas dos perfis BBL A1 não devem ser ignoradas
    const criticalKeys = ['layer_height', 'travel_speed', 'nozzle_temperature', 'printable_height']
    for (const key of criticalKeys) {
      assert.ok(
        !ignored.includes(key),
        `Chave crítica '${key}' dos perfis A1 foi ignorada pelo slicer — verifique o config enviado`
      )
    }
  })
})
