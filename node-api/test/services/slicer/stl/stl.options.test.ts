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

    const body = {
      filePath: stlPath,
      output: outTarget,
      // Perfis explícitos
      printerProfile: 'Bambu Lab X1 Carbon 0.4 nozzle',
      filamentProfile: 'Bambu PLA Basic @BBL X1C',
      processProfile: '0.20mm Standard @BBL X1C',
      // Overrides: densidade de infill e altura de camada
      options: {
        sparse_infill_density: 30,
        layer_height: 0.24
      }
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, { validateStatus: () => true })

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const data = resp.data

    assert.ok(typeof data === 'object' && data, 'Resposta não é objeto')
    assert.ok(typeof data.outputPath === 'string' && data.outputPath.length > 0, 'outputPath ausente')
    assert.ok(typeof data.gcode === 'string' && data.gcode.length > 50, 'gcode ausente ou muito curto')

    const gcode = data.gcode as string
    // O bloco de configuração costuma conter linhas tipo: "; sparse_infill_density = 30%" e "; layer_height = 0.24"
    const hasInfill = /sparse_infill_density\s*=\s*30%?\b/.test(gcode)
    const hasLayer = /layer_height\s*=\s*0\.24\b/.test(gcode)
    assert.ok(hasInfill && hasLayer, 'Overrides não apareceram no G-code gerado')

    assert.ok(fs.existsSync(data.outputPath), 'Arquivo de saída não existe no disco')
  })
})

