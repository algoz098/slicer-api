// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  threeMFProcessorDataValidator,
  threeMFProcessorPatchValidator,
  threeMFProcessorQueryValidator,
  threeMFProcessorResolver,
  threeMFProcessorExternalResolver,
  threeMFProcessorDataResolver,
  threeMFProcessorPatchResolver,
  threeMFProcessorQueryResolver
} from './3mf.schema'
import type { Application } from '../../../../declarations'
import { ThreeMFProcessorService, getOptions } from './3mf.class'
import { threeMFProcessorPath, threeMFProcessorMethods } from './3mf.shared'

export * from './3mf.class'
export * from './3mf.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const threeMFProcessor = (app: Application) => {
  // Register our service on the Feathers application
  app.use(threeMFProcessorPath, new ThreeMFProcessorService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: threeMFProcessorMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  
  // Initialize hooks
  app.service(threeMFProcessorPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(threeMFProcessorExternalResolver),
        schemaHooks.resolveResult(threeMFProcessorResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(threeMFProcessorQueryValidator),
        schemaHooks.resolveQuery(threeMFProcessorQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(threeMFProcessorDataValidator),
        schemaHooks.resolveData(threeMFProcessorDataResolver)
      ],
      patch: [
        schemaHooks.validateData(threeMFProcessorPatchValidator),
        schemaHooks.resolveData(threeMFProcessorPatchResolver)
      ],
      remove: []
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
declare module '../../../../declarations' {
  interface ServiceTypes {
    [threeMFProcessorPath]: ThreeMFProcessorService
  }
}
