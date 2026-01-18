// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'

import type { Application } from '../../declarations'
import type { Medias, MediasData, MediasPatch, MediasQuery } from './medias.schema'

export type { Medias, MediasData, MediasPatch, MediasQuery }
import * as fs from 'node:fs'

export interface MediasServiceOptions {
  app: Application
}

export interface MediasParams extends Params<MediasQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class MediasService<ServiceParams extends MediasParams = MediasParams>
  implements ServiceInterface<Medias, MediasData, ServiceParams, MediasPatch>
{
  constructor(public options: MediasServiceOptions) {}

  async find(_params?: ServiceParams): Promise<any> {
    const { path } = _params?.query || {}
    if (!path) throw new Error('Missing query params')
    //check if file exists
    if (!fs.existsSync(path)) throw new Error('File not found')
    return { path }
  }

  async get(id: Id, _params?: ServiceParams): Promise<Medias> {
    return {
      id: 0,
      text: `A new message with ID: ${id}!`
    }
  }

  async create(data: MediasData, params?: ServiceParams): Promise<Medias>
  async create(data: MediasData[], params?: ServiceParams): Promise<Medias[]>
  async create(data: MediasData | MediasData[], params?: ServiceParams): Promise<Medias | Medias[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    return {
      id: 0,
      ...data
    }
  }

  // This method has to be added to the 'methods' option to make it available to clients
  async update(id: NullableId, data: MediasData, _params?: ServiceParams): Promise<Medias> {
    return {
      id: 0,
      ...data
    }
  }

  async patch(id: NullableId, data: MediasPatch, _params?: ServiceParams): Promise<Medias> {
    return {
      id: 0,
      text: `Fallback for ${id}`,
      ...data
    }
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<Medias> {
    return {
      id: 0,
      text: 'removed'
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
