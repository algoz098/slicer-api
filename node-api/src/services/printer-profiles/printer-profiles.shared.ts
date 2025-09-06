// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../client'
import type {
  PrinterProfiles,
  PrinterProfilesData,
  PrinterProfilesPatch,
  PrinterProfilesQuery,
  PrinterProfilesService
} from './printer-profiles.class'

export type { PrinterProfiles, PrinterProfilesData, PrinterProfilesPatch, PrinterProfilesQuery }

export type PrinterProfilesClientService = Pick<
  PrinterProfilesService<Params<PrinterProfilesQuery>>,
  (typeof printerProfilesMethods)[number]
>

export const printerProfilesPath = 'printer-profiles'

export const printerProfilesMethods: Array<keyof PrinterProfilesService> = [
  'find',
  'get',
  'create',
  'patch',
  'remove'
]

export const printerProfilesClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(printerProfilesPath, connection.service(printerProfilesPath), {
    methods: printerProfilesMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [printerProfilesPath]: PrinterProfilesClientService
  }
}
