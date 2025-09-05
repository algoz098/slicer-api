// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../client'
import type { 
  Validation, 
  ValidationData, 
  ValidationPatch, 
  ValidationQuery, 
  ValidationService 
} from './validation.class'

export type { Validation, ValidationData, ValidationPatch, ValidationQuery }

export type ValidationClientService = Pick<
  ValidationService<Params<ValidationQuery>>,
  (typeof validationMethods)[number]
>

export const validationPath = 'validation'

export const validationMethods: Array<keyof ValidationService> = [
  'find',
  'get', 
  'create',
  'patch',
  'remove'
]

export const validationClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(validationPath, connection.service(validationPath), {
    methods: validationMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [validationPath]: ValidationClientService
  }
}
