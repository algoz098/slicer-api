import { hooks as schemaHooks } from '@feathersjs/schema'
import type { Application } from '../../../declarations'
import { filesGcodeDataResolver, filesGcodeDataValidator, filesGcodeExternalResolver, filesGcodeQueryResolver, filesGcodeQueryValidator, filesGcodeResolver } from './gcode.schema'
import { DebugPreambleService, getDebugOptions } from './debug-preamble.service'
import { filesGcodePath } from './gcode.shared'

export const filesGcodeDebugPreamble = (app: Application) => {
  const debugPath = filesGcodePath + '/debug-preamble'
  app.use(debugPath, new DebugPreambleService(getDebugOptions(app)), {
    methods: ['create'],
    events: []
  })
  app.service(debugPath).hooks({
    around: { all: [schemaHooks.resolveExternal(filesGcodeExternalResolver), schemaHooks.resolveResult(filesGcodeResolver)] },
    before: { all: [schemaHooks.validateQuery(filesGcodeQueryValidator), schemaHooks.resolveQuery(filesGcodeQueryResolver)], create: [schemaHooks.validateData(filesGcodeDataValidator), schemaHooks.resolveData(filesGcodeDataResolver)] },
    after: { all: [] },
    error: { all: [] }
  })
}

declare module '../../../declarations' {
  interface ServiceTypes {
    [typeof filesGcodePath + '/debug-preamble']: DebugPreambleService
  }
}

