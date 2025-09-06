// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../../../client'
import type { 
  ThreeMFProcessor, 
  ThreeMFProcessorData, 
  ThreeMFProcessorPatch, 
  ThreeMFProcessorQuery, 
  ThreeMFProcessorService 
} from './3mf.class'

export type { ThreeMFProcessor, ThreeMFProcessorData, ThreeMFProcessorPatch, ThreeMFProcessorQuery }

export type ThreeMFProcessorClientService = Pick<
  ThreeMFProcessorService<Params<ThreeMFProcessorQuery>>,
  (typeof threeMFProcessorMethods)[number]
>

export const threeMFProcessorPath = 'files/processors/3mf'

export const threeMFProcessorMethods: Array<keyof ThreeMFProcessorService> = [
  'find',
  'get', 
  'create',
  'patch',
  'remove'
]

export const threeMFProcessorClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(threeMFProcessorPath, connection.service(threeMFProcessorPath), {
    methods: threeMFProcessorMethods
  })
}

// Add this service to the client service type index
declare module '../../../../client' {
  interface ServiceTypes {
    [threeMFProcessorPath]: ThreeMFProcessorClientService
  }
}
