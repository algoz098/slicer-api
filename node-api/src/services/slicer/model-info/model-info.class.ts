// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'

import type { Application } from '../../../declarations'
import type {
  SlicerModelInfo,
  SlicerModelInfoData,
  SlicerModelInfoPatch,
  SlicerModelInfoQuery
} from './model-info.schema'
import { BadRequest } from '@feathersjs/errors'
import { withSilencedLogging } from '../logging-guard'

export type { SlicerModelInfo, SlicerModelInfoData, SlicerModelInfoPatch, SlicerModelInfoQuery }

export interface SlicerModelInfoServiceOptions {
  app: Application
}

export interface SlicerModelInfoParams extends Params<SlicerModelInfoQuery> {}

export class SlicerModelInfoService<ServiceParams extends SlicerModelInfoParams = SlicerModelInfoParams>
  implements ServiceInterface<SlicerModelInfo, SlicerModelInfoData, ServiceParams, SlicerModelInfoPatch>
{
  constructor(public options: SlicerModelInfoServiceOptions) {}

  async find(_params?: ServiceParams): Promise<SlicerModelInfo[]> {
    return []
  }

  async get(id: Id, _params?: ServiceParams): Promise<SlicerModelInfo> {
    return {
      id: String(id),
      objectCount: 0,
      triangleCount: 0,
      volume: 0,
      boundingBox: '',
      isValid: false
    }
  }

  async create(data: SlicerModelInfoData, params?: ServiceParams): Promise<SlicerModelInfo>
  async create(data: SlicerModelInfoData[], params?: ServiceParams): Promise<SlicerModelInfo[]>
  async create(
    data: SlicerModelInfoData | SlicerModelInfoData[],
    params?: ServiceParams
  ): Promise<SlicerModelInfo | SlicerModelInfo[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    const orca = this.options.app.get('orca')
    if (!orca) {
      throw new Error('OrcaSlicer addon not loaded')
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
      originalFilename = fileObj.originalFilename || fileObj.name || fileObj.filename
    }

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
      throw new BadRequest('Nenhum arquivo recebido. Envie um multipart field "file" ou informe "filePath".')
    }

    if (!fs.existsSync(inputPath)) {
      throw new BadRequest('Input file not found')
    }

    // Copy to safe temp path to ensure simple filename for the addon
    const ext = path.extname(originalFilename ?? inputPath) || '.stl'
    const safeInputPath = path.join(os.tmpdir(), `model-info-${randomUUID()}${ext}`)
    fs.copyFileSync(inputPath, safeInputPath)

    try {
      const info = await withSilencedLogging(orca, () => (orca as any).getModelInfo(safeInputPath))

      if (!info) {
        throw new Error('Model info retrieval returned no data')
      }

      return {
        id: randomUUID(),
        filename: originalFilename,
        objectCount: info.objectCount ?? 0,
        triangleCount: info.triangleCount ?? 0,
        volume: info.volume ?? 0,
        boundingBox: info.boundingBox ?? '',
        isValid: info.isValid ?? true
      }
    } catch (err: any) {
      const msg = String(err?.message ?? err ?? '')
      const lower = msg.toLowerCase()

      if (lower.includes('unknown') || lower.includes('invalid') || lower.includes('unrecognized') || lower.includes('failed to set')) {
        throw new BadRequest(`Invalid option(s): ${msg}`)
      }
      throw new Error(msg || 'Model info retrieval failed')
    } finally {
      try {
        fs.unlinkSync(safeInputPath)
      } catch {
        // best-effort cleanup
      }
      if (fileObj) {
        try {
          fs.unlinkSync(inputPath)
        } catch {
          // best-effort cleanup
        }
      }
    }
  }

  async update(
    id: NullableId,
    _data: SlicerModelInfoData,
    _params?: ServiceParams
  ): Promise<SlicerModelInfo> {
    return {
      id: String(id ?? ''),
      objectCount: 0,
      triangleCount: 0,
      volume: 0,
      boundingBox: '',
      isValid: false
    }
  }

  async patch(
    id: NullableId,
    _data: SlicerModelInfoPatch,
    _params?: ServiceParams
  ): Promise<SlicerModelInfo> {
    return {
      id: String(id ?? ''),
      objectCount: 0,
      triangleCount: 0,
      volume: 0,
      boundingBox: '',
      isValid: false
    }
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<SlicerModelInfo> {
    return {
      id: String(id ?? ''),
      objectCount: 0,
      triangleCount: 0,
      volume: 0,
      boundingBox: '',
      isValid: false
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
