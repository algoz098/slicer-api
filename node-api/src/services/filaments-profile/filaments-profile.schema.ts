// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { FilamentsProfileService } from './filaments-profile.class'

// Schema for profile file content (reuses structure similar to printer profiles)
const profileEntrySchema = Type.Object({
  name: Type.String(),
  sub_path: Type.String()
})

const profileFileContentSchema = Type.Object(
  {
    name: Type.String(),
    version: Type.Optional(Type.Union([Type.String(), Type.Number()])),
    force_update: Type.Optional(Type.Union([Type.String(), Type.Number(), Type.Boolean()])),
    description: Type.Optional(Type.String()),
    url: Type.Optional(Type.String()),
    author: Type.Optional(Type.String()),
    license: Type.Optional(Type.String()),
    tags: Type.Optional(Type.Array(Type.String())),
    metadata: Type.Optional(Type.Object({}, { additionalProperties: true })),
    machine_model_list: Type.Optional(Type.Array(profileEntrySchema)),
    process_list: Type.Optional(Type.Array(profileEntrySchema)),
    filament_list: Type.Optional(Type.Array(profileEntrySchema)),
    machine_list: Type.Optional(Type.Array(profileEntrySchema))
  },
  { additionalProperties: true }
)

// Main data model schema
export const filamentsProfileSchema = Type.Object(
  {
    id: Type.String(),
    text: Type.String(),
    // Human-friendly name as shown in OrcaSlicer, derived from filename
    displayName: Type.Optional(Type.String()),
    fileContent: profileFileContentSchema
  },
  { $id: 'FilamentsProfile', additionalProperties: false }
)
export type FilamentsProfile = Static<typeof filamentsProfileSchema>
export const filamentsProfileValidator = getValidator(filamentsProfileSchema, dataValidator)
export const filamentsProfileResolver = resolve<FilamentsProfile, HookContext<FilamentsProfileService>>({})

export const filamentsProfileExternalResolver = resolve<
  FilamentsProfile,
  HookContext<FilamentsProfileService>
>({})

// Schema for creating new entries
export const filamentsProfileDataSchema = Type.Pick(filamentsProfileSchema, ['text'], {
  $id: 'FilamentsProfileData'
})
export type FilamentsProfileData = Static<typeof filamentsProfileDataSchema>
export const filamentsProfileDataValidator = getValidator(filamentsProfileDataSchema, dataValidator)
export const filamentsProfileDataResolver = resolve<FilamentsProfile, HookContext<FilamentsProfileService>>(
  {}
)

// Schema for updating existing entries
export const filamentsProfilePatchSchema = Type.Partial(filamentsProfileSchema, {
  $id: 'FilamentsProfilePatch'
})
export type FilamentsProfilePatch = Static<typeof filamentsProfilePatchSchema>
export const filamentsProfilePatchValidator = getValidator(filamentsProfilePatchSchema, dataValidator)
export const filamentsProfilePatchResolver = resolve<FilamentsProfile, HookContext<FilamentsProfileService>>(
  {}
)

// Schema for allowed query properties
export const filamentsProfileQueryProperties = Type.Pick(filamentsProfileSchema, ['id', 'text'])
export const filamentsProfileQuerySchema = Type.Intersect(
  [
    querySyntax(filamentsProfileQueryProperties),
    // Add additional query properties here
    Type.Object({
      raw: Type.Optional(Type.Union([Type.String(), Type.Boolean()]))
    }, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type FilamentsProfileQuery = Static<typeof filamentsProfileQuerySchema>
export const filamentsProfileQueryValidator = getValidator(filamentsProfileQuerySchema, queryValidator)
export const filamentsProfileQueryResolver = resolve<
  FilamentsProfileQuery,
  HookContext<FilamentsProfileService>
>({})
