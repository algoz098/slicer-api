// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  validationDataValidator,
  validationPatchValidator,
  validationQueryValidator,
  validationResolver,
  validationExternalResolver,
  validationDataResolver,
  validationPatchResolver,
  validationQueryResolver
} from './validation.schema'

import type { Application } from '../../declarations'
import { ValidationService, getOptions } from './validation.class'
import { validationPath, validationMethods } from './validation.shared'

export * from './validation.class'
export * from './validation.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const validation = (app: Application) => {
  // Register our service on the Feathers application
  app.use(validationPath, new ValidationService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: validationMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  
  // Initialize hooks
  app.service(validationPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(validationExternalResolver),
        schemaHooks.resolveResult(validationResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(validationQueryValidator),
        schemaHooks.resolveQuery(validationQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(validationDataValidator),
        schemaHooks.resolveData(validationDataResolver)
      ],
      patch: [
        schemaHooks.validateData(validationPatchValidator),
        schemaHooks.resolveData(validationPatchResolver)
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
declare module '../../declarations' {
  interface ServiceTypes {
    [validationPath]: ValidationService
  }
}
