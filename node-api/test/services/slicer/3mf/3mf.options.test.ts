/* Axios JSON test for 3MF service with options and plate */
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

describe('slicer/3mf service (JSON body with options)', () => {
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

  it('POST JSON with filePath, plate and options returns .gcode.3mf', async function () {
    this.timeout(180000)

    const input3mf = path.resolve(__dirname, '../../../../../example_files/3DBenchy.3mf')
    assert.ok(fs.existsSync(input3mf), 'Arquivo de exemplo 3DBenchy.3mf não encontrado')

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, `node_api_3mf_options_plate_1.gcode.3mf`)

    const body = {
      filePath: input3mf,
      output: outTarget,
      plate: 1,
      printerProfile: 'Bambu Lab X1 Carbon 0.4 nozzle',
      filamentProfile: 'Bambu PLA Basic @BBL X1C',
      processProfile: '0.20mm Standard @BBL X1C',
      options: { sparse_infill_density: 30, layer_height: 0.24 }
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      maxBodyLength: Infinity,
      maxContentLength: Infinity,
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const data = resp.data

    assert.ok(typeof data === 'object' && data, 'Resposta não é objeto')
    assert.ok(typeof data.outputPath === 'string' && data.outputPath.length > 0, 'outputPath ausente')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Saída não termina com .gcode.3mf')
    assert.strictEqual(data.contentType, 'model/3mf')
    assert.ok(typeof data.size === 'number' && data.size > 1000)

    // Verificar que o arquivo existe no disco
    assert.ok(fs.existsSync(data.outputPath), 'Arquivo de saída não existe no disco')
    const stat = fs.statSync(data.outputPath)
    assert.ok(stat.size > 1000, 'Arquivo .gcode.3mf muito pequeno')
    assert.strictEqual(stat.size, data.size, 'Tamanho no disco difere do declarado')
  })
})

