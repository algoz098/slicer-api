// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
// CommonJS style requires to keep mocha in CJS mode (tsx/register cuida do ESM)
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
const FormData = require('form-data') as typeof import('form-data')

describe('slicer/3mf service', () => {
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

  it('registered the service', () => {
    const service = app.service('slicer/3mf')
    assert.ok(service, 'Registered the service')
  })

  it('faz upload de um 3MF via multipart e retorna .gcode.3mf', async function () {
    this.timeout(180000)

    const input3mf = path.resolve(__dirname, '../../../../../example_files/3DBenchy.3mf')
    assert.ok(fs.existsSync(input3mf), 'Arquivo de exemplo 3DBenchy.3mf não encontrado')

    const form = new FormData()
    form.append('file', fs.createReadStream(input3mf), {
      filename: '3DBenchy.3mf',
      contentType: 'model/3mf'
    })

    // Salvar em output_files seguindo o template, com sufixo .gcode.3mf e prefixo node_api
    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const modelBase = path.basename(input3mf, path.extname(input3mf))
    const plate = 1
    const outTarget = path.join(outDir, `node_api_${modelBase}_plate_${plate}.gcode.3mf`)
    form.append('output', outTarget)

    // Perfis explícitos (usar nomes existentes no engine)
    form.append('printerProfile', 'Bambu Lab X1 Carbon 0.4 nozzle')
    form.append('filamentProfile', 'Bambu PLA Basic @BBL X1C')
    form.append('processProfile', '0.20mm Standard @BBL X1C')

    const resp = await axios.post(`${baseURL}/slicer/3mf`, form, {
      headers: form.getHeaders(),
      maxBodyLength: Infinity,
      maxContentLength: Infinity,
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Status inesperado: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const data = resp.data

    assert.ok(typeof data === 'object' && data, 'Resposta não é objeto')
    assert.ok(typeof data.outputPath === 'string' && data.outputPath.length > 0, 'outputPath ausente')
    assert.ok(data.outputPath.endsWith('.gcode.3mf'), 'Saída não termina com .gcode.3mf')

    assert.ok(typeof data.dataBase64 === 'string' && data.dataBase64.length > 1000, 'dataBase64 ausente/curto')
    assert.strictEqual(data.contentType, 'model/3mf')
    assert.ok(typeof data.size === 'number' && data.size > 1000)

    const decoded = Buffer.from(data.dataBase64, 'base64')
    assert.strictEqual(decoded.length, data.size)

    assert.ok(fs.existsSync(data.outputPath), 'Arquivo de saída não existe no disco')
    const stat = fs.statSync(data.outputPath)
    assert.ok(stat.size > 1000, 'Arquivo .gcode.3mf muito pequeno')
    assert.strictEqual(stat.size, data.size, 'Tamanho no disco difere do declarado')
  })
})
