// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../../client'
import type {
  SlicerModelInfo,
  SlicerModelInfoData,
  SlicerModelInfoPatch,
  SlicerModelInfoQuery,
  SlicerModelInfoService
} from './model-info.class'

export type { SlicerModelInfo, SlicerModelInfoData, SlicerModelInfoPatch, SlicerModelInfoQuery }

export type SlicerModelInfoClientService = Pick<
  SlicerModelInfoService<Params<SlicerModelInfoQuery>>,
  (typeof slicerModelInfoMethods)[number]
>

export const slicerModelInfoPath = 'slicer/model-info'

export const slicerModelInfoMethods: Array<keyof SlicerModelInfoService> = ['find', 'get', 'create']

export const slicerModelInfoClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(slicerModelInfoPath, connection.service(slicerModelInfoPath), {
    methods: slicerModelInfoMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [slicerModelInfoPath]: SlicerModelInfoClientService
  }
}
