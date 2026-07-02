/**
 * Regression tests: colocação Z dos objetos (erro "empty initial layer").
 *
 * Bug: "One object has empty initial layer and can't be printed. Please Cut the
 * bottom or enable supports. (object N)" — lançado pelo libslic3r (GCode.cpp)
 * quando um objeto não toca a mesa.
 *
 * Causas no addon (OrcaSlicerAddon/src/core/plate/PlateCentering.cpp):
 * 1. Objetos flutuando (min Z > 0) nunca eram baixados no caminho BBL — o GUI
 *    equivale ao usuário clicar "place on bed"; headless precisa ser automático.
 * 2. center_instances_on_bed_center aplicava um shift Z GLOBAL (-min_z de todos
 *    os objetos juntos): um objeto afundado de propósito (Z<0, corte da base)
 *    levantava todos os outros, que ficavam flutuando.
 *
 * Paridade GUI: ensure_on_bed por instância com sinking permitido — baixa apenas
 * instâncias flutuando; instâncias afundadas são preservadas (fatiador corta o
 * que está abaixo de Z=0).
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

const FIXTURES_DIR = path.resolve(__dirname, '../../../fixtures')
const BASE_3MF = path.join(FIXTURES_DIR, 'teste_a1mini.3mf')

/**
 * Cria uma cópia do 3MF deslocando em Z o build item do objectid informado.
 * O transform 3MF tem 12 números; o 12º é a translação Z.
 */
async function make3mfWithZShift(source: string, objectId: number, deltaZ: number): Promise<string> {
  const zip = await JSZip.loadAsync(fs.readFileSync(source))
  const modelFile = zip.file('3D/3dmodel.model')
  assert.ok(modelFile, 'Fixture não possui 3D/3dmodel.model')
  let xml = await modelFile!.async('string')
  const itemRe = new RegExp(`(<item objectid="${objectId}"[^>]*transform=")([^"]+)(")`)
  const match = xml.match(itemRe)
  assert.ok(match, `Build item do objectid=${objectId} não encontrado`)
  const nums = match![2].trim().split(/\s+/)
  assert.strictEqual(nums.length, 12, 'Transform deve ter 12 números')
  nums[11] = String(parseFloat(nums[11]) + deltaZ)
  xml = xml.replace(itemRe, `$1${nums.join(' ')}$3`)
  zip.file('3D/3dmodel.model', xml)
  const outPath = path.join(
    os.tmpdir(),
    `z-shift-test-${Date.now()}-${Math.random().toString(36).slice(2)}.3mf`
  )
  fs.writeFileSync(outPath, await zip.generateAsync({ type: 'nodebuffer' }))
  return outPath
}

describe('slicer/3mf: colocação Z dos objetos (empty initial layer)', function () {
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

  async function slice(filePath: string) {
    return axios.post(
      `${baseURL}/slicer/3mf`,
      { filePath, plate: 1 },
      { headers: { 'content-type': 'application/json' }, validateStatus: () => true }
    )
  }

  it('baixa automaticamente para a mesa um objeto flutuando (min Z > 0)', async function () {
    const input3mf = await make3mfWithZShift(BASE_3MF, 2, +30)
    tempFiles.push(input3mf)

    const resp = await slice(input3mf)
    assert.strictEqual(
      resp.status,
      201,
      `Objeto flutuando deve ser baixado para a mesa e fatiar: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
  })

  it('preserva objeto afundado (min Z < 0) sem levantar os demais', async function () {
    // Afundar o objeto 2 em 10mm: o fatiador corta o que está abaixo de Z=0.
    // Regressão do shift Z global: os outros objetos NÃO podem ser levantados.
    const input3mf = await make3mfWithZShift(BASE_3MF, 2, -10)
    tempFiles.push(input3mf)

    const resp = await slice(input3mf)
    assert.strictEqual(
      resp.status,
      201,
      `Objeto afundado deve fatiar com a base cortada: ${resp.status} - ${JSON.stringify(resp.data)}`
    )
  })
})
