// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../declarations'
import { dataValidator, queryValidator } from '../../validators'
import type { MediasService } from './medias.class'

// Main data model schema
export const mediasSchema = Type.Object({}, { $id: 'Medias', additionalProperties: false })
export type Medias = Static<typeof mediasSchema>
export const mediasValidator = getValidator(mediasSchema, dataValidator)
export const mediasResolver = resolve<Medias, HookContext<MediasService>>({})

export const mediasExternalResolver = resolve<Medias, HookContext<MediasService>>({})

// Schema for creating new entries
export const mediasDataSchema = Type.Pick(mediasSchema, [], {
  $id: 'MediasData'
})
export type MediasData = Static<typeof mediasDataSchema>
export const mediasDataValidator = getValidator(mediasDataSchema, dataValidator)
export const mediasDataResolver = resolve<Medias, HookContext<MediasService>>({})

// Schema for updating existing entries
export const mediasPatchSchema = Type.Partial(mediasSchema, {
  $id: 'MediasPatch'
})
export type MediasPatch = Static<typeof mediasPatchSchema>
export const mediasPatchValidator = getValidator(mediasPatchSchema, dataValidator)
export const mediasPatchResolver = resolve<Medias, HookContext<MediasService>>({})

// Schema for allowed query properties
export const mediasQueryProperties = Type.Pick(mediasSchema, [])
export const mediasQuerySchema = Type.Intersect(
  [
    querySyntax(mediasQueryProperties),
    // Add additional query properties here
    Type.Object(
      {
        path: Type.Optional(Type.Any())
      },
      { additionalProperties: false }
    )
  ],
  { additionalProperties: false }
)
export type MediasQuery = Static<typeof mediasQuerySchema>
export const mediasQueryValidator = getValidator(mediasQuerySchema, queryValidator)
export const mediasQueryResolver = resolve<MediasQuery, HookContext<MediasService>>({})
