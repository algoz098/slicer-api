// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../../client'
import type { 
  FilesUpload, 
  FilesUploadData, 
  FilesUploadPatch, 
  FilesUploadQuery, 
  FilesUploadService 
} from './upload.class'

export type { FilesUpload, FilesUploadData, FilesUploadPatch, FilesUploadQuery }

export type FilesUploadClientService = Pick<
  FilesUploadService<Params<FilesUploadQuery>>,
  (typeof filesUploadMethods)[number]
>

export const filesUploadPath = 'files/upload'

export const filesUploadMethods: Array<keyof FilesUploadService> = [
  'find',
  'get', 
  'create',
  'patch',
  'remove'
]

export const filesUploadClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(filesUploadPath, connection.service(filesUploadPath), {
    methods: filesUploadMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [filesUploadPath]: FilesUploadClientService
  }
}
