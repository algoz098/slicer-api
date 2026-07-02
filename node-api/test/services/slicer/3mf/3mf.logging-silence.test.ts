/**
 * PROVA DE AUDITORIA — B3: 3MF nunca silencia logging (empty finally block).
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B3
 * Arquivo auditado: node-api/src/services/slicer/3mf/3mf.class.ts:197-210
 *
 * O bloco try/finally em 3MF esta VAZIO:
 *
 *   try {
 *     try {
 *       res = await orca.slice(...)
 *     } finally {
 *     }                          ← vazio! nao chama setLoggingSilenced
 *   } catch (err) { ... }
 *
 * Em contraste, STL (stl.class.ts:122-135) e model-info chamam
 * setLoggingSilenced(true)/false corretamente.
 *
 * Consequencia: slices 3MF despejam logs do libslic3r em stdout/stderr
 * (poluicao de logs e possivel vazamento de paths temporarios).
 *
 * ESTE TESTE E DOCUMENTAL: verifica a estrutura do codigo fonte para
 * comprovar que o bloco finally esta vazio.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')

describe('AUDIT B3 — 3MF nao silencia logging (empty finally)', function () {
  this.timeout(10000)

  let source3mf: string

  before(() => {
    const srcPath = path.resolve(__dirname, '../../../../src/services/slicer/3mf/3mf.class.ts')
    source3mf = fs.readFileSync(srcPath, 'utf8')
  })

  it('PROVA B3-A: 3mf.class.ts tem bloco finally VAZIO (sem setLoggingSilenced)', () => {
    // Procura o padrao do bug: try { try { ... } finally { } } catch
    // O inner finally block deve estar vazio ou nao conter setLoggingSilenced
    const innerFinallyMatch = source3mf.match(/try\s*\{\s*try\s*\{[^}]*res\s*=\s*await\s*orca\.slice[^}]*\}\s*finally\s*\{([^}]*)\}/s)

    if (innerFinallyMatch) {
      const finallyBody = innerFinallyMatch[1].trim()
      console.log(`[B3] Inner finally body: "${finallyBody}"`)
      const isSilenced = finallyBody.includes('setLoggingSilenced')
      console.log(`[B3] Logging silenced in finally: ${isSilenced}`)

      // PROVA: o finally esta vazio ou nao tem setLoggingSilenced
      assert.strictEqual(isSilenced, false,
        `Inner finally contem setLoggingSilenced — o fix ja foi aplicado. ` +
        `finallyBody="${finallyBody}"`)
    } else {
      // Tenta outro padrao (o codigo fonte real)
      const altMatch = source3mf.match(/\} finally \{/g)
      if (altMatch) {
        console.log(`[B3] Encontrados ${altMatch.length} blocos finally`)
        // Extrai o contexto ao redor de cada finally
        const lines = source3mf.split('\n')
        for (let i = 0; i < lines.length; i++) {
          if (lines[i].includes('finally {')) {
            console.log(`[B3] line ${i + 1}: ${lines[i].trim()}`)
            console.log(`[B3] line ${i + 2}: ${(lines[i + 1] || '').trim()}`)
          }
        }
      }
    }
  })

  it('PROVA B3-B: STL chama setLoggingSilenced (comportamento CORRETO de referencia)', () => {
    const stlPath = path.resolve(__dirname, '../../../../src/services/slicer/stl/stl.class.ts')
    const sourceStl = fs.readFileSync(stlPath, 'utf8')

    const silencedCalls = (sourceStl.match(/setLoggingSilenced/g) || [])
    console.log(`[B3 STL] setLoggingSilenced calls: ${silencedCalls.length}`)

    // STL deve ter pelo menos 2 chamadas (true + false)
    assert.ok(silencedCalls.length >= 2,
      `STL deve ter pelo menos 2 chamadas setLoggingSilenced, encontradas ${silencedCalls.length}`)
  })

  it('PROVA B3-C: model-info chama setLoggingSilenced (comportamento CORRETO de referencia)', () => {
    const miPath = path.resolve(__dirname, '../../../../src/services/slicer/model-info/model-info.class.ts')
    if (!fs.existsSync(miPath)) {
      console.log('[B3] model-info.class.ts nao encontrado, pulando')
      return
    }
    const sourceMi = fs.readFileSync(miPath, 'utf8')
    const silencedCalls = (sourceMi.match(/setLoggingSilenced/g) || [])
    console.log(`[B3 MODEL-INFO] setLoggingSilenced calls: ${silencedCalls.length}`)

    assert.ok(silencedCalls.length >= 2,
      `model-info deve ter pelo menos 2 chamadas setLoggingSilenced, encontradas ${silencedCalls.length}`)
  })

  it('PROVA B3-D: 3MF tem ZERO chamadas a setLoggingSilenced', () => {
    const silencedCalls = (source3mf.match(/setLoggingSilenced/g) || [])
    console.log(`[B3 3MF] setLoggingSilenced calls: ${silencedCalls.length}`)

    // PROVA: 3mf.class.ts nunca chama setLoggingSilenced
    assert.strictEqual(silencedCalls.length, 0,
      `3MF tem ${silencedCalls.length} chamadas setLoggingSilenced. ` +
      `O fix do B3 pode ter sido aplicado — atualize o teste.`)
  })

  it('RESUMO B3: inconsistencia de logging entre endpoints', () => {
    const stlPath = path.resolve(__dirname, '../../../../src/services/slicer/stl/stl.class.ts')
    const sourceStl = fs.readFileSync(stlPath, 'utf8')
    const stlCalls = (sourceStl.match(/setLoggingSilenced/g) || []).length
    const _3mfCalls = (source3mf.match(/setLoggingSilenced/g) || []).length

    console.log('')
    console.log('========== RESUMO B3: LOGGING CONTROL ==========')
    console.log(`  STL:       ${stlCalls} chamadas setLoggingSilenced ← SILENCIADO`)
    console.log(`  3MF:       ${_3mfCalls} chamadas setLoggingSilenced ← NAO SILENCIADO`)
    console.log('')
    console.log('  O bloco inner finally em 3mf.class.ts:209-210 esta VAZIO.')
    console.log('  Isso e um leftover — STL e model-info chamam')
    console.log('  setLoggingSilenced(true/false) corretamente.')
    console.log('')
    console.log('  Consequencia: slices 3MF despejam logs do libslic3r')
    console.log('  em stdout/stderr (poluicao + vazamento de paths).')
    console.log('==================================================')
    console.log('')
  })
})
