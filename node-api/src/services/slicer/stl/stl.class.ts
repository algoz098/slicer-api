// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'

import type { Application } from '../../../declarations'
import type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery } from './stl.schema'
import { BadRequest } from '@feathersjs/errors'

export type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery }

// Carrega o addon N-API do OrcaSlicerAddon. Preferimos ORCACLI_ADDON_DIR em dev para usar o addon precompilado da imagem.
// eslint-disable-next-line @typescript-eslint/no-var-requires
const addonDir =
  process.env.ORCACLI_ADDON_DIR || path.resolve(__dirname, '../../../../../OrcaSlicerAddon/bindings/node')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const orca = require(addonDir)

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
    const orcaFromApp = (this.options.app as any)?.get?.('orca')
    const orcaAny = (orcaFromApp || orca) as any

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

    if (!inputPath) {
      throw new Error('Nenhum arquivo recebido. Envie um multipart field "file" ou informe "filePath".')
    }

    // Define caminho de saída do G-code
    const outPath = data.output ?? path.join(os.tmpdir(), `orca-${randomUUID()}.gcode`)

    // NOTE: Nao carregamos vendors/profiles aqui.
    // O addon funciona em modo on-the-fly puro:
    // - A configuracao completa e passada via `options` em cada chamada de slice
    // - O addon usa FullPrintConfig::defaults() como fallback

    // Mescla options e config, onde config tem precedencia
    // Precedencia: config > options > profiles
    const baseOptions = (data as any).options ?? {}
    const configOverrides = (data as any).config ?? {}
    const finalOptions = { ...baseOptions, ...configOverrides }

    // Guarda as chaves de options para validacao posterior
    const optionsKeys = new Set(Object.keys(baseOptions))

    // Executa fatiamento via N-API
    let output: string
    let ignoredOptions: string[] = []
    try {
      orcaAny.setLoggingSilenced(true)
      let res: any
      try {
        res = await orcaAny.slice({
          input: inputPath,
          output: outPath,
          plate: data.plate,
          options: finalOptions,
          center: true,
          autoRealignIfNeeded: true
        })
      } finally {
        orcaAny.setLoggingSilenced(false)
      }
      output = res.output
      ignoredOptions = res.ignoredOptions ?? []
    } catch (err: any) {
      const msg = String(err?.message ?? err ?? '')
      const lower = msg.toLowerCase()
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

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      gcode
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
