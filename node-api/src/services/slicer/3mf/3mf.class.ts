// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'

import type { Application } from '../../../declarations'
import type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery } from './3mf.schema'
import { BadRequest } from '@feathersjs/errors'

export type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery }
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

    const orca = await this.options.app.get('orca')

    const { options, printerProfileName, filamentProfileName, processProfileName, center, bedType } =
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

    // Define caminho de saída padrão com extensão .gcode.3mf
    const defaultOut = path.join(os.tmpdir(), `orca-${randomUUID()}.gcode.3mf`)
    const outPath = data.output ?? defaultOut

    // NOTE: Nao carregamos vendors/profiles aqui.
    // O addon funciona em modo on-the-fly puro:
    // - A configuracao completa e passada via `options` em cada chamada de slice
    // - O addon usa FullPrintConfig::defaults() como fallback
    console.log('[3MF] On-the-fly mode - configuration passed via options')

    let output: string
    let usedOptions: string[] | undefined
    let ignoredOptions: string[] | undefined
    let estimatedTimeSec: number | undefined
    let filamentUsedGrams: number | undefined

    // Mescla options e config, onde config tem precedencia
    // Precedencia: config > options > profiles
    const configOverrides = (data as any).config ?? {}
    const finalOptions = { ...options, ...configOverrides }
    finalOptions.curr_bed_type = bedType ?? 'High Temp Plate'

    // Remove flush_volumes_matrix from overrides if it doesn't match the filament count
    // This prevents "Flush volumes matrix do not match to the correct size" errors
    // The addon will automatically synchronize the matrix based on filament_colour
    if (finalOptions.flush_volumes_matrix) {
      const filamentCount = Array.isArray(finalOptions.filament_colour)
        ? finalOptions.filament_colour.length
        : 1
      const headsCount = Array.isArray(finalOptions.flush_multiplier)
        ? finalOptions.flush_multiplier.length
        : 1
      const expectedSize = filamentCount * filamentCount * headsCount
      const actualSize = Array.isArray(finalOptions.flush_volumes_matrix)
        ? finalOptions.flush_volumes_matrix.length
        : 0
      if (actualSize !== expectedSize) {
        console.log(
          `[3MF] Removing inconsistent flush_volumes_matrix: expected=${expectedSize}, actual=${actualSize}`
        )
        delete finalOptions.flush_volumes_matrix
      }
    }

    // Guarda as chaves de options para validacao posterior
    const optionsKeys = new Set(Object.keys(options ?? {}))

    try {
      // TEMPORARILY DISABLE SILENCING TO DEBUG "Comparing incompatible types" error
      ;(orca as any).setLoggingSilenced(false)
      let res: any
      try {
        res = await orca.slice({
          input: inputPath,
          output: outPath,
          plate: data.plate,
          options: finalOptions,
          center: true,
          autoRealignIfNeeded: true,
          // Display names for profiles in output 3MF (metadata only, does not load any preset)
          printerProfileName: printerProfileName,
          filamentProfileName: filamentProfileName,
          processProfileName: processProfileName,
          transferPrinterCustomizations: data.transferPrinterCustomizations ?? true,
          transferFilamentCustomizations: data.transferFilamentCustomizations ?? true,
          transferProcessCustomizations: data.transferProcessCustomizations ?? true,
          transferProjectOverrides: data.transferProjectOverrides ?? true
        })
      } finally {
        ;(orca as any).setLoggingSilenced(false)
      }
      output = res.output

      usedOptions = (res as any)?.usedOptions
      ignoredOptions = (res as any)?.ignoredOptions
      estimatedTimeSec = (res as any)?.estimatedTimeSec
      filamentUsedGrams = (res as any)?.filamentUsedGrams
    } catch (err: any) {
      const msg = String(err?.message ?? err ?? '')
      const lower = msg.toLowerCase()

      // Erro de elementos fora da area de impressao
      if (
        lower.includes('fora da área') ||
        lower.includes('fora da area') ||
        lower.includes('outside') ||
        lower.includes('out of bounds') ||
        lower.includes('does not fit')
      ) {
        throw new BadRequest(msg, { code: 'OBJECTS_OUT_OF_BOUNDS' })
      }

      if (
        lower.includes('unknown') ||
        lower.includes('invalid') ||
        lower.includes('unrecognized') ||
        lower.includes('failed to set')
      ) {
        throw new BadRequest(`Invalid override option(s): ${msg}`)
      }
      // Always throw a proper Error instance to avoid "error: undefined" logs
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
    // const dataBase64 = content.toString('base64')

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      contentType: 'model/3mf',
      size: content.length,
      // dataBase64,
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
