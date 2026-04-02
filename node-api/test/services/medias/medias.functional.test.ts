const assert = require('assert')
const { app } = require('../../../src/app')
const axios = require('axios')
const path = require('path')
const fs = require('fs')
const os = require('os')

describe('medias service functional tests', () => {
  let server
  let baseURL
  let tempFile
  let outputDir
  let outputFile

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`

    // Create a temp file in os.tmpdir()
    tempFile = path.join(os.tmpdir(), 'medias-test-temp.txt')
    fs.writeFileSync(tempFile, 'temp content')

    // Create a file in output_files. The medias service allowlist usa `path.resolve('output_files')`,
    // entao criamos o arquivo exatamente nesse diretório para que o acesso seja permitido.
    outputDir = path.resolve('output_files')
    if (!fs.existsSync(outputDir)) {
      fs.mkdirSync(outputDir, { recursive: true })
    }
    outputFile = path.join(outputDir, 'medias-test-output.txt')
    fs.writeFileSync(outputFile, 'output content')
  })

  after(async () => {
    if (fs.existsSync(tempFile)) fs.unlinkSync(tempFile)
    if (fs.existsSync(outputFile)) fs.unlinkSync(outputFile)
    await app.teardown()
  })

  it('allows access to files in os.tmpdir()', async () => {
    const resp = await axios.get(`${baseURL}/medias`, {
      params: { path: tempFile },
      validateStatus: () => true
    })
    assert.strictEqual(resp.status, 200)

    // Service retorna um array com `{ path, data }`, onde data é base64 do conteúdo
    assert.ok(Array.isArray(resp.data), 'Resposta deve ser um array')
    assert.ok(resp.data.length === 1, 'Deve retornar exatamente um item')
    const item = resp.data[0]
    assert.strictEqual(item.path, tempFile)
    const decoded = Buffer.from(item.data, 'base64').toString('utf8')
    assert.strictEqual(decoded, 'temp content')
  })

  it('allows access to files in output_files', async () => {
    const resp = await axios.get(`${baseURL}/medias`, {
      params: { path: outputFile },
      validateStatus: () => true
    })
    assert.strictEqual(resp.status, 200)

    assert.ok(Array.isArray(resp.data), 'Resposta deve ser um array')
    assert.ok(resp.data.length === 1, 'Deve retornar exatamente um item')
    const item = resp.data[0]
    assert.strictEqual(item.path, outputFile)
    const decoded = Buffer.from(item.data, 'base64').toString('utf8')
    assert.strictEqual(decoded, 'output content')
  })

  it('denies access to files outside allowed directories (e.g. /etc/hosts)', async () => {
    // Try /etc/hosts (or equivalent on the system)
    const target = '/etc/hosts'
    if (fs.existsSync(target)) {
      const resp = await axios.get(`${baseURL}/medias`, {
        params: { path: target },
        validateStatus: () => true
      })
      assert.notEqual(resp.status, 200)
      assert.ok(resp.status === 400 || resp.status === 403)
    }
  })

  it('denies access to parent directory traversal', async () => {
    // Try navigating out of tmpdir
    const target = path.join(os.tmpdir(), '..', '..', 'etc', 'passwd') // Fake path but traversal attempt
    // If resolved path is outside allowed dirs, it should fail
    const resp = await axios.get(`${baseURL}/medias`, {
      params: { path: target },
      validateStatus: () => true
    })
    assert.notEqual(resp.status, 200)
    assert.ok(resp.status === 400 || resp.status === 403 || resp.status === 404)
  })

  it('returns 400 if path is missing', async () => {
    const resp = await axios.get(`${baseURL}/medias`, {
      validateStatus: () => true
    })
    assert.strictEqual(resp.status, 400)
  })

  it('returns 404 if file does not exist', async () => {
    const safeButMissing = path.join(os.tmpdir(), 'does-not-exist.txt')
    const resp = await axios.get(`${baseURL}/medias`, {
      params: { path: safeButMissing },
      validateStatus: () => true
    })
    assert.strictEqual(resp.status, 404)
  })
})
