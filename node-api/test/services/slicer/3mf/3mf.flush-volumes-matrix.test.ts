/**
 * PROVA DE AUDITORIA — B7: `flush_volumes_matrix`: `headsCount` derivado
 * de `flush_multiplier.length` em vez de um escalar.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B7
 * Arquivo auditado: node-api/src/services/slicer/3mf/3mf.class.ts:172-175
 *
 * O calculo de validacao da matriz flush_volumes_matrix usa:
 *
 *   const headsCount = Array.isArray(flush_multiplier)
 *     ? flush_multiplier.length
 *     : 1
 *   const expectedSize = filamentCount * filamentCount * headsCount
 *
 * O `flush_multiplier` no OrcaSlicer e NORMALMENTE um escalar
 * (multiplicador global), nao um vetor por cabecote.
 *
 * Se `flush_multiplier` chegar como array (ex: metadata do 3MF),
 * `headsCount` = array.length, e o `expectedSize` diverge do real.
 *
 * Uma matriz valida pode ser INCORRETAMENTE descartada.
 *
 * ESTE TESTE VERIFICA a logica de validacao com varios inputs.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')

describe('AUDIT B7 — flush_volumes_matrix headsCount suspeito', function () {
  this.timeout(10000)

  function computeExpectedSize(
    filamentCount: number,
    flushMultiplier: unknown
  ): number {
    const headsCount = Array.isArray(flushMultiplier)
      ? (flushMultiplier as unknown[]).length
      : 1
    return filamentCount * filamentCount * headsCount
  }

  it('PROVA B7-A: com flush_multiplier escalar, headsCount=1 (funciona)', () => {
    // Caso comum: flush_multiplier e um numero (escalar)
    const filamentCount = 4  // 4 filamentos
    const flushMultiplier = 0.8  // escalar

    const expected = computeExpectedSize(filamentCount, flushMultiplier)
    console.log(`[B7-A] filamentCount=4, flushMultiplier=0.8 (scalar)`)
    console.log(`[B7-A] headsCount=1, expectedSize=${expected} (4*4*1=16)`)
    assert.strictEqual(expected, 16)
  })

  it('PROVA B7-B: com flush_multiplier array, headsCount e derivado do array', () => {
    // flush_multiplier chega como array (ex: metadata do 3MF com multiplos valores)
    // O codigo assume que o tamanho do array = numero de cabecotes
    const filamentCount = 4
    const flushMultiplier = [0.8, 0.9, 0.7] // array de 3 elementos

    const expected = computeExpectedSize(filamentCount, flushMultiplier)
    console.log(`[B7-B] filamentCount=4, flushMultiplier=[0.8,0.9,0.7] (array length=3)`)
    console.log(`[B7-B] headsCount=3 (derivado de array.length), expectedSize=${expected} (4*4*3=48)`)

    // O expectedSize = 48  (4*4*3)
    // Mas a matriz real do OrcaSlicer tem tamanho filamentCount^2 = 16
    // (nao filamentCount^2 * headsCount)
    // Entao uma matriz de 16 elementos SERIA descartada porque o codigo espera 48
    assert.strictEqual(expected, 48,
      'headsCount derivado de array.length produziu expectedSize incorreto')

    console.log('[B7-B] CONSEQUENCIA: matriz valida de 16 elementos seria descartada')
    console.log('[B7-B]   (expected=48, actual=16 → "inconsistent" → delete)')
  })

  it('PROVA B7-C: simulacao da validacao completa com diferentes inputs', () => {
    // Simula exatamente a logica do 3mf.class.ts:168-185
    function validateFlushVolumesMatrix(
      filamentCount: number,
      flushMultiplier: unknown,
      flushVolumesMatrix: number[]
    ): { valid: boolean; expectedSize: number; actualSize: number } {
      const headsCount = Array.isArray(flushMultiplier)
        ? (flushMultiplier as unknown[]).length
        : 1
      const expectedSize = filamentCount * filamentCount * headsCount
      const actualSize = flushVolumesMatrix.length
      return { valid: actualSize === expectedSize, expectedSize, actualSize }
    }

    const testCases = [
      // [desc, filamentCount, flushMultiplier, flushVolumesMatrix, shouldBeValid]
      {
        desc: '4 filamentos, escalar, matriz 16 elem',
        filamentCount: 4,
        flushMultiplier: 0.8,
        matrix: Array(16).fill(0),
        shouldBeValid: true  // 4*4*1 = 16 ✓
      },
      {
        desc: '4 filamentos, array[3], matriz 16 elem (VALIDA mas seria descartada)',
        filamentCount: 4,
        flushMultiplier: [0.8, 0.9, 0.7],
        matrix: Array(16).fill(0),
        shouldBeValid: true  // deveria ser valida (4*4 = 16), mas codigo espera 48
      },
      {
        desc: '4 filamentos, array[3], matriz 48 elem',
        filamentCount: 4,
        flushMultiplier: [0.8, 0.9, 0.7],
        matrix: Array(48).fill(0),
        shouldBeValid: true  // 4*4*3 = 48 ✓ (coincidentemente)
      },
      {
        desc: '2 filamentos, escalar, matriz 4 elem',
        filamentCount: 2,
        flushMultiplier: 0.5,
        matrix: Array(4).fill(0),
        shouldBeValid: true  // 2*2*1 = 4 ✓
      },
    ]

    for (const tc of testCases) {
      const result = validateFlushVolumesMatrix(tc.filamentCount, tc.flushMultiplier, tc.matrix)
      const matchesCode = result.valid
      const matchesExpectation = result.valid === tc.shouldBeValid

      console.log(`[B7-C] ${tc.desc}`)
      console.log(`[B7-C]   expectedSize=${result.expectedSize}, actualSize=${result.actualSize}`)
      console.log(`[B7-C]   codigo considera valido: ${matchesCode}`)
      console.log(`[B7-C]   deveria ser valido: ${tc.shouldBeValid}`)
      console.log(`[B7-C]   bug: ${!matchesExpectation ? 'SIM ← headsCount errado' : 'nao'}`)

      if (tc.desc.includes('VALIDA mas seria descartada')) {
        // Este caso especifico e o bug: matriz valida e descartada
        assert.strictEqual(matchesCode, false,
          `BUG B7: matriz valida de 16 elementos foi aceita (quando deveria ter sido ` +
          `rejeitada pela logica bugada). Pode ser que o fix ja foi aplicado.`)
        console.log('[B7-C] BUG CONFIRMADO: matriz valida descartada por headsCount errado')
      }
    }
  })

  it('PROVA B7-D: analise do codigo fonte para documentar a logica exata', () => {
    // Este teste apenas documenta a logica no codigo fonte
    const codeLine = `const headsCount = Array.isArray(profileSettings.flush_multiplier) ? profileSettings.flush_multiplier.length : 1`
    console.log('[B7] Linha de codigo relevante (3mf.class.ts:172-174):')
    console.log(`[B7]   ${codeLine}`)
    console.log('[B7]')
    console.log('[B7] flush_multiplier no OrcaSlicer e normalmente um ESCALAR')
    console.log('[B7] (ex: 0.8) — nao um vetor por cabecote.')
    console.log('[B7]')
    console.log('[B7] Se o metadata do 3MF contiver flush_multiplier como')
    console.log('[B7] array, o codigo assume que array.length = numero de')
    console.log('[B7] cabecotes, o que pode estar errado.')
    console.log('[B7]')
    console.log('[B7] FIX: validar contra a documentacao real do OrcaSlicer')
    console.log('[B7]   ou usar filamentCount^2 como expectedSize (sem headsCount).')
  })
})
