// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  filesGcodeDataValidator,
  filesGcodePatchValidator,
  filesGcodeQueryValidator,
  filesGcodeResolver,
  filesGcodeExternalResolver,
  filesGcodeDataResolver,
  filesGcodePatchResolver,
  filesGcodeQueryResolver
} from './gcode.schema'

import type { Application } from '../../../declarations'
import { FilesGcodeService, getOptions } from './gcode.class'
import { filesGcodePath, filesGcodeMethods } from './gcode.shared'

export * from './gcode.class'
export * from './gcode.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const filesGcode = (app: Application) => {
  // Register our service on the Feathers application
  app.use(filesGcodePath, new FilesGcodeService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: filesGcodeMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(filesGcodePath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(filesGcodeExternalResolver),
        schemaHooks.resolveResult(filesGcodeResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(filesGcodeQueryValidator),
        schemaHooks.resolveQuery(filesGcodeQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(filesGcodeDataValidator),
        schemaHooks.resolveData(filesGcodeDataResolver)
      ],
      patch: [
        schemaHooks.validateData(filesGcodePatchValidator),
        schemaHooks.resolveData(filesGcodePatchResolver)
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
    [filesGcodePath]: FilesGcodeService
    [filesGcodePath + '/debug-preamble']: any
  }
}
