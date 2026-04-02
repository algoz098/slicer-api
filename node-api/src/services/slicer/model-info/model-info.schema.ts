// For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { SlicerModelInfoService } from './model-info.class'

// Main result model schema (response)
export const slicerModelInfoSchema = Type.Object(
  {
    id: Type.String(),
    filename: Type.Optional(Type.String()),
    objectCount: Type.Number(),
    triangleCount: Type.Number(),
    volume: Type.Number(),
    boundingBox: Type.String(), // raw string from addon, e.g. "256 x 256 x 100"
    isValid: Type.Boolean()
  },
  { $id: 'SlicerModelInfo', additionalProperties: false }
)
export type SlicerModelInfo = Static<typeof slicerModelInfoSchema>
export const slicerModelInfoValidator = getValidator(slicerModelInfoSchema, dataValidator)
export const slicerModelInfoResolver = resolve<SlicerModelInfo, HookContext<SlicerModelInfoService>>({})

export const slicerModelInfoExternalResolver = resolve<SlicerModelInfo, HookContext<SlicerModelInfoService>>(
  {}
)

// Schema for creating new entries (request)
export const slicerModelInfoDataSchema = Type.Object(
  {
    // Opcional: caminho do arquivo de entrada caso não use multipart
    filePath: Type.Optional(Type.String()),
    // Opcional: nome do campo multipart (se aplicável). Padrão: "file"
    field: Type.Optional(Type.String())
  },
  { $id: 'SlicerModelInfoData', additionalProperties: false }
)
export type SlicerModelInfoData = Static<typeof slicerModelInfoDataSchema>
export const slicerModelInfoDataValidator = getValidator(slicerModelInfoDataSchema, dataValidator)
export const slicerModelInfoDataResolver = resolve<SlicerModelInfo, HookContext<SlicerModelInfoService>>({})

// Schema for updating existing entries (não usado neste serviço)
export const slicerModelInfoPatchSchema = Type.Partial(slicerModelInfoSchema, {
  $id: 'SlicerModelInfoPatch'
})
export type SlicerModelInfoPatch = Static<typeof slicerModelInfoPatchSchema>
export const slicerModelInfoPatchValidator = getValidator(slicerModelInfoPatchSchema, dataValidator)
export const slicerModelInfoPatchResolver = resolve<SlicerModelInfo, HookContext<SlicerModelInfoService>>({})

// Schema for allowed query properties (nenhuma por enquanto)
export const slicerModelInfoQueryProperties = Type.Object({})
export const slicerModelInfoQuerySchema = Type.Intersect(
  [querySyntax(slicerModelInfoQueryProperties), Type.Object({}, { additionalProperties: false })],
  { additionalProperties: false }
)
export type SlicerModelInfoQuery = Static<typeof slicerModelInfoQuerySchema>
export const slicerModelInfoQueryValidator = getValidator(slicerModelInfoQuerySchema, queryValidator)
export const slicerModelInfoQueryResolver = resolve<
  SlicerModelInfoQuery,
  HookContext<SlicerModelInfoService>
>({})
