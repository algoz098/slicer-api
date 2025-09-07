// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { FilesGcodeService } from './gcode.class'

// Main data model schema
export const filesGcodeSchema = Type.Object(
  {
    id: Type.Number(),
    file: Type.Object({
      originalFilename: Type.String(),
      // Buffer is not JSON-serializable; keep this permissive for in-process usage
      buffer: Type.Any()
    })
  },
  { $id: 'FilesGcode', additionalProperties: false }
)
export type FilesGcode = Static<typeof filesGcodeSchema>
export const filesGcodeValidator = getValidator(filesGcodeSchema, dataValidator)
export const filesGcodeResolver = resolve<FilesGcode, HookContext<FilesGcodeService>>({})

export const filesGcodeExternalResolver = resolve<FilesGcode, HookContext<FilesGcodeService>>({})

// Schema for creating new entries (we accept empty payload; file comes from Koa ctx)
export const filesGcodeDataSchema = Type.Object({}, { $id: 'FilesGcodeData' })
export type FilesGcodeData = Static<typeof filesGcodeDataSchema>
export const filesGcodeDataValidator = getValidator(filesGcodeDataSchema, dataValidator)
export const filesGcodeDataResolver = resolve<FilesGcode, HookContext<FilesGcodeService>>({})

// Schema for updating existing entries
export const filesGcodePatchSchema = Type.Partial(filesGcodeSchema, {
  $id: 'FilesGcodePatch'
})
export type FilesGcodePatch = Static<typeof filesGcodePatchSchema>
export const filesGcodePatchValidator = getValidator(filesGcodePatchSchema, dataValidator)
export const filesGcodePatchResolver = resolve<FilesGcode, HookContext<FilesGcodeService>>({})

// Schema for allowed query properties
export const filesGcodeQueryProperties = Type.Pick(filesGcodeSchema, ['id'])
export const filesGcodeQuerySchema = Type.Intersect(
  [
    querySyntax(filesGcodeQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type FilesGcodeQuery = Static<typeof filesGcodeQuerySchema>
export const filesGcodeQueryValidator = getValidator(filesGcodeQuerySchema, queryValidator)
export const filesGcodeQueryResolver = resolve<FilesGcodeQuery, HookContext<FilesGcodeService>>({})
