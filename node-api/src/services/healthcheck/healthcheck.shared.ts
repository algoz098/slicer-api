// For more information about this file see https://dove.feathersjs.com/guides/cli/service.shared.html
import type { Params } from '@feathersjs/feathers'

import type { ClientApplication } from '../../client'
import type {
  Healthcheck,
  HealthcheckData,
  HealthcheckPatch,
  HealthcheckQuery,
  HealthcheckService
} from './healthcheck.class'

export type { Healthcheck, HealthcheckData, HealthcheckPatch, HealthcheckQuery }

export type HealthcheckClientService = Pick<
  HealthcheckService<Params<HealthcheckQuery>>,
  (typeof healthcheckMethods)[number]
>

export const healthcheckPath = 'healthcheck'

export const healthcheckMethods: Array<keyof HealthcheckService> = [
  'find'
]

export const healthcheckClient = (client: ClientApplication) => {
  const connection = client.get('connection')

  client.use(healthcheckPath, connection.service(healthcheckPath), {
    methods: healthcheckMethods
  })
}

// Add this service to the client service type index
declare module '../../client' {
  interface ServiceTypes {
    [healthcheckPath]: HealthcheckClientService
  }
}
