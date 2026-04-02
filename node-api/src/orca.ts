import * as path from 'node:path'
import { logger } from './logger'

// Default local-first addon resolution in monorepo.
// Can be overridden by ORCACLI_PREFER_PREBUILT=1.
if (!process.env.ORCACLI_PREFER_PREBUILT && !process.env.ORCACLI_PREFER_LOCAL) {
  process.env.ORCACLI_PREFER_LOCAL = '1'
}

const addonDir =
  process.env.ORCACLI_ADDON_DIR || path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')

const orca = require(addonDir)

logger.debug('[Orca] Initializing addon...')
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
    logger.debug(`[Orca] Started loading. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

    const prevCwd = process.cwd()
    try {
      try {
        process.chdir('/tmp')
      } catch {
        // ignore
      }

      // Silencia output verboso do addon C++ apenas durante a inicializacao
      ;(orca as any).setLoggingSilenced(true)
      try {
        // Inicializa o addon em modo strict (sem auto-loads de env)
        // JSON on-the-fly mode: no profile loading, all config via options
        orca.initialize({
          resourcesPath,
          verbose: false,
          strict: true
        })
      } finally {
        // Restaura logging para nao suprimir output do test runner / servidor
        ;(orca as any).setLoggingSilenced(false)
      }
      logger.debug('[Orca] Initialized')

      // NOTE: Nao carregamos nenhum vendor bundle na inicializacao.
      // O addon funciona em modo on-the-fly puro:
      // - A configuracao completa e passada via `options` em cada chamada de slice
      // - Os perfis sao resolvidos pelo caller (backend/teste) antes de chamar o addon
      // - O addon usa FullPrintConfig::defaults() como fallback para valores nao especificados

      logger.debug(`[Orca] Addon ready. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

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
    logger.error('[Orca] Fail to load: %o', e)
    throw e
  }
}
