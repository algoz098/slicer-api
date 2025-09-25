// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { ProfileConverterService } from './profile-converter.class'

// Response schema: a JSON object of overrides compatible with slicer `options`
export const profileConverterSchema = Type.Object(
  {
    options: Type.Record(
      Type.String(),
      Type.Union([Type.String(), Type.Number(), Type.Boolean()])
    )
  },
  { $id: 'ProfileConverter', additionalProperties: false }
)
export type ProfileConverter = Static<typeof profileConverterSchema>
export const profileConverterValidator = getValidator(profileConverterSchema, dataValidator)
export const profileConverterResolver = resolve<ProfileConverter, HookContext<ProfileConverterService>>({})

export const profileConverterExternalResolver = resolve<
  ProfileConverter,
  HookContext<ProfileConverterService>
>({})

// Request schema: receives a `type` (printer/filament/process) and the exported profile `data`
// `data` is optional when a multipart file is provided.
export const profileConverterDataSchema = Type.Object(
  {
    type: Type.Union([
      Type.Literal('printer'),
      Type.Literal('filament'),
      Type.Literal('process')
    ]),
    // Accepts: JSON string, base64 string, raw text, filesystem path, or already-parsed object
    data: Type.Optional(
      Type.Union([
        Type.String(),
        Type.Record(Type.String(), Type.Any())
      ])
    )
  },
  { $id: 'ProfileConverterData', additionalProperties: false }
)
export type ProfileConverterData = Static<typeof profileConverterDataSchema>
export const profileConverterDataValidator = getValidator(profileConverterDataSchema, dataValidator)
export const profileConverterDataResolver = resolve<ProfileConverter, HookContext<ProfileConverterService>>(
  {}
)

// Schema for updating existing entries (not used here)
export const profileConverterPatchSchema = Type.Partial(profileConverterSchema, {
  $id: 'ProfileConverterPatch'
})
export type ProfileConverterPatch = Static<typeof profileConverterPatchSchema>
export const profileConverterPatchValidator = getValidator(profileConverterPatchSchema, dataValidator)
export const profileConverterPatchResolver = resolve<ProfileConverter, HookContext<ProfileConverterService>>(
  {}
)

// Query: none for now
export const profileConverterQueryProperties = Type.Object({})
export const profileConverterQuerySchema = Type.Intersect(
  [
    querySyntax(profileConverterQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type ProfileConverterQuery = Static<typeof profileConverterQuerySchema>
export const profileConverterQueryValidator = getValidator(profileConverterQuerySchema, queryValidator)
export const profileConverterQueryResolver = resolve<
  ProfileConverterQuery,
  HookContext<ProfileConverterService>
>({})
