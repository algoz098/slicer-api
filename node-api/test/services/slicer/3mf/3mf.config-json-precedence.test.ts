/**
 * Test: Config JSON deve ter precedencia absoluta sobre configs do 3MF
 *
 * Este teste valida que quando enviamos config JSON com valores especificos,
 * esses valores SAO usados no slice, nao os valores embutidos no 3MF.
 *
 * O teste extrai o arquivo de configuracao do 3MF gerado e verifica
 * que os valores correspondem ao que foi enviado no JSON.
 */

import assert from 'assert'
import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { execSync } from 'node:child_process'
import type { Server } from 'http'

describe('slicer/3mf - Config JSON Precedence (Automated Validation)', function () {
  this.timeout(180000)

  let server: Server | null = null
  let baseURL = ''
  let appRef: any = null
  let input3mf: string

  before(async () => {
    const nodeApiRoot = path.resolve(__dirname, '../../../..')
    const repoRoot = path.resolve(nodeApiRoot, '..')

    process.env.ORCACLI_PREFER_LOCAL = '1'
    process.env.ORCACLI_ENGINE_PATH = path.join(
      repoRoot,
      'OrcaSlicerAddon/build/bindings/node/liborcacli_engine.dylib'
    )

    const mod = await import('../../../../src/app')
    appRef = (mod as any).app
    server = await appRef.listen(0)

    const address = server!.address()
    const port = typeof address === 'string' || address === null ? 0 : (address as any).port
    baseURL = `http://127.0.0.1:${port}`

    input3mf = path.resolve(__dirname, '../../../../../example_files/3DBenchy.3mf')
  })

  after(async () => {
    if (appRef) await appRef.teardown()
  })

  /**
   * Extrai valor de uma chave do G-code dentro do 3MF
   * O G-code contem os valores REAIS usados no slice (apos aplicar overrides)
   * Formato: "; key = value"
   */
  function extractConfigValue(outputPath: string, key: string): string | null {
    try {
      const cmd = `unzip -p "${outputPath}" "Metadata/plate_1.gcode" 2>/dev/null | grep -E "^; ${key} = "`
      const result = execSync(cmd, { encoding: 'utf-8' })
      // Parse: "; key = value"
      const match = result.match(new RegExp(`^; ${key} = (.+)$`, 'm'))
      return match ? match[1].trim() : null
    } catch {
      return null
    }
  }

  it('DEVE aplicar valores do config JSON em vez dos valores do 3MF', async () => {
    if (!fs.existsSync(input3mf)) {
      console.warn('Arquivo 3DBenchy.3mf nao encontrado, pulando teste')
      return
    }

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'test_config_json_precedence.gcode.3mf')

    // Valores DISTINTOS que nao existem normalmente em profiles padrao
    // Isso garante que se o teste passar, e porque o config JSON foi aplicado
    const config = {
      layer_height: 0.12, // Valor incomum
      sparse_infill_density: 77, // Valor incomum (77%)
      wall_loops: 5, // Valor incomum
      top_shell_layers: 7, // Valor incomum
      bottom_shell_layers: 6 // Valor incomum
    }

    const body = {
      filePath: input3mf,
      // output: outTarget, // REMOVED: output path is now managed by the server for security
      plate: 1,
      // NAO passamos profiles - usamos apenas o config JSON sobre o 3MF
      // A logica e: 3MF ja tem profiles embutidos, mas o config JSON deve sobrescrever
      // Habilitamos transfer para usar os profiles do 3MF como base
      // E o config JSON deve sobrescrever os valores especificos
      // Config JSON que DEVE ter precedencia sobre profiles E 3MF
      config
    }

    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(
      resp.status,
      201,
      `Esperado 201, recebido ${resp.status}: ${JSON.stringify(resp.data)}`
    )
    assert.ok(fs.existsSync(resp.data.outputPath), 'Arquivo de saida deve existir')

    // Extrair e validar valores do 3MF gerado
    const outputPath = resp.data.outputPath

    const actualLayerHeight = extractConfigValue(outputPath, 'layer_height')
    const actualInfillDensity = extractConfigValue(outputPath, 'sparse_infill_density')
    const actualWallLoops = extractConfigValue(outputPath, 'wall_loops')
    const actualTopShell = extractConfigValue(outputPath, 'top_shell_layers')
    const actualBottomShell = extractConfigValue(outputPath, 'bottom_shell_layers')

    console.log('Valores extraidos do 3MF gerado:')
    console.log('  layer_height:', actualLayerHeight, '(esperado: 0.12)')
    console.log('  sparse_infill_density:', actualInfillDensity, '(esperado: 77%)')
    console.log('  wall_loops:', actualWallLoops, '(esperado: 5)')
    console.log('  top_shell_layers:', actualTopShell, '(esperado: 7)')
    console.log('  bottom_shell_layers:', actualBottomShell, '(esperado: 6)')

    // Validacoes
    assert.strictEqual(actualLayerHeight, '0.12', `layer_height deveria ser 0.12, mas e ${actualLayerHeight}`)
    assert.strictEqual(
      actualInfillDensity,
      '77%',
      `sparse_infill_density deveria ser 77%, mas e ${actualInfillDensity}`
    )
    assert.strictEqual(actualWallLoops, '5', `wall_loops deveria ser 5, mas e ${actualWallLoops}`)
    assert.strictEqual(actualTopShell, '7', `top_shell_layers deveria ser 7, mas e ${actualTopShell}`)
    assert.strictEqual(
      actualBottomShell,
      '6',
      `bottom_shell_layers deveria ser 6, mas e ${actualBottomShell}`
    )
  })
})
