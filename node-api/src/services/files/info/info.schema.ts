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
    ),
    // Differences agora é um ARRAY de diferenças entre arquivo e perfil da impressora
    differences: Type.Optional(Type.Array(
      Type.Object({
        parameter: Type.String({
          description: 'Name of the parameter that differs'
        }),
        fileValue: Type.Union([Type.String(), Type.Number(), Type.Boolean(), Type.Null()], {
          description: 'Value from the uploaded file'
        }),
        profileValue: Type.Optional(Type.Union([Type.String(), Type.Number(), Type.Boolean(), Type.Null()], {
          description: 'Value from the selected printer profile'
        }))
      })
    )),

    // Valores do perfil da impressora para comparação
    printerProfileValues: Type.Optional(Type.Record(
      Type.String(),
      Type.Any(),
      {
        description: 'Values from the printer profile used for comparison'
      }
    )),

    // Perfil do filamento
    filamentProfile: Type.Optional(Type.Record(
      Type.String(),
      Type.Any(),
      {
        description: 'Filament profile values and settings'
      }
    )),


    // Print settings extracted from file
    printSettings: Type.Optional(
      Type.Object({
        sparseInfillPercentage: Type.Optional(Type.Number({
          minimum: 0,
          maximum: 100,
          description: 'Infill percentage (0-100%)'
        })),
        layerHeight: Type.Optional(Type.Number({
          minimum: 0.01,
          maximum: 1.0,
          description: 'Layer height in mm'
        })),
        printSpeed: Type.Optional(Type.Number({
          minimum: 1,
          maximum: 1000,
          description: 'Print speed in mm/s'
        })),
        bedTemperature: Type.Optional(Type.Number({
          minimum: 0,
          maximum: 200,
          description: 'Bed temperature in Celsius'
        })),
        nozzleTemperature: Type.Optional(Type.Number({
          minimum: 0,
          maximum: 400,
          description: 'Nozzle temperature in Celsius'
        })),
        supportEnabled: Type.Optional(Type.Boolean({
          description: 'Whether supports are enabled'
        })),
        adhesionType: Type.Optional(Type.String({
          description: 'Bed adhesion type (e.g., "brim", "raft", "skirt")'
        })),
        filamentType: Type.Optional(Type.String({
          description: 'Filament material type (e.g., "PLA", "ABS", "PETG")'
        }))
      }, {
        description: 'Print settings extracted from the file'
      })
    ),
    // Plate count information
    plateCount: Type.Optional(Type.Number({
      minimum: 0,
      description: 'Number of plates/objects detected in the file'
    })),
    // Comparison with selected printer profile
    profileComparison: Type.Optional(
      Type.Object({
        selectedProfileId: Type.Optional(Type.String({
          description: 'ID of the printer profile used for comparison'
        })),
        differences: Type.Optional(Type.Array(
          Type.Object({
            parameter: Type.String({
              description: 'Name of the parameter that differs'
            }),
            fileValue: Type.Union([Type.String(), Type.Number(), Type.Boolean(), Type.Null()], {
              description: 'Value from the uploaded file'
            }),
            profileValue: Type.Union([Type.String(), Type.Number(), Type.Boolean(), Type.Null()], {
              description: 'Value from the selected printer profile'
            })
          })
        )),
        summary: Type.Optional(Type.Object({
          totalDifferences: Type.Number({
            description: 'Total number of parameter differences found'
          }),
          criticalDifferences: Type.Number({
            description: 'Number of critical parameter differences (e.g., nozzle diameter, printer model)'
          }),
          compatibilityScore: Type.Number({
            minimum: 0,
            maximum: 100,
            description: 'Compatibility percentage (0-100)'
          })
        }))
      }, {
        description: 'Comparison results between file parameters and selected printer profile'
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
      })),
      // Profile comparison parameters
      compareWithProfile: Type.Optional(Type.String({
        description: 'ID of printer profile to compare file parameters against'
      })),
      includeComparison: Type.Optional(Type.Boolean({
        description: 'Whether to include profile comparison in the response (default: false)'
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
