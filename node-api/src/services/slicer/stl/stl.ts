// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'
import type { HookContext } from '@feathersjs/feathers'

import {
  slicerStlDataValidator,
  slicerStlPatchValidator,
  slicerStlQueryValidator,
  slicerStlResolver,
  slicerStlExternalResolver,
  slicerStlDataResolver,
  slicerStlPatchResolver,
  slicerStlQueryResolver
} from './stl.schema'

import type { Application } from '../../../declarations'
import { SlicerStlService, getOptions } from './stl.class'
import { slicerStlPath, slicerStlMethods } from './stl.shared'

export * from './stl.class'
export * from './stl.schema'

// Hook para parsear campos JSON que vem como string via multipart form-data
const parseJsonFields = async (context: HookContext) => {
  const data = context.data
  if (data && typeof data.config === 'string') {
    try {
      data.config = JSON.parse(data.config)
    } catch (e: any) {
      throw new Error('Failed to parse "config" as JSON')
    }
  }
  if (data && typeof data.options === 'string') {
    try {
      data.options = JSON.parse(data.options)
    } catch (e: any) {
      throw new Error('Failed to parse "options" as JSON')
    }
  }
  return context
}

// A configure function that registers the service and its hooks via `app.configure`
export const slicerStl = (app: Application) => {
  // Register our service on the Feathers application
  app.use(slicerStlPath, new SlicerStlService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: slicerStlMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })
  // Initialize hooks
  app.service(slicerStlPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(slicerStlExternalResolver),
        schemaHooks.resolveResult(slicerStlResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(slicerStlQueryValidator),
        schemaHooks.resolveQuery(slicerStlQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        parseJsonFields,
        schemaHooks.validateData(slicerStlDataValidator),
        schemaHooks.resolveData(slicerStlDataResolver)
      ],
      patch: [
        parseJsonFields,
        schemaHooks.validateData(slicerStlPatchValidator),
        schemaHooks.resolveData(slicerStlPatchResolver)
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
    [slicerStlPath]: SlicerStlService
  }
}
