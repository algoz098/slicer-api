// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { SlicerStlService } from './stl.class'

// Main result model schema (response)
export const slicerStlSchema = Type.Object(
  {
    id: Type.String(),
    filename: Type.Optional(Type.String()),
    outputPath: Type.String(),
    gcode: Type.String()
  },
  { $id: 'SlicerStl', additionalProperties: false }
)
export type SlicerStl = Static<typeof slicerStlSchema>
export const slicerStlValidator = getValidator(slicerStlSchema, dataValidator)
export const slicerStlResolver = resolve<SlicerStl, HookContext<SlicerStlService>>({})

export const slicerStlExternalResolver = resolve<SlicerStl, HookContext<SlicerStlService>>({})

// Schema for creating new entries (request)
export const slicerStlDataSchema = Type.Object(
  {
    // Opcional: caminho do arquivo de entrada caso não use multipart
    filePath: Type.Optional(Type.String()),
    // Opcional: nome do campo multipart (se aplicável). Padrão: "file"
    field: Type.Optional(Type.String()),
    // Opcional: plate quando input for .3mf
    plate: Type.Optional(Type.Number({ minimum: 1 })),
    // Opcional: nomes de perfis do Orca (impressora/filamento/processo)
    printerProfile: Type.Optional(Type.String()),
    filamentProfile: Type.Optional(Type.String()),
    processProfile: Type.Optional(Type.String()),
    // Opcional: caminho de saída para salvar o G-code
    output: Type.Optional(Type.String())
  },
  { $id: 'SlicerStlData', additionalProperties: false }
)
export type SlicerStlData = Static<typeof slicerStlDataSchema>
export const slicerStlDataValidator = getValidator(slicerStlDataSchema, dataValidator)
export const slicerStlDataResolver = resolve<SlicerStl, HookContext<SlicerStlService>>({})

// Schema for updating existing entries (não usado neste serviço)
export const slicerStlPatchSchema = Type.Partial(slicerStlSchema, {
  $id: 'SlicerStlPatch'
})
export type SlicerStlPatch = Static<typeof slicerStlPatchSchema>
export const slicerStlPatchValidator = getValidator(slicerStlPatchSchema, dataValidator)
export const slicerStlPatchResolver = resolve<SlicerStl, HookContext<SlicerStlService>>({})

// Schema for allowed query properties (nenhuma por enquanto)
export const slicerStlQueryProperties = Type.Object({})
export const slicerStlQuerySchema = Type.Intersect(
  [
    querySyntax(slicerStlQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type SlicerStlQuery = Static<typeof slicerStlQuerySchema>
export const slicerStlQueryValidator = getValidator(slicerStlQuerySchema, queryValidator)
export const slicerStlQueryResolver = resolve<SlicerStlQuery, HookContext<SlicerStlService>>({})
