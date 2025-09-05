// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  printerProfilesDataValidator,
  printerProfilesPatchValidator,
  printerProfilesQueryValidator,
  printerProfilesResolver,
  printerProfilesExternalResolver,
  printerProfilesDataResolver,
  printerProfilesPatchResolver,
  printerProfilesQueryResolver
} from './printer-profiles.schema'

import type { Application } from '../../declarations'
import { PrinterProfilesService, getOptions } from './printer-profiles.class'
import { printerProfilesPath, printerProfilesMethods } from './printer-profiles.shared'

export * from './printer-profiles.class'
export * from './printer-profiles.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const printerProfiles = (app: Application) => {
  // Register our service on the Feathers application
  app.use(printerProfilesPath, new PrinterProfilesService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: printerProfilesMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(printerProfilesPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(printerProfilesExternalResolver),
        schemaHooks.resolveResult(printerProfilesResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(printerProfilesQueryValidator),
        schemaHooks.resolveQuery(printerProfilesQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(printerProfilesDataValidator),
        schemaHooks.resolveData(printerProfilesDataResolver)
      ],
      patch: [
        schemaHooks.validateData(printerProfilesPatchValidator),
        schemaHooks.resolveData(printerProfilesPatchResolver)
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
    [printerProfilesPath]: PrinterProfilesService
  }
}
