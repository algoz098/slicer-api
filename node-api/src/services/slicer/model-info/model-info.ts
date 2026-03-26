// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html
import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  slicerModelInfoDataValidator,
  slicerModelInfoPatchValidator,
  slicerModelInfoQueryValidator,
  slicerModelInfoResolver,
  slicerModelInfoExternalResolver,
  slicerModelInfoDataResolver,
  slicerModelInfoPatchResolver,
  slicerModelInfoQueryResolver
} from './model-info.schema'

import type { Application } from '../../../declarations'
import { SlicerModelInfoService, getOptions } from './model-info.class'
import { slicerModelInfoPath, slicerModelInfoMethods } from './model-info.shared'

export * from './model-info.class'
export * from './model-info.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const slicerModelInfo = (app: Application) => {
  app.use(slicerModelInfoPath, new SlicerModelInfoService(getOptions(app)), {
    methods: slicerModelInfoMethods,
    events: []
  })
  app.service(slicerModelInfoPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(slicerModelInfoExternalResolver),
        schemaHooks.resolveResult(slicerModelInfoResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(slicerModelInfoQueryValidator),
        schemaHooks.resolveQuery(slicerModelInfoQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        schemaHooks.validateData(slicerModelInfoDataValidator),
        schemaHooks.resolveData(slicerModelInfoDataResolver)
      ],
      patch: [
        schemaHooks.validateData(slicerModelInfoPatchValidator),
        schemaHooks.resolveData(slicerModelInfoPatchResolver)
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
    [slicerModelInfoPath]: SlicerModelInfoService
  }
}
