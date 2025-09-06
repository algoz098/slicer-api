// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  filamentsProfileDataValidator,
  filamentsProfilePatchValidator,
  filamentsProfileQueryValidator,
  filamentsProfileResolver,
  filamentsProfileExternalResolver,
  filamentsProfileDataResolver,
  filamentsProfilePatchResolver,
  filamentsProfileQueryResolver
} from './filaments-profile.schema'

import type { Application } from '../../declarations'
import { FilamentsProfileService, getOptions } from './filaments-profile.class'
import { filamentsProfilePath, filamentsProfileMethods } from './filaments-profile.shared'

export * from './filaments-profile.class'
export * from './filaments-profile.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const filamentsProfile = (app: Application) => {
  // Register our service on the Feathers application
  app.use(filamentsProfilePath, new FilamentsProfileService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: filamentsProfileMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(filamentsProfilePath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(filamentsProfileExternalResolver),
        schemaHooks.resolveResult(filamentsProfileResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(filamentsProfileQueryValidator),
        schemaHooks.resolveQuery(filamentsProfileQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(filamentsProfileDataValidator),
        schemaHooks.resolveData(filamentsProfileDataResolver)
      ],
      patch: [
        schemaHooks.validateData(filamentsProfilePatchValidator),
        schemaHooks.resolveData(filamentsProfilePatchResolver)
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
    [filamentsProfilePath]: FilamentsProfileService
  }
}
