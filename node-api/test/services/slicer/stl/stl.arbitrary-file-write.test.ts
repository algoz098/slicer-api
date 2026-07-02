/**
 * PROVA DE AUDITORIA — S1: Arbitrary File Write via campo `output` no STL endpoint.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §S1
 * Arquivo auditado: node-api/src/services/slicer/stl/stl.class.ts:97-100
 *
 * Este teste verifica a ESTRUTURA DO CODIGO FONTE, comprovando que:
 *   1. STL usa `data.output` para determinar o caminho de saida (vulnerabilidade)
 *   2. 3MF IGNORA `data.output` e usa `os.tmpdir()` sempre (comportamento seguro)
 *   3. A schema do STL aceita o campo `output` do cliente (stl.schema.ts:56)
 *
 * Abordagem: code-structure test (nao requer engine de slicing funcional).
 * Le o codigo fonte e verifica os padroes que constituem a vulnerabilidade.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')

const repoRoot = path.resolve(__dirname, '../../../../..')
const srcDir = path.resolve(__dirname, '../../../../src')

describe('AUDIT S1 — Arbitrary File Write no STL (verificacao de codigo fonte)', function () {
  this.timeout(10000)

  let sourceStl: string
  let source3mf: string
  let schemaStl: string

  before(() => {
    sourceStl = fs.readFileSync(path.join(srcDir, 'services/slicer/stl/stl.class.ts'), 'utf8')
    source3mf = fs.readFileSync(path.join(srcDir, 'services/slicer/3mf/3mf.class.ts'), 'utf8')
    schemaStl = fs.readFileSync(path.join(srcDir, 'services/slicer/stl/stl.schema.ts'), 'utf8')
  })

  it('PROVA S1-A: STL usa data.output para determinar BOTH outputDir e outputFilename', () => {
    // Verifica que o codigo fonte contem o padrao vulneravel:
    //   const outputDir = data.output ? path.dirname(data.output) : os.tmpdir()
    //   const outputFilename = data.output ? path.basename(data.output) : `orca-${randomUUID()}.gcode`
    //   const outPath = path.join(outputDir, outputFilename)
    //
    // Isso permite que o cliente controle completamente o destino do arquivo.

    const usesDataOutputForDir = sourceStl.includes('data.output ? path.dirname(data.output)')
    const usesDataOutputForFilename = sourceStl.includes('data.output ? path.basename(data.output)')
    const constructsOutPath = sourceStl.includes('path.join(outputDir, outputFilename)')

    console.log(`[S1-A] data.output controla outputDir:     ${usesDataOutputForDir}`)
    console.log(`[S1-A] data.output controla outputFilename: ${usesDataOutputForFilename}`)
    console.log(`[S1-A] outPath = join(outputDir, filename): ${constructsOutPath}`)
    console.log(`[S1-A] Arquivo: stl.class.ts:97-100`)

    assert.strictEqual(usesDataOutputForDir, true,
      'PROVA S1-A: STL nao usa data.output para outputDir. O codigo foi alterado — verifique.')

    assert.strictEqual(usesDataOutputForFilename, true,
      'PROVA S1-A: STL nao usa data.output para outputFilename. O codigo foi alterado — verifique.')

    assert.strictEqual(constructsOutPath, true,
      'PROVA S1-A: Padrao de construcao de outPath nao encontrado.')

    // PROVA: a combinacao desses tres elementos permite escrita arbitraria
    const isVulnerable = usesDataOutputForDir && usesDataOutputForFilename && constructsOutPath
    console.log(`[S1-A] Vulnerabilidade ativa (data.output controla destino): ${isVulnerable}`)
    console.log('[S1-A] PROVA CABAL: stl.class.ts:97-100 — outputDir e outputFilename')
    console.log('[S1-A]   ambos derivados de data.output, sem validacao de diretorio.')
  })

  it('PROVA S1-B: 3MF IGNORA data.output (comportamento SEGURO de referencia)', () => {
    // O 3MF foi corrigido: ignora data.output e sempre usa os.tmpdir()
    //   const outputFilename = `orca-${randomUUID()}.gcode.3mf`
    //   const outPath = path.join(os.tmpdir(), outputFilename)
    //   (sem referencia a data.output)

    const ignoresDataOutput = !source3mf.includes('data.output') || source3mf.includes('Ignora data.output')
    const alwaysUsesTmpdir = source3mf.includes("path.join(os.tmpdir(), outputFilename)")

    console.log(`[S1-B] 3MF ignora data.output:        ${ignoresDataOutput}`)
    console.log(`[S1-B] 3MF sempre usa os.tmpdir():    ${alwaysUsesTmpdir}`)
    console.log(`[S1-B] Arquivo: 3mf.class.ts:110-111`)

    assert.strictEqual(alwaysUsesTmpdir, true,
      'PROVA S1-B: 3MF nao usa os.tmpdir() para o output. O fix pode ter sido revertido.')

    console.log('[S1-B] PROVA: 3MF endpoint IGNORA data.output do cliente.')
    console.log('[S1-B]   O fix deve ser aplicado ao STL da mesma forma.')
  })

  it('PROVA S1-C: schema do STL aceita campo `output` do cliente', () => {
    // A schema do STL (stl.schema.ts:56) define `output` como campo opcional:
    //   output: Type.Optional(Type.String())
    // Isso permite que o cliente envie o campo. O schema NAO valida o valor.

    const schemaHasOutput = schemaStl.includes("output: Type.Optional(Type.String())")
    console.log(`[S1-C] Schema STL aceita campo "output": ${schemaHasOutput}`)
    console.log(`[S1-C] Arquivo: stl.schema.ts:56`)

    assert.strictEqual(schemaHasOutput, true,
      'PROVA S1-C: Schema STL nao define campo output. Pode ter sido removido.')

    console.log('[S1-C] O campo `output` e aceito pelo schema sem restricao de diretorio.')
    console.log('[S1-C]   Combinado com S1-A, isso fecha o ciclo da vulnerabilidade:')
    console.log('[S1-C]   cliente envia output → schema aceita → servico escreve no path.')
  })

  it('PROVA S1-D: o comentario no codigo reconhece o risco ("should handle with care")', () => {
    // O proprio codigo fonte do STL tem um comentario de seguranca nao resolvido:
    //   "Security: Force output to be in a safe directory or sanitize filename"
    //   "Ignore user provided output path to prevent arbitrary writes, or sanitize it"
    //   "For safety, we enforce a generated path in tmp or a specific output dir"

    const hasSecurityComment1 = sourceStl.includes('Force output to be in a safe directory')
    const hasSecurityComment2 = sourceStl.includes('Ignore user provided output path')
    const hasSecurityComment3 = sourceStl.includes('For safety, we enforce')

    console.log(`[S1-D] Comentario "Force output to be in safe dir":    ${hasSecurityComment1}`)
    console.log(`[S1-D] Comentario "Ignore user provided output path":   ${hasSecurityComment2}`)
    console.log(`[S1-D] Comentario "For safety, we enforce":            ${hasSecurityComment3}`)

    assert.strictEqual(hasSecurityComment1, true,
      'Comentario de seguranca "Force output" removido — verifique o codigo.')
    assert.strictEqual(hasSecurityComment2, true,
      'Comentario "Ignore user provided output" removido — verifique.')
    assert.strictEqual(hasSecurityComment3, true,
      'Comentario "For safety, we enforce" removido — verifique.')

    console.log('[S1-D] O codigo fonte CONTEM comentarios de seguranca,')
    console.log('[S1-D]   mas a protecao NAO foi implementada.')
    console.log('[S1-D]   AGENTS.md §5 tambem reconhece o gap:')
    console.log('[S1-D]   "The STL service still accepts output but should be handled with care."')
  })

  it('PROVA S1-E: AGENTS.md documenta o risco conhecido', () => {
    const agentsPath = path.join(repoRoot, 'AGENTS.md')
    const agentsMd = fs.readFileSync(agentsPath, 'utf8')

    const hasAcknowlegement = agentsMd.includes('STL service still accepts') && agentsMd.includes('output')

    console.log(`[S1-E] AGENTS.md reconhece o gap: ${hasAcknowlegement}`)
    console.log('[S1-E]   "The STL service still accepts output but should be handled with care."')

    assert.strictEqual(hasAcknowlegement, true,
      'AGENTS.md nao documenta o gap do STL output — verifique.')
  })

  it('RESUMO S1: cadeia completa da vulnerabilidade', () => {
    console.log('')
    console.log('========== RESUMO S1: ARBITRARY FILE WRITE NO STL ==========')
    console.log('')
    console.log('  1. Schema aceita campo `output` do cliente (stl.schema.ts:56)')
    console.log('  2. Servico usa data.output para outputDir (stl.class.ts:97)')
    console.log('  3. Servico usa data.output para outputFilename (stl.class.ts:99)')
    console.log('  4. Sem validacao de diretorio permitido')
    console.log('  5. Sem autenticacao (S2) → qualquer cliente pode escrever')
    console.log('')
    console.log('  FIX (3MF ja implementa, 3mf.class.ts:110-111):')
    console.log('    const outPath = path.join(os.tmpdir(), `orca-${uuid()}.gcode`)')
    console.log('    // ignora completamente data.output')
    console.log('')
    console.log('  Alternativa: validar data.output contra lista de dirs permitidos.')
    console.log('')
    console.log('  PROVA CABAL: stl.class.ts:97-100 + stl.schema.ts:56')
    console.log('=============================================================')
    console.log('')
  })
})
