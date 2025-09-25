// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  profileConverterDataValidator,
  profileConverterPatchValidator,
  profileConverterQueryValidator,
  profileConverterResolver,
  profileConverterExternalResolver,
  profileConverterDataResolver,
  profileConverterPatchResolver,
  profileConverterQueryResolver
} from './profile-converter.schema'

import type { Application } from '../../declarations'
import { ProfileConverterService, getOptions } from './profile-converter.class'
import { profileConverterPath, profileConverterMethods } from './profile-converter.shared'

export * from './profile-converter.class'
export * from './profile-converter.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const profileConverter = (app: Application) => {
  // Register our service on the Feathers application
  app.use(profileConverterPath, new ProfileConverterService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: profileConverterMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(profileConverterPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(profileConverterExternalResolver),
        schemaHooks.resolveResult(profileConverterResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(profileConverterQueryValidator),
        schemaHooks.resolveQuery(profileConverterQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(profileConverterDataValidator),
        schemaHooks.resolveData(profileConverterDataResolver)
      ],
      patch: [
        schemaHooks.validateData(profileConverterPatchValidator),
        schemaHooks.resolveData(profileConverterPatchResolver)
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
    [profileConverterPath]: ProfileConverterService
  }
}
