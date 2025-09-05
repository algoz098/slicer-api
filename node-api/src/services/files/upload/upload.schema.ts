// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { FilesUploadService } from './upload.class'

// Upload result schema
export const filesUploadSchema = Type.Object(
  {
    id: Type.String({
      description: 'Unique identifier for the upload'
    }),
    originalFilename: Type.String({
      description: 'Original filename from client'
    }),
    sanitizedFilename: Type.String({
      description: 'Sanitized filename used for storage'
    }),
    fileSize: Type.Number({
      description: 'File size in bytes'
    }),
    mimeType: Type.Optional(Type.String({
      description: 'MIME type of the uploaded file'
    })),
    fileExtension: Type.String({
      description: 'File extension (e.g., .3mf, .stl)'
    }),
    tempPath: Type.String({
      description: 'Temporary file path for processing'
    }),
    uploadedAt: Type.String({
      format: 'date-time',
      description: 'Upload timestamp'
    }),
    status: Type.Union([
      Type.Literal('uploaded'),
      Type.Literal('processing'),
      Type.Literal('processed'),
      Type.Literal('error'),
      Type.Literal('expired')
    ], {
      description: 'Upload status'
    }),
    metadata: Type.Optional(Type.Object({}, {
      additionalProperties: true,
      description: 'Additional metadata extracted from file'
    })),
    expiresAt: Type.String({
      format: 'date-time',
      description: 'When the temporary file expires'
    })
  },
  { $id: 'FilesUpload', additionalProperties: false }
)

export type FilesUpload = Static<typeof filesUploadSchema>
export const filesUploadValidator = getValidator(filesUploadSchema, dataValidator)
export const filesUploadResolver = resolve<FilesUpload, HookContext<FilesUploadService>>({})

export const filesUploadExternalResolver = resolve<FilesUpload, HookContext<FilesUploadService>>({})

// Schema for creating new uploads
export const filesUploadDataSchema = Type.Object({
  // File upload data comes from multipart form data
  // No direct properties needed here
}, {
  $id: 'FilesUploadData',
  description: 'Data schema for file upload - actual data comes from multipart form'
})

export type FilesUploadData = Static<typeof filesUploadDataSchema>
export const filesUploadDataValidator = getValidator(filesUploadDataSchema, dataValidator)
export const filesUploadDataResolver = resolve<FilesUpload, HookContext<FilesUploadService>>({})

// Schema for updating existing uploads
export const filesUploadPatchSchema = Type.Partial(
  Type.Pick(filesUploadSchema, ['status', 'metadata']),
  {
    $id: 'FilesUploadPatch'
  }
)

export type FilesUploadPatch = Static<typeof filesUploadPatchSchema>
export const filesUploadPatchValidator = getValidator(filesUploadPatchSchema, dataValidator)
export const filesUploadPatchResolver = resolve<FilesUpload, HookContext<FilesUploadService>>({})

// Schema for allowed query properties
export const filesUploadQueryProperties = Type.Pick(filesUploadSchema, [
  'originalFilename', 
  'fileExtension', 
  'status', 
  'mimeType'
])

export const filesUploadQuerySchema = Type.Intersect(
  [
    querySyntax(filesUploadQueryProperties),
    // Add additional query properties
    Type.Object({
      // Support for filtering by upload date range
      uploadedAfter: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter uploads after this date'
      })),
      uploadedBefore: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter uploads before this date'
      })),
      // Support for cleanup operations
      includeExpired: Type.Optional(Type.Boolean({
        description: 'Include expired uploads in results'
      }))
    }, { additionalProperties: false })
  ],
  { 
    additionalProperties: false,
    description: 'Query parameters for file uploads'
  }
)

export type FilesUploadQuery = Static<typeof filesUploadQuerySchema>
export const filesUploadQueryValidator = getValidator(filesUploadQuerySchema, queryValidator)
export const filesUploadQueryResolver = resolve<FilesUploadQuery, HookContext<FilesUploadService>>({})

// Upload options schema for configuration
export const uploadOptionsSchema = Type.Object({
  maxFileSize: Type.Optional(Type.Number({
    minimum: 1,
    description: 'Maximum file size in bytes'
  })),
  allowedExtensions: Type.Optional(Type.Array(Type.String(), {
    description: 'Allowed file extensions'
  })),
  allowedMimeTypes: Type.Optional(Type.Array(Type.String(), {
    description: 'Allowed MIME types'
  })),
  enableDeepValidation: Type.Optional(Type.Boolean({
    description: 'Enable deep content validation'
  })),
  retentionPeriod: Type.Optional(Type.Number({
    minimum: 1,
    description: 'File retention period in milliseconds'
  }))
}, {
  $id: 'UploadOptions',
  additionalProperties: false
})

export type UploadOptions = Static<typeof uploadOptionsSchema>
