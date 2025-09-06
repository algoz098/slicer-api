// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../../client'
import type { FilesInfo, FilesInfoData, FilesInfoPatch, FilesInfoQuery, FilesInfoService } from './info.class'

export type { FilesInfo, FilesInfoData, FilesInfoPatch, FilesInfoQuery }

export type FilesInfoClientService = Pick<
  FilesInfoService<Params<FilesInfoQuery>>,
  (typeof filesInfoMethods)[number]
>

export const filesInfoPath = 'files/info'

export const filesInfoMethods: Array<keyof FilesInfoService> = ['find', 'get', 'create', 'patch', 'remove']

export const filesInfoClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(filesInfoPath, connection.service(filesInfoPath), {
    methods: filesInfoMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [filesInfoPath]: FilesInfoClientService
  }
}
