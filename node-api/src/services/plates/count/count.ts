// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  platesCountDataValidator,
  platesCountPatchValidator,
  platesCountQueryValidator,
  platesCountResolver,
  platesCountExternalResolver,
  platesCountDataResolver,
  platesCountPatchResolver,
  platesCountQueryResolver
} from './count.schema'

import type { Application } from '../../../declarations'
import { PlatesCountService, getOptions } from './count.class'
import { platesCountPath, platesCountMethods } from './count.shared'
import { extractFileUpload } from '../../../hooks/file-upload'

export * from './count.class'
export * from './count.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const platesCount = (app: Application) => {
  // Register our service on the Feathers application
  app.use(platesCountPath, new PlatesCountService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: platesCountMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(platesCountPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(platesCountExternalResolver),
        schemaHooks.resolveResult(platesCountResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(platesCountQueryValidator),
        schemaHooks.resolveQuery(platesCountQueryResolver)
      ],
      create: [
        extractFileUpload(),
        // Temporarily disable validation for testing
        // schemaHooks.validateData(platesCountDataValidator),
        // schemaHooks.resolveData(platesCountDataResolver)
      ]
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
declare module '../../../declarations' {
  interface ServiceTypes {
    [platesCountPath]: PlatesCountService
  }
}
