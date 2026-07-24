// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'

import type { Application } from '../../../declarations'
import type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery } from './stl.schema'
import { BadRequest } from '@feathersjs/errors'
import { sanitizeBblGcodeTemplates } from '../3mf/gcode-sanitizer'
import { withSilencedLogging } from '../logging-guard'

export type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery }

export interface SlicerStlServiceOptions {
  app: Application
}

export interface SlicerStlParams extends Params<SlicerStlQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class SlicerStlService<ServiceParams extends SlicerStlParams = SlicerStlParams>
  implements ServiceInterface<SlicerStl, SlicerStlData, ServiceParams, SlicerStlPatch>
{
  constructor(public options: SlicerStlServiceOptions) {}

  async find(_params?: ServiceParams): Promise<SlicerStl[]> {
    return []
  }

  async get(id: Id, _params?: ServiceParams): Promise<SlicerStl> {
    return {
      id: String(id),
      filename: undefined,
      outputPath: '',
      gcode: ''
    }
  }

  async create(data: SlicerStlData, params?: ServiceParams): Promise<SlicerStl>
  async create(data: SlicerStlData[], params?: ServiceParams): Promise<SlicerStl[]>
  async create(
    data: SlicerStlData | SlicerStlData[],
    params?: ServiceParams
  ): Promise<SlicerStl | SlicerStl[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    // Access the shared addon instance configured by src/orca.ts
    const orcaAny = this.options.app.get('orca')
    if (!orcaAny) {
      throw new Error('OrcaSlicer addon not loaded')
    }

    const reqField = data.field ?? 'file'
    const anyParams: any = params ?? {}

    // Detecta arquivo vindo via multipart (koa-body, multer, etc.)
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

    // Security: if filePath is provided directly (not from upload), restrict to tmpdir
    if (inputPath && !fileObj) {
      let resolved: string
      try {
        resolved = fs.realpathSync(path.resolve(inputPath))
      } catch {
        throw new BadRequest('Input file not found or not accessible')
      }
      const tmpDir = os.tmpdir()
      const prefix = tmpDir.endsWith(path.sep) ? tmpDir : tmpDir + path.sep
      if (!resolved.startsWith(prefix)) {
        throw new BadRequest('filePath must be within the temp directory or use multipart upload')
      }
      inputPath = resolved
    }

    if (!inputPath) {
      throw new Error('Nenhum arquivo recebido. Envie um multipart field "file" ou informe "filePath".')
    }

    // Security: Validate input path
    // If inputPath comes from user (not file upload), ensure it exists and is within allowed boundaries if necessary
    // For now, simple check
    if (!fs.existsSync(inputPath)) {
      throw new BadRequest('Input file not found')
    }

    // Forca o caminho de saida para ser no diretorio temporario para seguranca
    // Ignora data.output enviado pelo usuario para evitar Arbitrary File Write
    const outputFilename = `orca-${randomUUID()}.gcode`
    const outPath = path.join(os.tmpdir(), outputFilename)

    // NOTE: Nao carregamos vendors/profiles aqui.
    // O addon funciona em modo on-the-fly puro:
    // - A configuracao completa e passada via `options` em cada chamada de slice
    // - O addon usa FullPrintConfig::defaults() como fallback

    // Precedencia: options (explicit overrides) > config (base profile) > defaults
    const baseOptions = (data as any).options ?? {}
    const configOverrides = (data as any).config ?? {}

    const sanitizedConfig = Object.keys(configOverrides).length > 0
      ? sanitizeBblGcodeTemplates({ ...configOverrides })
      : configOverrides
    const sanitizedOptions = Object.keys(baseOptions).length > 0
      ? sanitizeBblGcodeTemplates({ ...baseOptions })
      : baseOptions

    // Guarda as chaves de options para validacao posterior
    const optionsKeys = new Set(Object.keys(baseOptions))

    // Executa fatiamento via N-API
    let output: string
    let ignoredOptions: string[] = []
    let estimatedTimeSec: number | undefined
    let filamentUsedGrams: number | undefined
    try {
      const res = await withSilencedLogging(orcaAny, () =>
        orcaAny.slice({
          input: inputPath,
          output: outPath,
          plate: data.plate,
          profile: Object.keys(sanitizedConfig).length > 0 ? sanitizedConfig : undefined,
          options: Object.keys(sanitizedOptions).length > 0 ? sanitizedOptions : undefined,
          center: true,
          autoRealignIfNeeded: true
        })
      )
      output = res.output
      ignoredOptions = res.ignoredOptions ?? []
      estimatedTimeSec = res.estimatedTimeSec
      filamentUsedGrams = res.filamentUsedGrams
    } catch (err: any) {
      const rawMsg = String(err?.message ?? err ?? '')
      const msg = (rawMsg && rawMsg !== 'null') ? rawMsg : 'Slice failed'
      const lower = msg.toLowerCase()
      // SlicingErrors — erros do OrcaSlicer engine
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
        'does not fit',
        'outside',
        'out of bounds'
      ]
      const isSlicingError = knownSlicingErrorPatterns.some(p => lower.includes(p))
      if (isSlicingError) {
        throw new BadRequest(msg, { code: 'SLICING_ERROR', originalError: msg })
      }
      if (
        lower.includes('unknown') ||
        lower.includes('invalid') ||
        lower.includes('unrecognized') ||
        lower.includes('failed to set')
      ) {
        throw new BadRequest(`Invalid override option(s): ${msg}`)
      }
      // Sempre propague um Error bem formado para evitar logs "error: undefined"
      throw new Error(msg || 'Slice failed')
    }

    // Verifica se alguma opcao de 'options' foi ignorada (erro 400)
    // Opcoes de 'config' sao ignoradas silenciosamente
    const ignoredFromOptions = ignoredOptions.filter((k: string) => optionsKeys.has(k))
    if (ignoredFromOptions.length > 0) {
      throw new BadRequest(`Invalid override option(s): unknown keys: ${ignoredFromOptions.join(', ')}`)
    }

    const gcode = await fs.promises.readFile(output, 'utf8')

    // Clean up temporary files
    try { await fs.promises.unlink(outPath) } catch { /* ignore cleanup errors */ }
    if (fileObj) {
      try { await fs.promises.unlink(inputPath) } catch { /* ignore */ }
    }

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      gcode,
      estimatedTimeSec,
      filamentUsedGrams
    }
  }

  // This method has to be added to the 'methods' option to make it available to clients
  async update(id: NullableId, _data: SlicerStlData, _params?: ServiceParams): Promise<SlicerStl> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: '',
      gcode: ''
    }
  }

  async patch(id: NullableId, _data: SlicerStlPatch, _params?: ServiceParams): Promise<SlicerStl> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: '',
      gcode: ''
    }
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<SlicerStl> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: '',
      gcode: ''
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
