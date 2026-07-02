/**
 * PROVA DE AUDITORIA — B1: Precedencia INVERTIDA do campo `config`
 * entre os endpoints STL e 3MF.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B1
 * Arquivos auditados:
 *   - STL: node-api/src/services/slicer/stl/stl.class.ts:109-111
 *   - 3MF: node-api/src/services/slicer/3mf/3mf.class.ts:157-162
 *
 * Este teste verifica a ESTRUTURA DO CODIGO FONTE, comprovando que:
 *   1. STL: config SOBRESCREVE options (comportamento INVERTIDO)
 *   2. 3MF: options SOBRESCREVE config (comportamento CORRETO)
 *   3. AGENTS.md documenta a precedencia correta (options > config)
 *   4. A schema do STL documenta a precedencia invertida
 *
 * Abordagem: code-structure test (nao requer engine de slicing).
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')

const repoRoot = path.resolve(__dirname, '../../../..')
const srcDir = path.resolve(__dirname, '../../../src')

describe('AUDIT B1 — Precedencia invertida de `config` entre STL e 3MF (codigo fonte)', function () {
  this.timeout(10000)

  let sourceStl: string
  let source3mf: string
  let schemaStl: string
  let agentsMd: string

  before(() => {
    sourceStl = fs.readFileSync(path.join(srcDir, 'services/slicer/stl/stl.class.ts'), 'utf8')
    source3mf = fs.readFileSync(path.join(srcDir, 'services/slicer/3mf/3mf.class.ts'), 'utf8')
    schemaStl = fs.readFileSync(path.join(srcDir, 'services/slicer/stl/stl.schema.ts'), 'utf8')
    agentsMd = fs.readFileSync(path.join(repoRoot, 'AGENTS.md'), 'utf8')
  })

  it('PROVA B1-A: STL — config SOBRESCREVE options (comportamento INVERTIDO)', () => {
    // stl.class.ts:109-111:
    //   const baseOptions = (data as any).options ?? {}
    //   const configOverrides = (data as any).config ?? {}
    //   const finalOptions = { ...baseOptions, ...configOverrides }
    //
    // configOverrides e aplicado DEPOIS, portanto SOBRESCREVE baseOptions = options.
    // Isso significa: config > options (INVERTIDO em relacao aos docs).

    const hasInvertedPrecedence = sourceStl.includes('...baseOptions, ...configOverrides')
    const hasInvertedComment = schemaStl.includes('tem precedencia sobre options')

    console.log(`[B1-A] STL: finalOptions = { ...baseOptions, ...configOverrides }: ${hasInvertedPrecedence}`)
    console.log(`[B1-A] Schema STL: "config ... tem precedencia sobre options": ${hasInvertedComment}`)
    console.log(`[B1-A] Arquivo: stl.class.ts:109-111`)
    console.log(`[B1-A] Arquivo schema: stl.schema.ts:41`)

    assert.strictEqual(hasInvertedPrecedence, true,
      'STL nao usa spread com configOverrides por ultimo. O fix pode ter sido aplicado.')

    console.log('[B1-A] PROVA: No STL, config (configOverrides) e aplicado DEPOIS de options (baseOptions)')
    console.log('[B1-A]   → config SOBRESCREVE options → PRECEDENCIA INVERTIDA')
  })

  it('PROVA B1-B: 3MF — options SOBRESCREVE config (comportamento CORRETO)', () => {
    // 3mf.class.ts:157-162:
    //   const configOverrides = (data as any).config ?? {}
    //   const profileSettings = { ...configOverrides, curr_bed_type: '...' }
    //   const finalOptions = { ...options }
    //
    // `config` vai para `profile` (aplicado ANTES do 3MF como base)
    // `options` vai para `finalOptions` (aplicado DEPOIS do 3MF como override)
    // Isso significa: options > 3MF > config (CORRETO segundo docs).

    const configGoesToProfile = source3mf.includes('config') && source3mf.includes('profileSettings')
    const optionsStaySeparate = source3mf.includes('finalOptions = { ...options }')
    const precedenceComment = source3mf.includes('Precedência correta') ||
      source3mf.includes('O campo `profile` é aplicado antes')

    console.log(`[B1-B] 3MF: config vai para profileSettings (base):         ${configGoesToProfile}`)
    console.log(`[B1-B] 3MF: options mantido separado como finalOptions:     ${optionsStaySeparate}`)
    console.log(`[B1-B] 3MF: comentario sobre precedencia correta:            ${precedenceComment}`)
    console.log(`[B1-B] Arquivo: 3mf.class.ts:157-162`)

    assert.strictEqual(configGoesToProfile, true,
      '3MF nao coloca config em profileSettings. O codigo foi alterado — verifique.')
    assert.strictEqual(optionsStaySeparate, true,
      '3MF nao mantem options separado. O codigo foi alterado — verifique.')

    console.log('[B1-B] PROVA: No 3MF, config e aplicado como base (ANTES do 3MF)')
    console.log('[B1-B]   e options e aplicado como override (DEPOIS do 3MF)')
    console.log('[B1-B]   → options SOBRESCREVE config → PRECEDENCIA CORRETA')
  })

  it('PROVA B1-C: AGENTS.md documenta a precedencia CORRETA (options > config)', () => {
    // AGENTS.md §2:
    //   "options (explicit overrides) > embedded 3MF > config/profile > defaults"

    const hasCorrectPrecedence = agentsMd.includes('options') &&
      agentsMd.includes('config') &&
      agentsMd.includes('highest to lowest')

    console.log(`[B1-C] AGENTS.md documenta precedencia correta: ${hasCorrectPrecedence}`)
    console.log('[B1-C]   "options (highest override) > 3MF > config/profile (base) > defaults"')

    assert.strictEqual(hasCorrectPrecedence, true,
      'AGENTS.md nao documenta a precedencia — verifique AGENTS.md §2.')

    console.log('[B1-C] A documentacao oficial diz: options > config (STL faz o oposto)')
  })

  it('PROVA B1-D: a schema do STL explicitamente documenta a precedencia INVERTIDA', () => {
    // stl.schema.ts:41:
    //   "config ... tem precedencia sobre options e profiles"
    // Este comentario na schema CONFIRMA que a inversao e intencional (ou pelo menos documentada)
    // e nao um acidente.

    const schemaSaysConfigOverridesOptions = schemaStl.includes('tem precedencia sobre options')
    console.log(`[B1-D] Schema STL diz "config tem precedencia sobre options": ${schemaSaysConfigOverridesOptions}`)
    console.log(`[B1-D] Arquivo: stl.schema.ts:41`)

    assert.strictEqual(schemaSaysConfigOverridesOptions, true,
      'Schema STL nao documenta a precedencia invertida. Comentario removido?')

    console.log('[B1-D] A propria schema do STL documenta a precedencia invertida,')
    console.log('[B1-D]   indicando que o comportamento INVERTIDO e conhecido e documentado,')
    console.log('[B1-D]   mas NAO esta alinhado com AGENTS.md nem com o 3MF.')
  })

  it('PROVA B1-E: o campo `config` existe em AMBOS os endpoints com o mesmo nome', () => {
    // Ambos schemas definem `config` como campo — mas com semantica oposta.

    const stlHasConfig = schemaStl.includes('config: Type.Optional(')
    const _3mfHasConfig = fs.readFileSync(
      path.join(srcDir, 'services/slicer/3mf/3mf.schema.ts'), 'utf8'
    ).includes('config: Type.Optional(')

    console.log(`[B1-E] Schema STL tem campo 'config': ${stlHasConfig}`)
    console.log(`[B1-E] Schema 3MF tem campo 'config': ${_3mfHasConfig}`)

    assert.strictEqual(stlHasConfig, true, 'Schema STL nao tem campo config.')
    assert.strictEqual(_3mfHasConfig, true, 'Schema 3MF nao tem campo config.')

    console.log('[B1-E] O MESMO campo `config` existe em ambos os endpoints,')
    console.log('[B1-E]   mas com precedencia OPOSTA (STL: prioridade maxima; 3MF: prioridade minima).')
  })

  it('RESUMO B1: tabela de precedencia por endpoint', () => {
    console.log('')
    console.log('========== RESUMO B1: PRECEDENCIA DO CAMPO `config` ==========')
    console.log('')
    console.log('  STL  (stl.class.ts:111):')
    console.log('    finalOptions = { ...baseOptions, ...configOverrides }')
    console.log('    → config SOBRESCREVE options  ← INVERTIDO')
    console.log('    Schema stl.schema.ts:41: "tem precedencia sobre options"')
    console.log('')
    console.log('  3MF  (3mf.class.ts:157-162):')
    console.log('    config → profileSettings (base, aplicado ANTES do 3MF)')
    console.log('    options → finalOptions  (override, aplicado DEPOIS do 3MF)')
    console.log('    → options SOBRESCREVE config  ← CORRETO')
    console.log('')
    console.log('  AGENTS.md §2:')
    console.log('    "options (highest override) > 3MF > config/profile > defaults"')
    console.log('')
    console.log('  CONSEQUENCIA: o mesmo JSON `config` produz resultados')
    console.log('  DIFERENTES dependendo do endpoint usado (STL vs 3MF).')
    console.log('')
    console.log('  FIX: alinhar STL com 3MF (options > config, nao o inverso).')
    console.log('')
    console.log('  PROVA CABAL: stl.class.ts:111 vs 3mf.class.ts:157-162')
    console.log('=============================================================')
    console.log('')
  })
})
