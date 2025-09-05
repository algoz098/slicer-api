// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../../declarations'
import { dataValidator, queryValidator } from '../../../../validators'
import type { ThreeMFProcessorService } from './3mf.class'

// 3MF processing result schema
export const threeMFProcessorSchema = Type.Object(
  {
    id: Type.String({
      description: 'Unique identifier for the processing request'
    }),
    uploadId: Type.String({
      description: 'ID of the upload that was processed'
    }),
    filePath: Type.String({
      description: 'Path to the 3MF file that was processed'
    }),
    status: Type.Union([
      Type.Literal('processing'),
      Type.Literal('completed'),
      Type.Literal('failed'),
      Type.Literal('cancelled')
    ], {
      description: 'Processing status'
    }),
    extractedData: Type.Optional(Type.Object({
      printer: Type.Optional(Type.String({
        description: 'Printer model or manufacturer name'
      })),
      nozzle: Type.Optional(Type.String({
        description: 'Nozzle diameter in mm'
      })),
      profile: Type.Optional(Type.String({
        description: 'Print profile name'
      })),
      technicalName: Type.Optional(Type.String({
        description: 'Technical profile name from slicer software'
      })),
      metadata: Type.Optional(Type.Object({}, {
        additionalProperties: true,
        description: 'Additional metadata extracted from 3MF file'
      }))
    }, {
      description: 'Data extracted from the 3MF file'
    })),
    processingTime: Type.Optional(Type.Number({
      description: 'Processing time in milliseconds'
    })),
    errors: Type.Array(Type.String(), {
      description: 'List of processing errors'
    }),
    warnings: Type.Array(Type.String(), {
      description: 'List of processing warnings'
    }),
    processedAt: Type.String({
      format: 'date-time',
      description: 'When the processing was completed'
    }),
    candidateFiles: Type.Optional(Type.Array(Type.String(), {
      description: 'List of candidate metadata files found in the 3MF archive'
    })),
    archiveInfo: Type.Optional(Type.Object({
      totalFiles: Type.Number({
        description: 'Total number of files in the archive'
      }),
      archiveSize: Type.Number({
        description: 'Size of the archive in bytes'
      }),
      hasModel: Type.Boolean({
        description: 'Whether the archive contains 3D model files'
      }),
      hasMetadata: Type.Boolean({
        description: 'Whether the archive contains metadata files'
      })
    }, {
      description: 'Information about the 3MF archive structure'
    }))
  },
  { $id: 'ThreeMFProcessor', additionalProperties: false }
)

export type ThreeMFProcessor = Static<typeof threeMFProcessorSchema>
export const threeMFProcessorValidator = getValidator(threeMFProcessorSchema, dataValidator)
export const threeMFProcessorResolver = resolve<ThreeMFProcessor, HookContext<ThreeMFProcessorService>>({})

export const threeMFProcessorExternalResolver = resolve<ThreeMFProcessor, HookContext<ThreeMFProcessorService>>({})

// Schema for creating new processing requests
export const threeMFProcessorDataSchema = Type.Object({
  uploadId: Type.String({
    description: 'ID of the upload to process'
  }),
  options: Type.Optional(Type.Object({
    candidateFiles: Type.Optional(Type.Array(Type.String(), {
      description: 'Custom list of candidate metadata files to search'
    })),
    extractionPatterns: Type.Optional(Type.Array(Type.String(), {
      description: 'Custom regex patterns for data extraction'
    })),
    timeout: Type.Optional(Type.Number({
      minimum: 1000,
      maximum: 300000,
      description: 'Processing timeout in milliseconds (1s to 5min)'
    })),
    includeArchiveInfo: Type.Optional(Type.Boolean({
      description: 'Whether to include detailed archive information'
    })),
    validateStructure: Type.Optional(Type.Boolean({
      description: 'Whether to validate 3MF file structure'
    }))
  }, {
    additionalProperties: false,
    description: 'Processing options'
  }))
}, {
  $id: 'ThreeMFProcessorData',
  description: 'Data schema for 3MF processing requests'
})

export type ThreeMFProcessorData = Static<typeof threeMFProcessorDataSchema>
export const threeMFProcessorDataValidator = getValidator(threeMFProcessorDataSchema, dataValidator)
export const threeMFProcessorDataResolver = resolve<ThreeMFProcessor, HookContext<ThreeMFProcessorService>>({})

// Schema for updating existing processing requests
export const threeMFProcessorPatchSchema = Type.Partial(
  Type.Pick(threeMFProcessorSchema, ['status']),
  {
    $id: 'ThreeMFProcessorPatch'
  }
)

export type ThreeMFProcessorPatch = Static<typeof threeMFProcessorPatchSchema>
export const threeMFProcessorPatchValidator = getValidator(threeMFProcessorPatchSchema, dataValidator)
export const threeMFProcessorPatchResolver = resolve<ThreeMFProcessor, HookContext<ThreeMFProcessorService>>({})

// Schema for allowed query properties
export const threeMFProcessorQueryProperties = Type.Pick(threeMFProcessorSchema, [
  'uploadId', 
  'status', 
  'processedAt'
])

export const threeMFProcessorQuerySchema = Type.Intersect(
  [
    querySyntax(threeMFProcessorQueryProperties),
    // Add additional query properties
    Type.Object({
      // Support for filtering by processing date range
      processedAfter: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter processing results after this date'
      })),
      processedBefore: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter processing results before this date'
      })),
      // Support for filtering by results
      hasErrors: Type.Optional(Type.Boolean({
        description: 'Filter by presence of errors'
      })),
      hasWarnings: Type.Optional(Type.Boolean({
        description: 'Filter by presence of warnings'
      })),
      hasExtractedData: Type.Optional(Type.Boolean({
        description: 'Filter by presence of extracted data'
      }))
    }, { additionalProperties: false })
  ],
  { 
    additionalProperties: false,
    description: 'Query parameters for 3MF processing results'
  }
)

export type ThreeMFProcessorQuery = Static<typeof threeMFProcessorQuerySchema>
export const threeMFProcessorQueryValidator = getValidator(threeMFProcessorQuerySchema, queryValidator)
export const threeMFProcessorQueryResolver = resolve<ThreeMFProcessorQuery, HookContext<ThreeMFProcessorService>>({})
