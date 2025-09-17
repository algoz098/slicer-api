// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../../client'
import type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery, SlicerStlService } from './stl.class'

export type { SlicerStl, SlicerStlData, SlicerStlPatch, SlicerStlQuery }

export type SlicerStlClientService = Pick<
  SlicerStlService<Params<SlicerStlQuery>>,
  (typeof slicerStlMethods)[number]
>

export const slicerStlPath = 'slicer/stl'

export const slicerStlMethods: Array<keyof SlicerStlService> = ['find', 'get', 'create', 'patch', 'remove']

export const slicerStlClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(slicerStlPath, connection.service(slicerStlPath), {
    methods: slicerStlMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [slicerStlPath]: SlicerStlClientService
  }
}
