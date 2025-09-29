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
    console.log(0)
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    const orca = await this.options.app.get('orca')

    const { options } = data ?? {options: {}}
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

    // Carrega sob demanda somente os presets requisitados por nome
    try {
      const resourcesPath: string | undefined = (this.options.app as any).get('orca_resourcesPath')
      const resolveVendorForPreset = (subdir: 'machine' | 'filament' | 'process', presetName: string): string | undefined => {
        if (!resourcesPath) return undefined
        const profilesDir = path.join(resourcesPath, 'profiles')
        try {
          const vendors = fs.readdirSync(profilesDir, { withFileTypes: true })
          for (const v of vendors) {
            if (!v.isDirectory()) continue
            const vendorId = v.name
            const candidate = path.join(profilesDir, vendorId, subdir, `${presetName}.json`)
            if (fs.existsSync(candidate)) return vendorId
          }
        } catch {
          // silencioso: se não for possível resolver por FS, seguimos sem vendor explícito
        }
        return undefined
      }

      if (data.printerProfile && typeof data.printerProfile === 'string') {
        try {
          const vendor = resolveVendorForPreset('machine', data.printerProfile)
          if (vendor) orca.loadVendor(vendor)
          orca.loadPrinterProfile(data.printerProfile)
        } catch (e) {
          throw new BadRequest(`Printer preset not found: ${data.printerProfile}`)
        }
      }

      if (data.filamentProfile && typeof data.filamentProfile === 'string') {
        try {
          const vendor = resolveVendorForPreset('filament', data.filamentProfile)
          if (vendor) orca.loadVendor(vendor)
          orca.loadFilamentProfile(data.filamentProfile)
        } catch (e) {
          throw new BadRequest(`Filament preset not found: ${data.filamentProfile}`)
        }
      }

      if (data.processProfile && typeof data.processProfile === 'string') {
        try {
          const vendor = resolveVendorForPreset('process', data.processProfile)
          if (vendor) orca.loadVendor(vendor)
          orca.loadProcessProfile(data.processProfile)
        } catch (e) {
          throw new BadRequest(`Process preset not found: ${data.processProfile}`)
        }
      }
    } catch (e) {
      // Repassa BadRequest ou outros erros para o handler abaixo
      throw e
    }

    let output: string
    let usedOptions: string[] | undefined
    let ignoredOptions: string[] | undefined
    try {
      const res = await orca.slice({
        input: inputPath,
        output: outPath,
        plate: data.plate,
        printerProfile: data.printerProfile,
        filamentProfile: data.filamentProfile,
        processProfile: data.processProfile,
        options
      })
      output = res.output
      usedOptions = (res as any)?.usedOptions
      ignoredOptions = (res as any)?.ignoredOptions
    } catch (err: any) {
      const msg = String(err?.message || err)
      const lower = msg.toLowerCase()
      if (lower.includes('unknown') || lower.includes('invalid') || lower.includes('unrecognized') || lower.includes('failed to set')) {
        throw new BadRequest(`Invalid override option(s): ${msg}`)
      }
      throw err
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
      ignoredOptions
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
