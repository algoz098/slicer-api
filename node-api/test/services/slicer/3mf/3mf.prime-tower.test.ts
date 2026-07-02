/**
 * Regression tests: prime tower handling on 3MF slicing.
 *
 * Bug report: 3MFs saved with the prime tower DISABLED failed with
 *   BadRequest "Prime Tower outside printable area" (OBJECTS_OUT_OF_BOUNDS).
 *
 * Root causes fixed in OrcaSlicerAddon/src/core/AddonCore.cpp:
 * 1. Multicolor detection force-enabled `enable_prime_tower`, clobbering the value
 *    explicitly saved in the 3MF (or sent via config/options). The stale
 *    `wipe_tower_x/y` left in the file then failed the printable-area check.
 * 2. checkOutside() validated the tower position whenever `enable_prime_tower` was
 *    set, even when no tower would actually be printed. GUI parity is
 *    Print::has_wipe_tower(), which also requires >1 filament and non-spiral mode.
 * 3. `wipe_tower_x/y` are per-plate vectors and were always read at index 0
 *    instead of the plate being sliced.
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
const os = require('node:os') as typeof import('node:os')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const JSZip = require('jszip') as typeof import('jszip')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { execSync } = require('node:child_process') as typeof import('node:child_process')

const FIXTURES_DIR = path.resolve(__dirname, '../../../fixtures')
const MULTICOLOR_3MF = path.join(FIXTURES_DIR, 'teste_a1mini.3mf') // 4 cores, A1 256x256
const SINGLECOLOR_3MF = path.join(FIXTURES_DIR, 'TARROS_COCINA.3mf') // 1 cor, P1P 256x256

/**
 * Cria uma cópia do 3MF com Metadata/project_settings.config alterado.
 * Usado para simular arquivos exportados pelo GUI com a prime tower desativada
 * e uma posição de torre "stale" fora da área imprimível.
 */
async function make3mfWithSettings(
  source: string,
  patch: Record<string, unknown>
): Promise<string> {
  const zip = await JSZip.loadAsync(fs.readFileSync(source))
  const settingsFile = zip.file('Metadata/project_settings.config')
  assert.ok(settingsFile, 'Fixture não possui Metadata/project_settings.config')
  const settings = JSON.parse(await settingsFile!.async('string'))
  Object.assign(settings, patch)
  zip.file('Metadata/project_settings.config', JSON.stringify(settings, null, 4))
  const outPath = path.join(os.tmpdir(), `prime-tower-test-${Date.now()}-${Math.random().toString(36).slice(2)}.3mf`)
  fs.writeFileSync(outPath, await zip.generateAsync({ type: 'nodebuffer' }))
  return outPath
}

function extractGcode(output3mf: string): string {
  const gcodeFile = output3mf.replace('.gcode.3mf', '-extracted.gcode')
  execSync(`unzip -p "${output3mf}" "Metadata/plate_1.gcode" > "${gcodeFile}"`)
  return fs.readFileSync(gcodeFile, 'utf-8')
}

describe('slicer/3mf: prime tower regression (OBJECTS_OUT_OF_BOUNDS)', function () {
  this.timeout(300_000)

  let server: any
  let baseURL: string
  const tempFiles: string[] = []

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`
  })

  after(async () => {
    await app.teardown()
    for (const f of tempFiles) {
      try { fs.unlinkSync(f) } catch { /* ignore */ }
    }
  })

  it('fatia 3MF multicolor com prime tower desativada e torre stale fora da mesa', async function () {
    // Cenário exato do bug: o GUI salva enable_prime_tower=0 mas mantém
    // wipe_tower_x/y antigos que caem fora da área imprimível. O addon
    // reativava a torre por ser multicolor e rejeitava o arquivo.
    const input3mf = await make3mfWithSettings(MULTICOLOR_3MF, {
      enable_prime_tower: '0',
      wipe_tower_x: ['300'],
      wipe_tower_y: ['300']
    })
    tempFiles.push(input3mf)

    const resp = await axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath: input3mf, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )

    assert.strictEqual(
      resp.status,
      201,
      `Slice deveria funcionar sem prime tower: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
    assert.ok(resp.data.outputPath?.endsWith('.gcode.3mf'), 'Saída deve ser .gcode.3mf')
    assert.ok(fs.existsSync(resp.data.outputPath), 'Arquivo de saída deve existir')

    const gcode = extractGcode(resp.data.outputPath)
    assert.ok(gcode.length > 1000, 'G-code extraído deve ter conteúdo')
    // A torre não pode ter sido impressa
    assert.ok(!/FEATURE: *Prime tower/i.test(gcode), 'G-code não deve conter prime tower')
    // Continua multicolor: deve haver trocas de filamento
    assert.ok(/^(T[1-9]|M620)/m.test(gcode), 'G-code deve manter trocas de filamento (multicolor)')
  })

  it('mantém a prime tower quando o 3MF a habilita com posição válida', async function () {
    // Guarda contra sobre-correção: a torre habilitada no 3MF (posição válida
    // dentro da mesa) deve continuar sendo gerada.
    const resp = await axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath: MULTICOLOR_3MF, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )

    assert.strictEqual(
      resp.status,
      201,
      `Slice com torre habilitada deveria funcionar: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
    const gcode = extractGcode(resp.data.outputPath)
    assert.ok(/FEATURE: *Prime tower/i.test(gcode), 'G-code deve conter a prime tower')
  })

  it('rejeita com OBJECTS_OUT_OF_BOUNDS quando a torre habilitada está fora da mesa', async function () {
    // O check de área continua ativo quando a torre realmente será impressa.
    const input3mf = await make3mfWithSettings(MULTICOLOR_3MF, {
      enable_prime_tower: '1',
      wipe_tower_x: ['300'],
      wipe_tower_y: ['300']
    })
    tempFiles.push(input3mf)

    const resp = await axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath: input3mf, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )

    assert.strictEqual(resp.status, 400, `Esperava 400: ${resp.status} - ${JSON.stringify(resp.data)}`)
    assert.strictEqual(resp.data?.data?.code, 'OBJECTS_OUT_OF_BOUNDS')
  })

  it('não é afetado por metadados de um slice anterior (engine de longa duração)', async function () {
    // Regressão do vazamento de estado: different_settings_to_system de um 3MF
    // fatiado antes ficava no config persistente do engine e corrompia a detecção
    // de overrides do próximo arquivo — o enable_prime_tower=0 era descartado e a
    // torre reativada, reproduzindo o OBJECTS_OUT_OF_BOUNDS em produção.
    const contaminating = path.join(FIXTURES_DIR, 'adhesive-wall-hook-a1-multiobject.3mf')
    const first = await axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath: contaminating, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )
    assert.strictEqual(first.status, 201, `Slice contaminante deveria funcionar: ${first.status}`)

    const input3mf = await make3mfWithSettings(MULTICOLOR_3MF, {
      enable_prime_tower: '0',
      wipe_tower_x: ['300'],
      wipe_tower_y: ['300']
    })
    tempFiles.push(input3mf)

    const resp = await axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath: input3mf, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )
    assert.strictEqual(
      resp.status,
      201,
      `Slice após contaminação deveria funcionar: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
    const gcode = extractGcode(resp.data.outputPath)
    assert.ok(!/FEATURE: *Prime tower/i.test(gcode), 'G-code não deve conter prime tower')
  })

  it('não valida posição da torre em arquivo de cor única (torre não será impressa)', async function () {
    // GUI parity: com 1 filamento a torre não é gerada (Print::has_wipe_tower),
    // então uma posição inválida não pode rejeitar o slice.
    const resp = await axios.post(
      `${baseURL}/slicer/3mf`,
      {
        filePath: SINGLECOLOR_3MF,
        plate: 1,
        options: { enable_prime_tower: '1', wipe_tower_x: '900', wipe_tower_y: '900' }
      },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )

    assert.strictEqual(
      resp.status,
      201,
      `Slice de cor única não deve validar a torre: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
    const gcode = extractGcode(resp.data.outputPath)
    assert.ok(!/FEATURE: *Prime tower/i.test(gcode), 'G-code não deve conter prime tower')
  })
})
