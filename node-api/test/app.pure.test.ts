// Testes de aplicação (Mocha puro): sobe o app e valida endpoints básicos
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../src/app') as { app: any }

describe('Feathers application tests (mocha puro)', () => {
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

  it('retorna a homepage HTML', async () => {
    const { data } = await axios.get(baseURL)
    assert.ok(typeof data === 'string' && data.includes('<html'), 'Index não retornou HTML')
  })

  it('retorna 404 JSON para rota inexistente', async () => {
    try {
      await axios.get(`${baseURL}/path/to/nowhere`, { responseType: 'json' })
      assert.fail('Deveria ter retornado 404')
    } catch (err: any) {
      const resp = err?.response
      assert.ok(resp, 'Sem resposta do servidor')
      assert.strictEqual(resp.status, 404)
      assert.strictEqual(resp.data?.name, 'NotFound')
    }
  })
})

