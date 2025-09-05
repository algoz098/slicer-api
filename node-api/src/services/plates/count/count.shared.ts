// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'
import type { ClientApplication } from '../../../client'
import type {
  PlatesCount,
  PlatesCountData,
  PlatesCountPatch,
  PlatesCountQuery,
  PlatesCountService
} from './count.class'

export type { PlatesCount, PlatesCountData, PlatesCountPatch, PlatesCountQuery }

export type PlatesCountClientService = Pick<
  PlatesCountService<Params<PlatesCountQuery>>,
  (typeof platesCountMethods)[number]
>

export const platesCountPath = 'plates/count'

export const platesCountMethods: Array<keyof PlatesCountService> = [
  'create'
]

export const platesCountClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(platesCountPath, connection.service(platesCountPath), {
    methods: platesCountMethods
  })
}

// Add this service to the client service type index
declare module '../../../client' {
  interface ServiceTypes {
    [platesCountPath]: PlatesCountClientService
  }
}
