// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'
import JSZip from 'jszip'
import { sanitizeBblGcodeTemplates } from './gcode-sanitizer'

import type { Application } from '../../../declarations'
import { logger } from '../../../logger'
import type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery } from './3mf.schema'
import { BadRequest } from '@feathersjs/errors'

export type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery }

// Profile settings passed to OrcaSlicer. It is essentially a dynamic
// key/value map coming from 3MF metadata, but we explicitly model the
// fields we touch below to keep TypeScript happy without perder
// informações.
type ProfileSettings = {
  curr_bed_type: string
  flush_volumes_matrix?: unknown
  filament_colour?: unknown
  flush_multiplier?: unknown
} & Record<string, unknown>

export interface Slicer3MfServiceOptions {
  app: Application
}

export interface Slicer3MfParams extends Params<Slicer3MfQuery> {}

export class Slicer3MfService<ServiceParams extends Slicer3MfParams = Slicer3MfParams>
  implements ServiceInterface<Slicer3Mf, Slicer3MfData, ServiceParams, Slicer3MfPatch>
{
  constructor(public options: Slicer3MfServiceOptions) {}

  async find(_params?: ServiceParams): Promise<Slicer3Mf[]> {
    return []
  }

  async get(id: Id, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id),
      filename: undefined,
      outputPath: ''
    }
  }

  async create(data: Slicer3MfData, params?: ServiceParams): Promise<Slicer3Mf>
  async create(data: Slicer3MfData[], params?: ServiceParams): Promise<Slicer3Mf[]>
  async create(
    data: Slicer3MfData | Slicer3MfData[],
    params?: ServiceParams
  ): Promise<Slicer3Mf | Slicer3Mf[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    const orca = this.options.app.get('orca')
    if (!orca) {
      throw new Error('OrcaSlicer addon not loaded')
    }

    const { options } =
      data ?? {
        options: {}
      }
    const reqField = data.field ?? 'file'
    const anyParams: any = params ?? {}

    const filesContainer =
      anyParams?.koa?.request?.files ?? anyParams?.files ?? anyParams?.koa?.ctx?.request?.files

    let fileObj: any | undefined
    if (filesContainer) {
      fileObj = filesContainer[reqField] ?? filesContainer.file
      if (Array.isArray(fileObj)) fileObj = fileObj[0]
      if (!fileObj && typeof filesContainer === 'object') {
        const first = Object.values(filesContainer)[0]
        fileObj = Array.isArray(first) ? first[0] : first
      }
    }

    let inputPath: string | undefined = data.filePath
    let originalFilename: string | undefined

    if (fileObj) {
      inputPath = fileObj.filepath || fileObj.path || fileObj.tempFilePath || inputPath
      originalFilename = fileObj.originalFilename || fileObj.name || fileObj.filename || originalFilename
    }

    if (!inputPath) {
      throw new Error('Nenhum arquivo recebido. Envie um multipart field "file" ou informe "filePath".')
    }

    if (!fs.existsSync(inputPath)) {
      throw new BadRequest('Input file not found')
    }

    const inputStats = fs.statSync(inputPath)
    logger.debug(`[3MF] Input file: ${inputPath}, Size: ${inputStats.size} bytes`)
    if (inputStats.size === 0) {
      logger.warn('[3MF] Check: Input file is empty!')
    }

    // Força o caminho de saída para ser no diretório temporário para segurança
    // Ignora data.output enviado pelo usuário para evitar Arbitrary File Write
    const outputFilename = `orca-${randomUUID()}.gcode.3mf`
    const outPath = path.join(os.tmpdir(), outputFilename)

    // Validate input file is a valid ZIP/3MF
    try {
      const fileContent = fs.readFileSync(inputPath)
      const zip = await JSZip.loadAsync(fileContent)
      logger.debug('[3MF] Input file is a valid ZIP. Contents:')
      const files = Object.keys(zip.files)

      for (const f of files) {
        const fileData = zip.files[f]
        // Log size for .model files or config
        if (f.endsWith('.model') || f.endsWith('.config')) {
          const content = await fileData.async('nodebuffer')
          logger.debug(`  - ${f} (Size: ${content.length} bytes)`)
          if (content.length === 0) {
            logger.warn(`[3MF] WARNING: Internal file ${f} is empty!`)
          }
        } else {
          logger.debug(`  - ${f}`)
        }
      }

      if (files.length === 0) {
        logger.warn('[3MF] Input ZIP is empty!')
      }
    } catch (err: any) {
      logger.error('[3MF] Input file is NOT a valid ZIP: %s', err?.message ?? String(err))
    }

    // NOTE: Nao carregamos vendors/profiles aqui.
    // O addon funciona em modo on-the-fly puro:
    // - A configuracao completa e passada via `options` em cada chamada de slice
    // - O addon usa FullPrintConfig::defaults() como fallback
    logger.debug('[3MF] On-the-fly mode - configuration passed via options')

    let output: string
    let usedOptions: string[] | undefined
    let ignoredOptions: string[] | undefined
    let estimatedTimeSec: number | undefined
    let filamentUsedGrams: number | undefined

    // Precedência correta:
    //   config (perfil base on-the-fly)  <  3MF (mesa/objeto)  <  options (overrides explícitos)
    // O campo `profile` é aplicado antes de carregar o 3MF; o 3MF sobrescreve o perfil.
    // O campo `options` é aplicado depois do 3MF com prioridade máxima.
    const configOverrides: Record<string, unknown> = (data as any).config ?? {}
    const profileSettings: ProfileSettings = {
      ...configOverrides,
      curr_bed_type: 'High Temp Plate',
    }
    const finalOptions = { ...options }

    // Sanitize BBL-proprietary G-code template variables before passing to OrcaSlicer.
    sanitizeBblGcodeTemplates(profileSettings)

    // Remove flush_volumes_matrix from profile if it doesn't match the filament count
    if (profileSettings.flush_volumes_matrix) {
      const filamentCount = Array.isArray(profileSettings.filament_colour)
        ? profileSettings.filament_colour.length
        : 1
      const headsCount = Array.isArray(profileSettings.flush_multiplier)
        ? profileSettings.flush_multiplier.length
        : 1
      const expectedSize = filamentCount * filamentCount * headsCount
      const actualSize = Array.isArray(profileSettings.flush_volumes_matrix)
        ? profileSettings.flush_volumes_matrix.length
        : 0
      if (actualSize !== expectedSize) {
        logger.debug(
          `[3MF] Removing inconsistent flush_volumes_matrix: expected=${expectedSize}, actual=${actualSize}`
        )
        delete profileSettings.flush_volumes_matrix
      }
    }

    // Guarda as chaves de options (overrides explícitos) para validação posterior
    const optionsKeys = new Set(Object.keys(options ?? {}))

    // Create a safe copy of the input file to ensure access and simple path
    const safeInputPath = path.join(os.tmpdir(), `safe_input_${randomUUID()}.3mf`)
    fs.copyFileSync(inputPath, safeInputPath)
    logger.debug(
      `[3MF] Copied input to safe path: ${safeInputPath} (Size: ${fs.statSync(safeInputPath).size})`
    )

    try {
      let res: any
      try {
        res = await orca.slice({
          input: safeInputPath,
          output: outPath,
          plate: data.plate,
          profile: profileSettings,
          options: Object.keys(finalOptions).length > 0 ? finalOptions : undefined,
          center: true,
          autoRealignIfNeeded: true,
        })
      } finally {
      }
      output = res.output

      usedOptions = (res as any)?.usedOptions
      ignoredOptions = (res as any)?.ignoredOptions
      estimatedTimeSec = (res as any)?.estimatedTimeSec
      filamentUsedGrams = (res as any)?.filamentUsedGrams
    } catch (err: any) {
      // 1. Log everything we know about the error for debugging
      const msg = String(err?.message ?? err ?? '')
      const details = String(err?.errorDetails ?? err?.error_details ?? '')
      logger.error('[3MF] Slice failed. input=%s plate=%s msg="%s" details="%s"',
        safeInputPath, data.plate ?? 'all', msg, details)
      if (err?.stack) logger.error('[3MF] Stack: %s', err.stack)

      const lower = msg.toLowerCase()
      const lowerDetails = details.toLowerCase()

      // 2. Erro de elementos fora da area de impressao
      if (
        lower.includes('fora da área') ||
        lower.includes('fora da area') ||
        lower.includes('outside') ||
        lower.includes('out of bounds') ||
        lower.includes('does not fit')
      ) {
        throw new BadRequest(msg, { code: 'OBJECTS_OUT_OF_BOUNDS' })
      }

      // 3. Overrides invalidas
      if (
        lower.includes('unknown') ||
        lower.includes('invalid') ||
        lower.includes('unrecognized') ||
        lower.includes('failed to set')
      ) {
        throw new BadRequest(`Invalid override option(s): ${msg}`)
      }

      // 4. SlicingErrors — erros lancados por SlicingError/SlicingErrors do OrcaSlicer.
      //    Agora o addon extrai as mensagens reais via reinterpret_cast do layout em memoria
      //    (contorno para duplicacao de typeinfo entre liblibslic3r.a e orcacli_core).
      //    Padroes conhecidos (GCode.cpp, Print.cpp, PrintObjectSlice.cpp):
      //    - "One object has empty initial layer and can't be printed..."
      //    - "No object can be printed. Maybe too small"
      //    - "The print is empty. The model is not printable with current print settings."
      //    - "No layers were detected..."
      //    - "Levitating objects cannot be printed without supports."
      //    Formato antigo (antes do fix): "Slicing failed: Errors"
      //    Ver docs/bugs/2026-06-06-slicing-error-propagation.md
      const knownSlicingErrorPatterns = [
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
      const isSlicingError = knownSlicingErrorPatterns.some(
        p => lower.includes(p) || details.toLowerCase().includes(p)
      )
      if (isSlicingError) {
        const hint = data.plate
          ? `plate #${data.plate}`
          : 'the model'
        logger.warn('[3MF] Slicing error for %s: %s', hint, msg)
        throw new BadRequest(msg, { code: 'SLICING_ERROR', originalError: msg })
      }

      // 6. Falha na exportacao do gcode
      if (lower.includes('g-code export failed') || lower.includes('gcode export failed')) {
        throw new BadRequest(
          `O OrcaSlicer concluiu o calculo mas falhou ao exportar o G-code. ` +
          `Pode ser problema de permissao de escrita em disco ou falta de espaco em ${os.tmpdir()}.`,
          { code: 'GCODE_EXPORT_FAILED' }
        )
      }

      // 7. Arquivo gerado muito pequeno (provavelmente vazio)
      if (lower.includes('too small') || lower.includes('file size')) {
        throw new BadRequest(
          `O G-code gerado e muito pequeno (${msg}). O slicing pode ter produzido um resultado vazio.`,
          { code: 'GCODE_FILE_TOO_SMALL' }
        )
      }

      // 8. Sempre lancar uma instancia Error propria para evitar "error: undefined" nos logs
      throw new Error(msg || 'Slice failed')
    }

    // Verifica se alguma opcao de 'options' foi ignorada (erro 400)
    // Opcoes de 'config' sao ignoradas silenciosamente
    const ignoredFromOptions = (ignoredOptions ?? []).filter((k: string) => optionsKeys.has(k))
    if (ignoredFromOptions.length > 0) {
      throw new BadRequest(`Invalid override option(s): unknown keys: ${ignoredFromOptions.join(', ')}`)
    }

    // Garante existência do arquivo antes de responder.
    if (!fs.existsSync(output)) {
      throw new Error('Falha ao gerar .gcode.3mf')
    }

    const content = await fs.promises.readFile(output)

    // Validação defensiva: .3mf deve ser um ZIP válido com pelo menos um G-code embutido.
    // Isso evita retornar arquivos inválidos quando um binário antigo do addon exporta
    // G-code puro com extensão .3mf.
    try {
      const zip = await JSZip.loadAsync(content)
      const fileNames = Object.keys(zip.files)
      const hasEmbeddedGcode = fileNames.some(
        name => /(^|\/)Metadata\/.+\.gcode$/i.test(name) || /\.gcode$/i.test(name)
      )
      if (!hasEmbeddedGcode) {
        throw new Error('3MF sem G-code embutido (Metadata/*.gcode não encontrado)')
      }
    } catch (zipErr: any) {
      const details = String(zipErr?.message ?? zipErr ?? 'erro desconhecido')
      throw new Error(
        `3MF inválido gerado pelo addon: ${details}. ` +
          'Verifique se o runtime está carregando o binário local atualizado do OrcaSlicerAddon.'
      )
    }
    // const dataBase64 = content.toString('base64')

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      contentType: 'model/3mf',
      size: content.length,
      dataBase64: content.toString('base64'),
      usedOptions,
      ignoredOptions,
      estimatedTimeSec,
      filamentUsedGrams
    }
  }

  async update(id: NullableId, _data: Slicer3MfData, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }

  async patch(id: NullableId, _data: Slicer3MfPatch, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
