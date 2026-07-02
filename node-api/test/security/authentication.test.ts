/**
 * PROVA DE AUDITORIA — S2: Nenhum endpoint exige autenticacao.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §S2
 * Arquivo auditado: node-api/src/app.ts:61
 *
 * `app.use(parseAuthentication())` apenas FAZ O PARSE do header de
 * autenticacao; nao existe nenhuma estrategia de autenticacao e nenhum
 * hook `authenticate` registrado em servico algum.
 *
 * Todos os endpoints sao completamente abertos, sem autenticacao.
 * Isso amplifica S1 (file write) e S3 (file read bypass).
 *
 * ESTE TESTE E DOCUMENTAL: comprova que nenhum endpoint requer
 * autenticacao, para que auditores possam constatar o fato.
 * Quando um sistema de autenticacao for implementado, este teste
 * devera ser atualizado para verificar que os endpoints protegidos
 * exigem credenciais validas.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../../src/app') as { app: any }
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')

describe('AUDIT S2 — Ausencia de autenticacao em todos os endpoints', function () {
  this.timeout(30000)

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

  const endpoints = [
    // Servicos de slicing
    { method: 'post', path: '/slicer/stl', body: {}, desc: 'POST /slicer/stl' },
    { method: 'post', path: '/slicer/3mf', body: {}, desc: 'POST /slicer/3mf' },

    // GET endpoints
    { method: 'get', path: '/medias', params: { path: '/nonexistent' }, desc: 'GET /medias' },
    { method: 'get', path: '/profiles', desc: 'GET /profiles' },

    // POST endpoints
    { method: 'post', path: '/profile-converter', body: { type: 'process', data: '{}' }, desc: 'POST /profile-converter' },
    { method: 'post', path: '/slicer/model-info', body: {}, desc: 'POST /slicer/model-info' },
  ]

  for (const ep of endpoints) {
    it(`PROVA S2: ${ep.desc} acessivel sem token de autenticacao`, async () => {
      let resp
      try {
        if (ep.method === 'get') {
          resp = await axios({
            method: 'get',
            url: `${baseURL}${ep.path}`,
            params: (ep as any).params || {},
            // Explicitamente SEM headers de autenticacao
            headers: {},
            validateStatus: () => true
          })
        } else {
          resp = await axios({
            method: 'post',
            url: `${baseURL}${ep.path}`,
            data: ep.body || {},
            headers: { 'content-type': 'application/json' },
            validateStatus: () => true
          })
        }
      } catch (err: any) {
        resp = err.response || { status: 0, data: String(err) }
      }

      // Um 401/403 indicaria que autenticacao e exigida (comportamento seguro).
      // Se NAO recebemos 401/403, entao o endpoint esta aberto.
      const isAuthRequired = resp.status === 401 || resp.status === 403

      console.log(`[S2] ${ep.desc} → status=${resp.status} authRequired=${isAuthRequired}`)

      // PROVA: o endpoint NAO exige autenticacao
      assert.strictEqual(isAuthRequired, false,
        `[S2 PROOF] ${ep.desc} retornou ${resp.status}. ` +
        `Isso indica que autenticacao foi exigida — o fix ja foi aplicado. ` +
        `Atualize este teste para verificar o comportamento seguro.`)
    })
  }

  it('PROVA S2: nao existe hook authenticate registrado em nenhum servico', () => {
    // Verifica programaticamente que nao ha authenticate nos hooks
    const services = app.services
    const serviceNames = Object.keys(services)

    for (const name of serviceNames) {
      const svc: any = services[name]
      // Feathers services expoem __hooks ou hooks internamente
      const hooks = svc?.__hooks || svc?.hooks
      if (!hooks) continue

      const beforeAll = hooks?.before?.all || []
      const hasAuthenticate = beforeAll.some((h: any) =>
        typeof h === 'function' && (h.name === 'authenticate' || String(h).includes('authenticate'))
      )

      console.log(`[S2 HOOKS] ${name}: before.all has authenticate = ${hasAuthenticate}`)

      assert.strictEqual(hasAuthenticate, false,
        `Servico "${name}" tem hook authenticate registrado. ` +
        `Isso indica que autenticacao foi implementada. ` +
        `Atualize este teste para verificar o comportamento seguro.`)
    }
  })
})
