// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { PlatesCountService } from './count.class'

// Main data model schema
export const platesCountSchema = Type.Object(
  {
    count: Type.Number(),
    fileName: Type.String()
  },
  { $id: 'PlatesCount', additionalProperties: false }
)
export type PlatesCount = Static<typeof platesCountSchema>
export const platesCountValidator = getValidator(platesCountSchema, dataValidator)
export const platesCountResolver = resolve<PlatesCount, HookContext<PlatesCountService>>({})

export const platesCountExternalResolver = resolve<PlatesCount, HookContext<PlatesCountService>>({})

// Schema for creating new entries
export const platesCountDataSchema = Type.Object(
  {
    file: Type.Optional(Type.Any()) // File upload - optional to avoid validation issues
  },
  { $id: 'PlatesCountData' }
)
export type PlatesCountData = Static<typeof platesCountDataSchema>
export const platesCountDataValidator = getValidator(platesCountDataSchema, dataValidator)
export const platesCountDataResolver = resolve<PlatesCount, HookContext<PlatesCountService>>({})

// Schema for updating existing entries
export const platesCountPatchSchema = Type.Partial(platesCountSchema, {
  $id: 'PlatesCountPatch'
})
export type PlatesCountPatch = Static<typeof platesCountPatchSchema>
export const platesCountPatchValidator = getValidator(platesCountPatchSchema, dataValidator)
export const platesCountPatchResolver = resolve<PlatesCount, HookContext<PlatesCountService>>({})

// Schema for allowed query properties
export const platesCountQueryProperties = Type.Pick(platesCountSchema, ['count', 'fileName'])
export const platesCountQuerySchema = Type.Intersect(
  [
    querySyntax(platesCountQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type PlatesCountQuery = Static<typeof platesCountQuerySchema>
export const platesCountQueryValidator = getValidator(platesCountQuerySchema, queryValidator)
export const platesCountQueryResolver = resolve<PlatesCountQuery, HookContext<PlatesCountService>>({})
