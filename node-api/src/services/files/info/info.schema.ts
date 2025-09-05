// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { FilesInfoService } from './info.class'

// Nozzle diameter pattern (e.g., "0.4", "0.6", "1.0")
const nozzleDiameterPattern = /^[0-9]+\.[0-9]+$/

// Enhanced schema with better validation
export const filesInfoSchema = Type.Object(
  {
    printer: Type.Optional(
      Type.String({
        minLength: 1,
        maxLength: 200,
        description: 'Printer model or manufacturer name'
      })
    ),
    nozzle: Type.Optional(
      Type.String({
        pattern: nozzleDiameterPattern.source,
        description: 'Nozzle diameter in mm (e.g., "0.4", "0.6")'
      })
    ),
    technicalName: Type.Optional(
      Type.String({
        minLength: 1,
        maxLength: 500,
        description: 'Technical profile name from slicer software'
      })
    ),
    profile: Type.Optional(
      Type.String({
        minLength: 1,
        maxLength: 200,
        description: 'Print profile name (e.g., "Standard", "Fine", "Draft")'
      })
    )
  },
  {
    $id: 'FilesInfo',
    additionalProperties: false,
    description: 'Information extracted from 3D printing files'
  }
)

export type FilesInfo = Static<typeof filesInfoSchema>
export const filesInfoValidator = getValidator(filesInfoSchema, dataValidator)
export const filesInfoResolver = resolve<FilesInfo, HookContext<FilesInfoService>>({})

export const filesInfoExternalResolver = resolve<FilesInfo, HookContext<FilesInfoService>>({})

// Schema for creating new entries (file upload data)
export const filesInfoDataSchema = Type.Object({
  // No direct data properties since this service processes file uploads
  // The actual data comes from the uploaded file via multipart form data
}, {
  $id: 'FilesInfoData',
  description: 'Data schema for file upload processing - actual data comes from uploaded files'
})

export type FilesInfoData = Static<typeof filesInfoDataSchema>
export const filesInfoDataValidator = getValidator(filesInfoDataSchema, dataValidator)
export const filesInfoDataResolver = resolve<FilesInfo, HookContext<FilesInfoService>>({})

// Schema for updating existing entries
export const filesInfoPatchSchema = Type.Partial(filesInfoSchema, {
  $id: 'FilesInfoPatch'
})
export type FilesInfoPatch = Static<typeof filesInfoPatchSchema>
export const filesInfoPatchValidator = getValidator(filesInfoPatchSchema, dataValidator)
export const filesInfoPatchResolver = resolve<FilesInfo, HookContext<FilesInfoService>>({})

// Schema for allowed query properties
export const filesInfoQueryProperties = Type.Pick(filesInfoSchema, ['printer', 'nozzle', 'technicalName', 'profile'])
export const filesInfoQuerySchema = Type.Intersect(
  [
    querySyntax(filesInfoQueryProperties),
    // Add additional query properties for filtering and searching
    Type.Object({
      // Support for partial text matching
      $text: Type.Optional(Type.String({
        description: 'Full-text search across all fields'
      })),
      // Support for file format filtering (though currently only 3MF is supported)
      fileFormat: Type.Optional(Type.Union([
        Type.Literal('3mf'),
        Type.Literal('stl'),
        Type.Literal('gcode')
      ], {
        description: 'Filter by file format'
      }))
    }, { additionalProperties: false })
  ],
  {
    additionalProperties: false,
    description: 'Query parameters for searching file information'
  }
)

export type FilesInfoQuery = Static<typeof filesInfoQuerySchema>
export const filesInfoQueryValidator = getValidator(filesInfoQuerySchema, queryValidator)
export const filesInfoQueryResolver = resolve<FilesInfoQuery, HookContext<FilesInfoService>>({})
