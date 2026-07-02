/**
 * PROVA DE AUDITORIA — B5: Classificacao de erro de slicing por
 * substring matching fragil.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B5
 * Arquivo auditado: node-api/src/services/slicer/3mf/3mf.class.ts:229-298
 *
 * A distincao entre erro 400 (usuario) e 500 (interno) depende de
 * matching de substrings em ingles/portugues:
 *
 *   lower.includes('fora da área')
 *   lower.includes('outside')
 *   lower.includes('empty initial layer')
 *   ... (mais de 20 padroes)
 *
 * Qualquer mudanca de wording no libslic3r (submodule que evolui
 * independentemente) reclassifica o erro como 500 generico.
 *
 * ESTE TESTE DOCUMENTA os padroes conhecidos e verifica que todos
 * estao registrados. Quando um novo erro de slicing surgir, este
 * teste deve ser atualizado com o novo padrao.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')

describe('AUDIT B5 — Fragil substring matching para erro de slicing', function () {
  this.timeout(10000)

  let source3mf: string

  before(() => {
    const srcPath = path.resolve(__dirname, '../../../../src/services/slicer/3mf/3mf.class.ts')
    source3mf = fs.readFileSync(srcPath, 'utf8')
  })

  it('PROVA B5-A: documenta todos os padroes de erro de slicing conhecidos', () => {
    // Extrai os padroes do array knownSlicingErrorPatterns no codigo fonte
    const patternsMatch = source3mf.match(/knownSlicingErrorPatterns\s*=\s*\[([\s\S]*?)\]/)
    assert.ok(patternsMatch, 'Nao foi possivel encontrar knownSlicingErrorPatterns no codigo fonte')

    const patternsStr = patternsMatch[1]
    // Extrai strings literais do array (single e double quotes)
    const stringLiterals = patternsStr.match(/'([^']*)'|"([^"]*)"/g) || []
    const patterns = stringLiterals.map(s => s.slice(1, -1))

    console.log('[B5] Padroes de erro de slicing registrados:')
    for (const p of patterns) {
      console.log(`  - "${p}"`)
    }

    // Lista dos padroes mencionados no comentario do codigo (3mf.class.ts:253-258)
    const documentedInComment = [
      'slicing failed: errors',
      'slicing errors',
      'slicingerrors',
      'slicing_error',
      'empty initial layer',
      "can't be printed",
      'no object can be printed',
      'the print is empty',
      'no layers were detected',
      'levitating objects',
    ]

    // Verifica que todos os padroes documentados estao no codigo
    for (const doc of documentedInComment) {
      const found = patterns.includes(doc)
      console.log(`[B5] Documentado "${doc}" → no codigo: ${found}`)
      assert.strictEqual(found, true,
        `Padrao "${doc}" esta documentado no comentario mas NAO esta no array knownSlicingErrorPatterns`)
    }

    console.log(`[B5] Total de padroes: ${patterns.length}`)
  })

  it('PROVA B5-B: lista TODOS os substring checks (fora da area + overrides + slicing errors + export)', () => {
    // Conta todos os blocos de classificacao de erro por substring matching
    const errorBlocks = source3mf.match(/lower\.includes\(|details\.toLowerCase\(\)\.includes\(/g) || []

    console.log(`[B5] Total de lower.includes() para classificacao de erro: ${errorBlocks.length}`)

    // Agrupa por secao
    const sections = [
      { name: 'fora da area', patterns: ['fora da área', 'outside', 'out of bounds', 'does not fit'] },
      { name: 'overrides invalidas', patterns: ['unknown', 'invalid', 'unrecognized', 'failed to set'] },
      { name: 'slicing errors', patterns: [
        'slicing failed: errors', 'slicing errors', 'slicingerrors', 'slicing_error',
        'empty initial layer', "can't be printed", 'no object can be printed',
        'the print is empty', 'no layers were detected', 'levitating objects',
      ]},
      { name: 'gcode export', patterns: ['g-code export failed', 'gcode export failed'] },
      { name: 'file too small', patterns: ['too small', 'file size'] },
    ]

    console.log('')
    console.log('========== B5: TODAS AS CLASSIFICACOES POR SUBSTRING ==========')
    for (const sec of sections) {
      console.log(`  [${sec.name}]:`)
      for (const p of sec.patterns) {
        const found = source3mf.includes(`'${p}'`) || source3mf.includes(`"${p}"`)
        console.log(`    "${p}" → ${found ? 'PRESENTE' : 'AUSENTE'}`)
      }
    }
    console.log('=================================================================')
    console.log('')

    console.log('[B5] Risco: se o libslic3r mudar o texto de qualquer erro,')
    console.log('[B5]   ele sera classificado como 500 generico em vez de 400.')
    console.log('[B5]   Nao ha contrato/API entre libslic3r e o HTTP service')
    console.log('[B5]   para a classificacao de erros — depende de strings humanas.')
  })

  it('PROVA B5-C: o arquivo docs/bugs/2026-06-06-slicing-error-propagation.md referenciado NAO EXISTE', () => {
    const refPath = path.resolve(__dirname, '../../../../../docs/bugs/2026-06-06-slicing-error-propagation.md')
    const exists = fs.existsSync(refPath)
    console.log(`[B5] docs/bugs/2026-06-06-slicing-error-propagation.md existe: ${exists}`)
    console.log(`[B5]   Referenciado em 3mf.class.ts:259`)
    console.log(`[B5]   Referenciado em AddonCore.cpp:904`)

    // Isso tambem e a PROVA B6 (documentacao ausente)
    assert.strictEqual(exists, false,
      'Arquivo docs/bugs/2026-06-06-slicing-error-propagation.md existe! Criado apos a auditoria. ' +
      'Atualize este teste.')
  })

  it('PROVA B5-D: simula como um erro de slicing seria classificado (teste de unidade)', () => {
    // Simula a logica de classificacao do codigo fonte
    const lower = (msg: string) => msg.toLowerCase()

    const knownSlicingErrorPatterns = [
      'slicing failed: errors', 'slicing errors', 'slicingerrors', 'slicing_error',
      'empty initial layer', "can't be printed", 'no object can be printed',
      'the print is empty', 'no layers were detected', 'levitating objects',
    ]

    function classifyAsSlicingError(msg: string): boolean {
      const l = lower(msg)
      return knownSlicingErrorPatterns.some(p => l.includes(p))
    }

    // Erros que DEVEM ser classificados como slicing error
    const shouldMatch = [
      'Slicing failed: Errors',
      'Slicing failed: errors',
      'slicingerrors',
      'One object has empty initial layer and cannot be printed',
      'No object can be printed. Maybe too small',
      'The print is empty. The model is not printable with current print settings.',
      'No layers were detected...',
      'Levitating objects cannot be printed without supports.',
    ]

    console.log('[B5 SIMULATION] Erros que DEVEM ser classificados como slicing error (400):')
    for (const msg of shouldMatch) {
      const matched = classifyAsSlicingError(msg)
      console.log(`  ${matched ? '✓' : '✗'} "${msg.substring(0, 60)}"`)
      assert.strictEqual(matched, true,
        `Erro "${msg}" nao foi classificado como slicing error`)
    }

    // Erros que NAO devem ser classificados como slicing error
    const shouldNotMatch = [
      'G-code export failed: permission denied',
      'Generated file too small: 10 bytes',
      'Internal engine error: segmentation fault',
    ]

    console.log('[B5 SIMULATION] Erros que NAO devem ser slicing error:')
    for (const msg of shouldNotMatch) {
      const matched = classifyAsSlicingError(msg)
      console.log(`  ${matched ? '✗' : '✓'} "${msg.substring(0, 60)}"`)
      // Alguns podem dar match parcial — isso e esperado pela natureza fragil
      if (matched) {
        console.log(`    ATENCAO: match falso-positivo! Substring matching fragil.`)
      }
    }
  })
})
