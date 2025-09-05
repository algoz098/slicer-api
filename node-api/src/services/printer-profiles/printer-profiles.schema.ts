
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { PrinterProfilesService } from './printer-profiles.class'


// Schema for profile file content
const profileEntrySchema = Type.Object({
  name: Type.String(),
  sub_path: Type.String()
})

// The profile files in `config/orcaslicer/.../profiles` are heterogeneous.
// Make the schema permissive but explicit about common fields:
// - `version` may be a string or number in different packages.
// - `force_update` is commonly a string like "0" but sometimes numeric/boolean.
// - add commonly-present optional metadata: url, author, license, tags.
// - keep the various lists optional and allow arbitrary additional properties
//   because many packages include vendor-specific keys.
const profileFileContentSchema = Type.Object(
  {
    name: Type.String(),
    // version can be string (e.g. "02.03.00.05") or a number
    version: Type.Optional(Type.Union([Type.String(), Type.Number()])),
    // force_update may be expressed as string/number/boolean
    force_update: Type.Optional(Type.Union([Type.String(), Type.Number(), Type.Boolean()])),
    description: Type.Optional(Type.String()),
    url: Type.Optional(Type.String()),
    author: Type.Optional(Type.String()),
    license: Type.Optional(Type.String()),
    tags: Type.Optional(Type.Array(Type.String())),
    // Allow an open metadata object for vendor-specific values
    metadata: Type.Optional(Type.Object({}, { additionalProperties: true })),
    machine_model_list: Type.Optional(Type.Array(profileEntrySchema)),
    process_list: Type.Optional(Type.Array(profileEntrySchema)),
    filament_list: Type.Optional(Type.Array(profileEntrySchema)),
    machine_list: Type.Optional(Type.Array(profileEntrySchema))
  },
  { additionalProperties: true }
)
// Main data model schema
export const printerProfilesSchema = Type.Object(
  {
    id: Type.String(),
    text: Type.String(),
    fileContent: profileFileContentSchema
  },
  { $id: 'PrinterProfiles', additionalProperties: false }
)
export type PrinterProfiles = Static<typeof printerProfilesSchema>
export const printerProfilesValidator = getValidator(printerProfilesSchema, dataValidator)
export const printerProfilesResolver = resolve<PrinterProfiles, HookContext<PrinterProfilesService>>({})

export const printerProfilesExternalResolver = resolve<PrinterProfiles, HookContext<PrinterProfilesService>>(
  {}
)

// Schema for creating new entries
export const printerProfilesDataSchema = Type.Pick(printerProfilesSchema, ['text'], {
  $id: 'PrinterProfilesData'
})
export type PrinterProfilesData = Static<typeof printerProfilesDataSchema>
export const printerProfilesDataValidator = getValidator(printerProfilesDataSchema, dataValidator)
export const printerProfilesDataResolver = resolve<PrinterProfiles, HookContext<PrinterProfilesService>>({})

// Schema for updating existing entries
export const printerProfilesPatchSchema = Type.Partial(printerProfilesSchema, {
  $id: 'PrinterProfilesPatch'
})
export type PrinterProfilesPatch = Static<typeof printerProfilesPatchSchema>
export const printerProfilesPatchValidator = getValidator(printerProfilesPatchSchema, dataValidator)
export const printerProfilesPatchResolver = resolve<PrinterProfiles, HookContext<PrinterProfilesService>>({})

// Schema for allowed query properties
export const printerProfilesQueryProperties = Type.Pick(printerProfilesSchema, ['id', 'text'])
export const printerProfilesQuerySchema = Type.Intersect(
  [
    querySyntax(printerProfilesQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type PrinterProfilesQuery = Static<typeof printerProfilesQuerySchema>
export const printerProfilesQueryValidator = getValidator(printerProfilesQuerySchema, queryValidator)
export const printerProfilesQueryResolver = resolve<
  PrinterProfilesQuery,
  HookContext<PrinterProfilesService>
>({})
