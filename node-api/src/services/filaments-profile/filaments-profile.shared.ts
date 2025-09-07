// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../client'
import type {
  FilamentsProfile,
  FilamentsProfileData,
  FilamentsProfilePatch,
  FilamentsProfileQuery,
  FilamentsProfileService
} from './filaments-profile.class'
export type { FilamentsProfile, FilamentsProfileData, FilamentsProfilePatch, FilamentsProfileQuery }

export type FilamentsProfileClientService = Pick<
  FilamentsProfileService<Params<FilamentsProfileQuery>>,
  (typeof filamentsProfileMethods)[number]
>

export const filamentsProfilePath = 'filaments-profile'

export const filamentsProfileMethods: Array<keyof FilamentsProfileService> = [
  'find',
  'get',
  'create',
  'patch',
  'remove'
]

export const filamentsProfileClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(filamentsProfilePath, connection.service(filamentsProfilePath), {
    methods: filamentsProfileMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [filamentsProfilePath]: FilamentsProfileClientService
  }
}
