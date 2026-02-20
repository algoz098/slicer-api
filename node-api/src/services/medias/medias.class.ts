import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import type { Application } from '../../declarations'
import type { Medias, MediasData, MediasPatch, MediasQuery } from './medias.schema'

export type { Medias, MediasData, MediasPatch, MediasQuery }
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { BadRequest, NotFound, MethodNotAllowed } from '@feathersjs/errors'

export interface MediasServiceOptions {
  app: Application
}

export interface MediasParams extends Params<MediasQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class MediasService<ServiceParams extends MediasParams = MediasParams>
  implements ServiceInterface<Medias, MediasData, ServiceParams, MediasPatch>
{
  constructor(public options: MediasServiceOptions) {}

  async find(_params?: ServiceParams): Promise<Medias[]> {
    const { path: reqPath } = _params?.query || {}
    if (!reqPath) throw new BadRequest('Missing query param: path')

    const safePath = path.resolve(reqPath)

    // Allowlist of directories
    const allowedDirs = [
      os.tmpdir(),
      path.resolve(this.options.app.get('public') ?? 'public'),
      path.resolve('output_files') // Common output directory
    ]

    const isAllowed = allowedDirs.some(dir => safePath.startsWith(dir))

    if (!isAllowed) {
      // Log attempt?
      throw new BadRequest('Access denied: path not allowed')
    }

    if (!fs.existsSync(safePath)) {
      throw new NotFound('File not found')
    }

    // Return array as expected by find()
    return [{ path: safePath }]
  }

  async get(id: Id, _params?: ServiceParams): Promise<Medias> {
    throw new MethodNotAllowed()
  }

  async create(data: MediasData, params?: ServiceParams): Promise<Medias>
  async create(data: MediasData[], params?: ServiceParams): Promise<Medias[]>
  async create(data: MediasData | MediasData[], params?: ServiceParams): Promise<Medias | Medias[]> {
    throw new MethodNotAllowed()
  }

  // This method has to be added to the 'methods' option to make it available to clients
  async update(id: NullableId, data: MediasData, _params?: ServiceParams): Promise<Medias> {
    throw new MethodNotAllowed()
  }

  async patch(id: NullableId, data: MediasPatch, _params?: ServiceParams): Promise<Medias> {
    throw new MethodNotAllowed()
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<Medias> {
    throw new MethodNotAllowed()
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
