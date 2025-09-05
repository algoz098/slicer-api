// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { HealthcheckService } from './healthcheck.class'

// Main data model schema
// Healthcheck result schema — read-only, used for monitoring
export const healthcheckSchema = Type.Object(
  {
    status: Type.Union([Type.Literal('ok'), Type.Literal('error')]),
    uptime: Type.Number(),
    timestamp: Type.String(),
    pid: Type.Number(),
    version: Type.Union([Type.String(), Type.Null()])
  },
  { $id: 'Healthcheck', additionalProperties: false }
)

export type Healthcheck = Static<typeof healthcheckSchema>
export const healthcheckValidator = getValidator(healthcheckSchema, dataValidator)
export const healthcheckResolver = resolve<Healthcheck, HookContext<HealthcheckService>>({})

export const healthcheckExternalResolver = resolve<Healthcheck, HookContext<HealthcheckService>>({})

// No create/patch/update/remove for a healthcheck service — keep data schemas minimal
export const healthcheckDataSchema = Type.Object({}, { $id: 'HealthcheckData', additionalProperties: false })
export type HealthcheckData = Static<typeof healthcheckDataSchema>
export const healthcheckDataValidator = getValidator(healthcheckDataSchema, dataValidator)
export const healthcheckDataResolver = resolve<Healthcheck, HookContext<HealthcheckService>>({})

export const healthcheckPatchSchema = Type.Object({}, { $id: 'HealthcheckPatch', additionalProperties: false })
export type HealthcheckPatch = Static<typeof healthcheckPatchSchema>
export const healthcheckPatchValidator = getValidator(healthcheckPatchSchema, dataValidator)
export const healthcheckPatchResolver = resolve<Healthcheck, HookContext<HealthcheckService>>({})

// Query properties — we don't allow filtering on mutable fields here
export const healthcheckQueryProperties = Type.Object({}, { additionalProperties: false })
export const healthcheckQuerySchema = Type.Intersect(
  [querySyntax(healthcheckQueryProperties), Type.Object({}, { additionalProperties: false })],
  { additionalProperties: false }
)

export type HealthcheckQuery = Static<typeof healthcheckQuerySchema>
export const healthcheckQueryValidator = getValidator(healthcheckQuerySchema, queryValidator)
export const healthcheckQueryResolver = resolve<HealthcheckQuery, HookContext<HealthcheckService>>({})
