// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../client'
import type { Medias, MediasData, MediasPatch, MediasQuery, MediasService } from './medias.class'

export type { Medias, MediasData, MediasPatch, MediasQuery }

export type MediasClientService = Pick<MediasService<Params<MediasQuery>>, (typeof mediasMethods)[number]>

export const mediasPath = 'medias'

export const mediasMethods: Array<keyof MediasService> = ['find', 'get', 'create', 'patch', 'remove']

export const mediasClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(mediasPath, connection.service(mediasPath), {
    methods: mediasMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [mediasPath]: MediasClientService
  }
}
