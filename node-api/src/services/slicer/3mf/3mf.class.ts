// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'

import type { Application } from '../../../declarations'
import type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery } from './3mf.schema'

export type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery }

// eslint-disable-next-line @typescript-eslint/no-var-requires
const orca = require('../../../../../OrcaSlicerCli/bindings/node')
let orcaInitialized = false
const ensureOrcaInitialized = () => {
  if (!orcaInitialized) {
    const resourcesPath = process.env.ORCACLI_RESOURCES || path.resolve(__dirname, '../../../../../OrcaSlicer/resources')
    orca.initialize({ resourcesPath, verbose: false })
    orcaInitialized = true
  }
}

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

    ensureOrcaInitialized()

    const reqField = data.field ?? 'file'
    const anyParams: any = params ?? {}

    const filesContainer = anyParams?.koa?.request?.files ?? anyParams?.files ?? anyParams?.koa?.ctx?.request?.files

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

    const { output } = await orca.slice({
      input: inputPath,
      output: outPath,
      plate: data.plate,
      printerProfile: data.printerProfile,
      filamentProfile: data.filamentProfile,
      processProfile: data.processProfile
    })

    // Garante existência do arquivo antes de responder.
    if (!fs.existsSync(output)) {
      throw new Error('Falha ao gerar .gcode.3mf')
    }

    const content = await fs.promises.readFile(output)
    const dataBase64 = content.toString('base64')

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      contentType: 'model/3mf',
      size: content.length,
      dataBase64
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
