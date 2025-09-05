// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { ValidationService } from './validation.class'

// Validation result schema
export const validationSchema = Type.Object(
  {
    id: Type.String({
      description: 'Unique identifier for the validation request'
    }),
    type: Type.Union([
      Type.Literal('file'),
      Type.Literal('string'),
      Type.Literal('path'),
      Type.Literal('nozzle'),
      Type.Literal('printer'),
      Type.Literal('technical-name')
    ], {
      description: 'Type of validation performed'
    }),
    isValid: Type.Boolean({
      description: 'Whether the validation passed'
    }),
    errors: Type.Array(Type.String(), {
      description: 'List of validation errors'
    }),
    warnings: Type.Optional(Type.Array(Type.String(), {
      description: 'List of validation warnings'
    })),
    sanitizedValue: Type.Optional(Type.Any({
      description: 'Sanitized/processed value if applicable'
    })),
    originalValue: Type.Optional(Type.Any({
      description: 'Original value that was validated'
    })),
    metadata: Type.Optional(Type.Object({}, {
      additionalProperties: true,
      description: 'Additional validation metadata'
    })),
    validatedAt: Type.String({
      format: 'date-time',
      description: 'When the validation was performed'
    }),
    // File-specific fields
    sanitizedFilename: Type.Optional(Type.String({
      description: 'Sanitized filename for file validations'
    })),
    tempPath: Type.Optional(Type.String({
      description: 'Temporary file path for file validations'
    })),
    detectedType: Type.Optional(Type.String({
      description: 'Detected file type for file validations'
    }))
  },
  { $id: 'Validation', additionalProperties: false }
)

export type Validation = Static<typeof validationSchema>
export const validationValidator = getValidator(validationSchema, dataValidator)
export const validationResolver = resolve<Validation, HookContext<ValidationService>>({})

export const validationExternalResolver = resolve<Validation, HookContext<ValidationService>>({})

// Schema for creating new validations
export const validationDataSchema = Type.Object({
  type: Type.Union([
    Type.Literal('file'),
    Type.Literal('string'),
    Type.Literal('path'),
    Type.Literal('nozzle'),
    Type.Literal('printer'),
    Type.Literal('technical-name')
  ], {
    description: 'Type of validation to perform'
  }),
  data: Type.Any({
    description: 'Data to validate (file object, string, etc.)'
  }),
  options: Type.Optional(Type.Object({
    // File validation options
    maxFileSize: Type.Optional(Type.Number()),
    allowedExtensions: Type.Optional(Type.Array(Type.String())),
    allowedMimeTypes: Type.Optional(Type.Array(Type.String())),
    deepValidation: Type.Optional(Type.Boolean()),
    
    // String validation options
    minLength: Type.Optional(Type.Number()),
    maxLength: Type.Optional(Type.Number()),
    pattern: Type.Optional(Type.String()),
    allowEmpty: Type.Optional(Type.Boolean()),
    trim: Type.Optional(Type.Boolean()),
    
    // Path validation options
    allowAbsolute: Type.Optional(Type.Boolean()),
    allowRelative: Type.Optional(Type.Boolean()),
    
    // Custom validation options
    customRules: Type.Optional(Type.Array(Type.String()))
  }, {
    additionalProperties: true,
    description: 'Validation options specific to the validation type'
  }))
}, {
  $id: 'ValidationData',
  description: 'Data schema for validation requests'
})

export type ValidationData = Static<typeof validationDataSchema>
export const validationDataValidator = getValidator(validationDataSchema, dataValidator)
export const validationDataResolver = resolve<Validation, HookContext<ValidationService>>({})

// Schema for updating existing validations (limited use case)
export const validationPatchSchema = Type.Partial(
  Type.Pick(validationSchema, ['metadata']),
  {
    $id: 'ValidationPatch'
  }
)

export type ValidationPatch = Static<typeof validationPatchSchema>
export const validationPatchValidator = getValidator(validationPatchSchema, dataValidator)
export const validationPatchResolver = resolve<Validation, HookContext<ValidationService>>({})

// Schema for allowed query properties
export const validationQueryProperties = Type.Pick(validationSchema, [
  'type', 
  'isValid', 
  'validatedAt'
])

export const validationQuerySchema = Type.Intersect(
  [
    querySyntax(validationQueryProperties),
    // Add additional query properties
    Type.Object({
      // Support for filtering by validation date range
      validatedAfter: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter validations after this date'
      })),
      validatedBefore: Type.Optional(Type.String({
        format: 'date-time',
        description: 'Filter validations before this date'
      })),
      // Support for filtering by validation results
      hasErrors: Type.Optional(Type.Boolean({
        description: 'Filter by presence of errors'
      })),
      hasWarnings: Type.Optional(Type.Boolean({
        description: 'Filter by presence of warnings'
      }))
    }, { additionalProperties: false })
  ],
  { 
    additionalProperties: false,
    description: 'Query parameters for validation results'
  }
)

export type ValidationQuery = Static<typeof validationQuerySchema>
export const validationQueryValidator = getValidator(validationQuerySchema, queryValidator)
export const validationQueryResolver = resolve<ValidationQuery, HookContext<ValidationService>>({})

// Validation rule schema for extensibility
export const validationRuleSchema = Type.Object({
  name: Type.String({
    description: 'Name of the validation rule'
  }),
  description: Type.String({
    description: 'Description of what the rule validates'
  }),
  type: Type.String({
    description: 'Type of data this rule applies to'
  }),
  pattern: Type.Optional(Type.String({
    description: 'Regex pattern for validation'
  })),
  minValue: Type.Optional(Type.Number({
    description: 'Minimum value for numeric validations'
  })),
  maxValue: Type.Optional(Type.Number({
    description: 'Maximum value for numeric validations'
  })),
  required: Type.Optional(Type.Boolean({
    description: 'Whether this validation is required'
  })),
  severity: Type.Union([
    Type.Literal('error'),
    Type.Literal('warning'),
    Type.Literal('info')
  ], {
    description: 'Severity level of validation failure'
  })
}, {
  $id: 'ValidationRule',
  additionalProperties: false
})

export type ValidationRule = Static<typeof validationRuleSchema>
