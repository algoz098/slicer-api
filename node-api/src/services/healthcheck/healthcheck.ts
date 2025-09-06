// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  healthcheckQueryValidator,
  healthcheckResolver,
  healthcheckExternalResolver,
  healthcheckQueryResolver
} from './healthcheck.schema'
import type { Application } from '../../declarations'
import { HealthcheckService, getOptions } from './healthcheck.class'
import { healthcheckPath, healthcheckMethods } from './healthcheck.shared'

export * from './healthcheck.class'
export * from './healthcheck.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const healthcheck = (app: Application) => {
  // Register our service on the Feathers application
  app.use(healthcheckPath, new HealthcheckService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: healthcheckMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(healthcheckPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(healthcheckExternalResolver),
        schemaHooks.resolveResult(healthcheckResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(healthcheckQueryValidator),
        schemaHooks.resolveQuery(healthcheckQueryResolver)
      ],
      find: []
    },
    after: {
      all: []
    },
    error: {
      all: []
    }
  })
}

// Add this service to the service type index
declare module '../../declarations' {
  interface ServiceTypes {
    [healthcheckPath]: HealthcheckService
  }
}
