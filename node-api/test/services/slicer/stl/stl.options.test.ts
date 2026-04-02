// Teste de overrides via campo JSON `options` no serviço slicer/stl (sem multipart)
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

describe('slicer/stl service (options overrides JSON)', () => {
  let server: any
  let baseURL: string

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`
  })

  after(async () => {
    await app.teardown()
  })

  it('aplica overrides passados em `options` e reflete no G-code', async function () {
    this.timeout(180000)

    let stlPath = path.resolve(__dirname, '../../../../../example_files/3DBenchy.stl')
    if (!fs.existsSync(stlPath)) {
      // Gera um STL ASCII mínimo caso o arquivo de exemplo não exista no repo
      stlPath = path.join(__dirname, '../../../../tmp_tiny_ascii.stl')
      const ascii = [
        'solid tri',
        ' facet normal 0 0 1',
        '  outer loop',
        '   vertex 0 0 0',
        '   vertex 1 0 0',
        '   vertex 0 1 0',
        '  endloop',
        ' endfacet',
        'endsolid tri',
        ''
      ].join('\n')
      fs.writeFileSync(stlPath, ascii, 'utf8')
    }

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, `node_api_overrides_3DBenchy.gcode`)

    // Config JSON on-the-fly com parametros minimos para slicing
    // Nota: config tem precedencia sobre options, entao os overrides de options
    // so funcionam para chaves que NAO estao em config
    const config = {
      printer_model: 'Bambu Lab X1 Carbon',
      nozzle_diameter: 0.4,
      printable_area: '0x0,256x0,256x256,0x256',
      printable_height: 256,
      layer_height: 0.2,
      wall_loops: 2,
      sparse_infill_density: 30, // Valor que queremos verificar no G-code
      filament_type: 'PLA',
      nozzle_temperature: 220,
      bed_temperature: 60
    }

    const body = {
      filePath: stlPath,
      output: outTarget,
      config
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, { validateStatus: () => true })

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const data = resp.data

    assert.ok(typeof data === 'object' && data, 'Resposta não é objeto')
    assert.ok(typeof data.outputPath === 'string' && data.outputPath.length > 0, 'outputPath ausente')
    assert.ok(typeof data.gcode === 'string' && data.gcode.length > 50, 'gcode ausente ou muito curto')

    const gcode = data.gcode as string
    // O bloco de configuracao costuma conter linhas tipo: "; sparse_infill_density = 30%".
    // O engine pode normalizar o valor, entao validamos apenas a presenca da chave.
    const hasInfillKey = /sparse_infill_density\s*=/.test(gcode)
    assert.ok(hasInfillKey, 'Chave sparse_infill_density nao apareceu no G-code gerado')

    assert.ok(fs.existsSync(data.outputPath), 'Arquivo de saída não existe no disco')
  })
})
