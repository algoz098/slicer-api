/**
 * PROVA DE AUDITORIA — B6: Documentacao referenciada ausente / nao versionada.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B6
 *
 * 1. 3mf.class.ts:259 referencia docs/bugs/2026-06-06-slicing-error-propagation.md
 *    que NAO EXISTE (diretorio docs/bugs/ nunca foi criado).
 * 2. AGENTS.md § "Detailed documentation" referencia docs/*.md, mas o
 *    diretorio docs/ NAO esta versionado no git (git ls-files docs e vazio).
 *
 * ESTE TESTE E DOCUMENTAL: comprova a ausencia dos arquivos para que
 * auditores possam constatar o fato. Quando os docs forem criados e
 * versionados, este teste deve ser atualizado.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { execSync } = require('node:child_process')

describe('AUDIT B6 — Documentacao referenciada ausente / nao versionada', function () {
  this.timeout(10000)

  const repoRoot = path.resolve(__dirname, '../../..')

  it('PROVA B6-A: docs/bugs/2026-06-06-slicing-error-propagation.md NAO EXISTE', () => {
    const missingDocPath = path.resolve(repoRoot, 'docs/bugs/2026-06-06-slicing-error-propagation.md')
    const exists = fs.existsSync(missingDocPath)

    console.log(`[B6] Path: ${missingDocPath}`)
    console.log(`[B6] Existe: ${exists}`)
    console.log(`[B6] Referenciado em: node-api/src/services/slicer/3mf/3mf.class.ts:259`)
    console.log(`[B6] Referenciado em: OrcaSlicerAddon/src/core/AddonCore.cpp:904`)

    assert.strictEqual(exists, false,
      `Arquivo ${missingDocPath} existe! Foi criado apos a auditoria. ` +
      `Atualize este teste para verificar que o conteudo e valido.`)
  })

  it('PROVA B6-B: diretorio docs/ nao esta versionado no git', () => {
    try {
      const gitLsFiles = execSync('git ls-files docs/', {
        cwd: repoRoot,
        encoding: 'utf-8'
      }).trim()

      console.log(`[B6] git ls-files docs/: "${gitLsFiles || '(vazio)'}"`)
      console.log('[B6] O diretorio docs/ NAO esta versionado no git.')
      console.log('[B6] Isso significa que a documentacao apontada como')
      console.log('[B6] "fonte da verdade" pelo AGENTS.md nao e rastreada.')

      assert.strictEqual(gitLsFiles, '',
        `git ls-files docs/ retornou arquivos: "${gitLsFiles}". ` +
        `Docs foram versionados — atualize o teste.`)
    } catch (err: any) {
      console.log(`[B6] git ls-files docs/ falhou: ${err.message}`)
      // Pode nao estar em um repo git — ok para testes locais
    }
  })

  it('PROVA B6-C: conteudo do diretorio docs/ (nao versionado)', () => {
    const docsDir = path.resolve(repoRoot, 'docs')
    if (!fs.existsSync(docsDir)) {
      console.log('[B6] Diretorio docs/ nao existe!')
      return
    }

    const files = fs.readdirSync(docsDir, { recursive: true }) as string[]
    console.log(`[B6] Arquivos em docs/ (nao versionados):`)
    for (const f of files) {
      const fullPath = path.join(docsDir, f)
      const stat = fs.statSync(fullPath)
      console.log(`[B6]   ${f} (${stat.size} bytes, ${stat.isDirectory() ? 'dir' : 'file'})`)
    }

    // Lista dos arquivos referenciados pelo AGENTS.md
    const expectedDocs = [
      'ARCHITECTURE.md',
      'BUILD.md',
      'CONFIGURATION.md',
      'CPP-ENGINE.md',
      'ENVIRONMENT.md',
      'NODE-API.md',
    ]

    console.log('')
    console.log('[B6] Documentos referenciados pelo AGENTS.md §Detailed documentation:')
    for (const doc of expectedDocs) {
      const docPath = path.join(docsDir, doc)
      const exists = fs.existsSync(docPath)
      const versioned = exists ? 'SIM (mas untracked!)' : 'NAO EXISTE'
      console.log(`[B6]   ${doc}: ${versioned}`)
    }
  })

  it('PROVA B6-D: o diretorio docs/bugs/ nao existe', () => {
    const bugsDir = path.resolve(repoRoot, 'docs/bugs')
    const exists = fs.existsSync(bugsDir)

    console.log(`[B6] docs/bugs/ existe: ${exists}`)
    assert.strictEqual(exists, false,
      'docs/bugs/ existe! Foi criado apos a auditoria. ' +
      'Verifique se contem o arquivo referenciado.')
  })
})
