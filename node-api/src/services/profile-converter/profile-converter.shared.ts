// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../client'
import type {
  ProfileConverter,
  ProfileConverterData,
  ProfileConverterPatch,
  ProfileConverterQuery,
  ProfileConverterService
} from './profile-converter.class'

export type { ProfileConverter, ProfileConverterData, ProfileConverterPatch, ProfileConverterQuery }

export type ProfileConverterClientService = Pick<
  ProfileConverterService<Params<ProfileConverterQuery>>,
  (typeof profileConverterMethods)[number]
>

export const profileConverterPath = 'profile-converter'

export const profileConverterMethods: Array<keyof ProfileConverterService> = [
  'find',
  'get',
  'create',
  'patch',
  'remove'
]

export const profileConverterClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(profileConverterPath, connection.service(profileConverterPath), {
    methods: profileConverterMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [profileConverterPath]: ProfileConverterClientService
  }
}
