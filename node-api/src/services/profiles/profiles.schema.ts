// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { ProfilesService } from './profiles.class'

// Response schema for a single profile with resolved config
export const profilesSchema = Type.Object(
  {
    name: Type.String(),
    type: Type.Union([Type.Literal('machine'), Type.Literal('filament'), Type.Literal('process')]),
    vendor: Type.String(),
    inherits: Type.Optional(Type.String()),
    config: Type.Record(Type.String(), Type.Any())
  },
  { $id: 'Profiles', additionalProperties: false }
)
export type Profiles = Static<typeof profilesSchema>
export const profilesValidator = getValidator(profilesSchema, dataValidator)
export const profilesResolver = resolve<Profiles, HookContext<ProfilesService>>({})

export const profilesExternalResolver = resolve<Profiles, HookContext<ProfilesService>>({})

// Data schema (not used for this read-only service)
export const profilesDataSchema = Type.Object({}, { $id: 'ProfilesData', additionalProperties: false })
export type ProfilesData = Static<typeof profilesDataSchema>
export const profilesDataValidator = getValidator(profilesDataSchema, dataValidator)
export const profilesDataResolver = resolve<Profiles, HookContext<ProfilesService>>({})

// Query schema
export const profilesQueryProperties = Type.Object({
  type: Type.Optional(
    Type.Union([Type.Literal('machine'), Type.Literal('filament'), Type.Literal('process')])
  ),
  vendor: Type.Optional(Type.String())
})
export const profilesQuerySchema = Type.Intersect(
  [querySyntax(profilesQueryProperties), Type.Object({}, { additionalProperties: false })],
  { additionalProperties: false }
)
export type ProfilesQuery = Static<typeof profilesQuerySchema>
export const profilesQueryValidator = getValidator(profilesQuerySchema, queryValidator)
export const profilesQueryResolver = resolve<ProfilesQuery, HookContext<ProfilesService>>({})
