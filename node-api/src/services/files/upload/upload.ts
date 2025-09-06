// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  filesUploadDataValidator,
  filesUploadPatchValidator,
  filesUploadQueryValidator,
  filesUploadResolver,
  filesUploadExternalResolver,
  filesUploadDataResolver,
  filesUploadPatchResolver,
  filesUploadQueryResolver
} from './upload.schema'
import type { Application } from '../../../declarations'
import { FilesUploadService, getOptions } from './upload.class'
import { filesUploadPath, filesUploadMethods } from './upload.shared'

export * from './upload.class'
export * from './upload.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const filesUpload = (app: Application) => {
  // Register our service on the Feathers application
  app.use(filesUploadPath, new FilesUploadService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: filesUploadMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  
  // Initialize hooks
  app.service(filesUploadPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(filesUploadExternalResolver),
        schemaHooks.resolveResult(filesUploadResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(filesUploadQueryValidator),
        schemaHooks.resolveQuery(filesUploadQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(filesUploadDataValidator),
        schemaHooks.resolveData(filesUploadDataResolver)
      ],
      patch: [
        schemaHooks.validateData(filesUploadPatchValidator),
        schemaHooks.resolveData(filesUploadPatchResolver)
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
    [filesUploadPath]: FilesUploadService
  }
}
