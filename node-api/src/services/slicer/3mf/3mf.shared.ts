// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../../client'
import type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery, Slicer3MfService } from './3mf.class'

export type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery }

export type Slicer3MfClientService = Pick<
  Slicer3MfService<Params<Slicer3MfQuery>>,
  (typeof slicer3MfMethods)[number]
>

export const slicer3MfPath = 'slicer/3mf'

export const slicer3MfMethods: Array<keyof Slicer3MfService> = ['find', 'get', 'create', 'patch', 'remove']

export const slicer3MfClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(slicer3MfPath, connection.service(slicer3MfPath), {
    methods: slicer3MfMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [slicer3MfPath]: Slicer3MfClientService
  }
}
