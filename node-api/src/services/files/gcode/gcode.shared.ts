// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../../client'
import type {
  FilesGcode,
  FilesGcodeData,
  FilesGcodePatch,
  FilesGcodeQuery,
  FilesGcodeService
} from './gcode.class'

export type { FilesGcode, FilesGcodeData, FilesGcodePatch, FilesGcodeQuery }

export type FilesGcodeClientService = Pick<
  FilesGcodeService<Params<FilesGcodeQuery>>,
  (typeof filesGcodeMethods)[number]
>

export const filesGcodePath = 'files/gcode'

export const filesGcodeMethods: Array<keyof FilesGcodeService> = ['find', 'get', 'create', 'patch', 'remove']

export const filesGcodeClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(filesGcodePath, connection.service(filesGcodePath), {
    methods: filesGcodeMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [filesGcodePath]: FilesGcodeClientService
  }
}
