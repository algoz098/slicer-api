/**
 * PROVA DE AUDITORIA — B2: `coerceValue` converte strings com zeros
 * a esquerda em numeros, apesar do comentario afirmar o contrario.
 *
 * Audit report: docs/AUDIT-2026-07-02.md §B2
 * Arquivo auditado: node-api/src/services/profile-converter/profile-converter.class.ts:48-52
 *
 * O comentario na linha 48 diz:
 *   "... avoid turning IDs with leading zeros into numbers"
 *
 * Mas a regex /^[+-]?(?:\d+\.\d+|\d+)$/i casa com "007", e
 * Number("007") === 7, perdendo o zero a esquerda.
 *
 * Apos a perda, nao ha como distinguir "007" de "7".
 * Para IDs/codigos com zeros a esquerda (ex: codigos de filamento),
 * o valor original e irreversivelmente perdido.
 *
 * ESTE TESTE DEVE FALHAR (red): comprova que "007" vira 7.
 */

// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../../../src/app') as { app: any }
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')

describe('AUDIT B2 — coerceValue perde zeros a esquerda', function () {
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

  it('PROVA B2-A: "007" e convertido para 7 (numero), perdendo os zeros', async () => {
    // Envia um profile JSON com valor string "007"
    // O profile-converter deve processa-lo via coerceValue
    const body = {
      type: 'process' as const,
      data: {
        version: '1.0',
        // my_code_id simula um ID/codigo com zeros a esquerda
        my_code_id: '007',
        // Valor numerico legitimo para referencia
        normal_number: '42'
      }
    }

    const resp = await axios.post(`${baseURL}/profile-converter`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201,
      `profile-converter falhou: status=${resp.status} body=${JSON.stringify(resp.data)}`)

    const options = resp.data.options as Record<string, unknown>

    console.log(`[B2] Input:  my_code_id = "007" (string)`)
    console.log(`[B2] Output: my_code_id = ${JSON.stringify(options.my_code_id)} (${typeof options.my_code_id})`)
    console.log(`[B2] Input:  normal_number = "42" (string)`)
    console.log(`[B2] Output: normal_number = ${JSON.stringify(options.normal_number)} (${typeof options.normal_number})`)

    // PROVA: "007" foi convertido para numero 7 (zeros perdidos)
    const myCodeId = options.my_code_id
    const isLeadingZeroLost = typeof myCodeId === 'number' && myCodeId === 7

    console.log(`[B2] Zero a esquerda perdido: ${isLeadingZeroLost}`)
    console.log(`[B2] Bug no profile-converter.class.ts:48-52 — coerceValue`)
    console.log(`[B2]   regex /^[+-]?(?:\\d+\\.\\d+|\\d+)$/i casa com "007"`)
    console.log(`[B2]   Number("007") === 7  ← zero a esquerda perdido`)

    assert.strictEqual(isLeadingZeroLost, true,
      `[B2 PROOF] my_code_id=${JSON.stringify(myCodeId)} (${typeof myCodeId}). ` +
      `Esperava Number 7 (perda de zeros a esquerda). ` +
      `Se o valor preservou o zero (string "007"), o fix ja foi aplicado.`)
  })

  it('PROVA B2-B: "0123" tambem perde o zero a esquerda', async () => {
    const body = {
      type: 'filament' as const,
      data: {
        version: '1.0',
        filament_id: '0123',
        normal_key: 'abc'
      }
    }

    const resp = await axios.post(`${baseURL}/profile-converter`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201)

    const options = resp.data.options as Record<string, unknown>
    const filamentId = options.filament_id

    console.log(`[B2-B] Input:  filament_id = "0123" (string)`)
    console.log(`[B2-B] Output: filament_id = ${JSON.stringify(filamentId)} (${typeof filamentId})`)

    const isLost = typeof filamentId === 'number' && filamentId === 123
    console.log(`[B2-B] Zero a esquerda perdido: ${isLost}`)

    assert.strictEqual(isLost, true,
      `[B2-B] filament_id=${JSON.stringify(filamentId)} (${typeof filamentId}). ` +
      `Esperava Number 123 (zero a esquerda perdido).`)
  })

  it('PROVA B2-C: valores booleanos e strings normais NAO sao afetados (controle)', async () => {
    // Verifica que o bug e especifico a strings que parecem numeros
    const body = {
      type: 'process' as const,
      data: {
        version: '1.0',
        enabled: 'true',
        name_key: 'hello_world',
        float_val: '0.5'
      }
    }

    const resp = await axios.post(`${baseURL}/profile-converter`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201)
    const options = resp.data.options as Record<string, unknown>

    // Boolean deve ser convertido para boolean
    console.log(`[B2-C] enabled = ${JSON.stringify(options.enabled)} (${typeof options.enabled})`)
    // String normal deve permanecer string
    console.log(`[B2-C] name_key = ${JSON.stringify(options.name_key)} (${typeof options.name_key})`)
    // Float deve ser convertido para number (comportamento correto)
    console.log(`[B2-C] float_val = ${JSON.stringify(options.float_val)} (${typeof options.float_val})`)

    assert.strictEqual(options.enabled, true, 'Boolean deve ser true')
    assert.strictEqual(options.name_key, 'hello_world', 'String normal deve ser preservada')
    assert.strictEqual(typeof options.float_val, 'number', 'Float deve virar number (ok)')
  })

  it('PROVA B2-D: o regex nao distingue "007" (ID) de "7" (numero)', () => {
    // Analise estatica do regex que causa o bug
    const regex = /^[+-]?(?:\d+\.\d+|\d+)$/i

    // Strings que deveriam ser tratadas como IDs (com zeros a esquerda)
    const idStrings = ['007', '0001', '0123', '000042']
    for (const s of idStrings) {
      const matches = regex.test(s)
      const converted = Number(s)
      console.log(`[B2 REGEX] "${s}" → regex match: ${matches}, Number("${s}") = ${converted}`)
      // O regex DEVERIA rejeitar strings com leading zeros, mas nao o faz
      assert.strictEqual(matches, true,
        `Regex deveria rejeitar "${s}" (tem zeros a esquerda), mas aceitou.`)
    }
  })
})
