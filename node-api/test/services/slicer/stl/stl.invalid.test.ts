// Axios HTTP test: invalid override option should return 400 Bad Request
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

describe('slicer/stl service (invalid overrides)', () => {
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

  it('retorna 400 quando options tem chave desconhecida', async function () {
    this.timeout(60000)

    const stlPath = path.resolve(__dirname, '../../../../../example_files/3DBenchy.stl')
    assert.ok(fs.existsSync(stlPath), 'Arquivo de exemplo 3DBenchy.stl não encontrado')

    // Config JSON on-the-fly com parametros minimos para slicing
    const config = {
      printer_model: 'Bambu Lab X1 Carbon',
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
      config,
      options: {
        nonexistent_config_foo: 1
      }
    }

    const resp = await axios.post(`${baseURL}/slicer/stl`, body, { validateStatus: () => true })
    assert.strictEqual(resp.status, 400, `Status inesperado: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const msg = JSON.stringify(resp.data)
    assert.ok(
      /Invalid override option/i.test(msg) || /unknown|invalid|unrecognized/i.test(msg),
      'Mensagem de erro inesperada'
    )
  })
})
