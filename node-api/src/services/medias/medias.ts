// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  mediasDataValidator,
  mediasPatchValidator,
  mediasQueryValidator,
  mediasResolver,
  mediasExternalResolver,
  mediasDataResolver,
  mediasPatchResolver,
  mediasQueryResolver
} from './medias.schema'

import type { Application } from '../../declarations'
import { MediasService, getOptions } from './medias.class'
import { mediasPath, mediasMethods } from './medias.shared'

export * from './medias.class'
export * from './medias.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const medias = (app: Application) => {
  // Register our service on the Feathers application
  app.use(mediasPath, new MediasService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: mediasMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(mediasPath).hooks({
    around: {
      all: [schemaHooks.resolveExternal(mediasExternalResolver), schemaHooks.resolveResult(mediasResolver)]
    },
    before: {
      all: [schemaHooks.validateQuery(mediasQueryValidator), schemaHooks.resolveQuery(mediasQueryResolver)],
      find: [],
      get: [],
      create: [schemaHooks.validateData(mediasDataValidator), schemaHooks.resolveData(mediasDataResolver)],
      patch: [schemaHooks.validateData(mediasPatchValidator), schemaHooks.resolveData(mediasPatchResolver)],
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
    [mediasPath]: MediasService
  }
}
