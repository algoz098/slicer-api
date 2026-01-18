import * as path from 'node:path'

const addonDir =
  process.env.ORCACLI_ADDON_DIR || path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')

const orca = require(addonDir)

console.log('[Orca] Initializing addon...')
;(orca as any).setLoggingSilenced(true)
const resourcesPath = process.env.ORCACLI_RESOURCES || path.resolve(__dirname, '../../OrcaSlicer/resources')

/**
 * Inicializa o addon OrcaSlicer.
 *
 * O addon funciona em modo on-the-fly puro:
 * - Nenhum vendor bundle e carregado na inicializacao
 * - A configuracao completa e passada via `options` em cada chamada de slice
 * - Os perfis sao resolvidos pelo caller (backend/teste) antes de chamar o addon
 * - O addon usa FullPrintConfig::defaults() como fallback para valores nao especificados
 */
export default function (app: any) {
  try {
    console.log(`[Orca] Started loading. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

    const prevCwd = process.cwd()
    try {
      try {
        process.chdir('/tmp')
      } catch {
        // ignore
      }

      // Inicializa o addon em modo strict (sem auto-loads de env)
      // JSON on-the-fly mode: no profile loading, all config via options
      orca.initialize({
        resourcesPath,
        verbose: false,
        strict: true
      })
      console.log('[Orca] Initialized')

      // NOTE: Nao carregamos nenhum vendor bundle na inicializacao.
      // O addon funciona em modo on-the-fly puro:
      // - A configuracao completa e passada via `options` em cada chamada de slice
      // - Os perfis sao resolvidos pelo caller (backend/teste) antes de chamar o addon
      // - O addon usa FullPrintConfig::defaults() como fallback para valores nao especificados

      console.log(`[Orca] Addon ready. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

      // Registra o addon no app para uso pelos servicos
      app.set('orca', orca)
      app.set('orca_resourcesPath', resourcesPath)
    } finally {
      try {
        process.chdir(prevCwd)
      } catch {
        // ignore
      }
    }
  } catch (e) {
    console.error('[Orca] Fail to load:', e)
    throw e
  }
}
