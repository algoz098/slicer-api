// For more information about this file see https://dove.feathersjs.com/guides/cli/service.html

import { hooks as schemaHooks } from '@feathersjs/schema'

import {
  slicer3MfDataValidator,
  slicer3MfPatchValidator,
  slicer3MfQueryValidator,
  slicer3MfResolver,
  slicer3MfExternalResolver,
  slicer3MfDataResolver,
  slicer3MfPatchResolver,
  slicer3MfQueryResolver
} from './3mf.schema'

import type { Application } from '../../../declarations'
import { Slicer3MfService, getOptions } from './3mf.class'
import { slicer3MfPath, slicer3MfMethods } from './3mf.shared'

export * from './3mf.class'
export * from './3mf.schema'

// A configure function that registers the service and its hooks via `app.configure`
export const slicer3Mf = (app: Application) => {
  // Register our service on the Feathers application
  app.use(slicer3MfPath, new Slicer3MfService(getOptions(app)), {
    // A list of all methods this service exposes externally
    methods: slicer3MfMethods,
    // You can add additional custom events to be sent to clients here
    events: []
  })

  // Initialize hooks
  app.service(slicer3MfPath).hooks({
    around: {
      all: [
        schemaHooks.resolveExternal(slicer3MfExternalResolver),
        schemaHooks.resolveResult(slicer3MfResolver)
      ]
    },
    before: {
      all: [
        schemaHooks.validateQuery(slicer3MfQueryValidator),
        schemaHooks.resolveQuery(slicer3MfQueryResolver)
      ],
      find: [],
      get: [],
      create: [
        (ctx: any) => {
          const { data } = ctx

          // Coerce multipart 'options' (string) to object
          if (data?.options) {
            if (typeof data.options === 'string') {
              try {
                data.options = JSON.parse(data.options)
              } catch (err: any) {
                throw new Error('Failed to parse "options" as JSON')
              }
            }
          }

          // Coerce multipart 'config' (string) to object
          if (data?.config) {
            if (typeof data.config === 'string') {
              try {
                data.config = JSON.parse(data.config)
              } catch (err: any) {
                throw new Error('Failed to parse "config" as JSON')
              }
            }
          }

          // Coerce multipart 'plate' (string) to number
          if (data?.plate != null && typeof data?.plate === 'string') {
            const n = Number(data.plate)
            if (Number.isFinite(n)) data.plate = n
          }

          if (!data) ctx.data = {}
          else ctx.data = data
        },
        schemaHooks.validateData(slicer3MfDataValidator),
        schemaHooks.resolveData(slicer3MfDataResolver)
      ],
      patch: [
        schemaHooks.validateData(slicer3MfPatchValidator),
        schemaHooks.resolveData(slicer3MfPatchResolver)
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
    [slicer3MfPath]: Slicer3MfService
  }
}
