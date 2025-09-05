// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  filesInfoDataValidator,
  filesInfoPatchValidator,
  filesInfoQueryValidator,
  filesInfoResolver,
  filesInfoExternalResolver,
  filesInfoDataResolver,
  filesInfoPatchResolver,
  filesInfoQueryResolver
} from './info.schema'

import type { Application } from '../../../declarations'
import { FilesInfoService, getOptions } from './info.class'
import { filesInfoPath, filesInfoMethods } from './info.shared'

export * from './info.class'
export * from './info.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const filesInfo = (app: Application) => {
  // Register our service on the Feathers application
  app.use(filesInfoPath, new FilesInfoService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: filesInfoMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(filesInfoPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(filesInfoExternalResolver),
        schemaHooks.resolveResult(filesInfoResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(filesInfoQueryValidator),
        schemaHooks.resolveQuery(filesInfoQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(filesInfoDataValidator),
        schemaHooks.resolveData(filesInfoDataResolver)
      ],
      patch: [
        schemaHooks.validateData(filesInfoPatchValidator),
        schemaHooks.resolveData(filesInfoPatchResolver)
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
declare module '../../../declarations' {
  interface ServiceTypes {
    [filesInfoPath]: FilesInfoService
  }
}
