// // For more information about this file see https://dove.feathersjs.com/guides/cli/service.schemas.html
import { resolve } from '@feathersjs/schema'
import { Type, getValidator, querySyntax } from '@feathersjs/typebox'
import type { Static } from '@feathersjs/typebox'

import type { HookContext } from '../../../declarations'
import { dataValidator, queryValidator } from '../../../validators'
import type { Slicer3MfService } from './3mf.class'

// Main result model schema (response)
export const slicer3MfSchema = Type.Object(
  {
    id: Type.String(),
    filename: Type.Optional(Type.String()),
    outputPath: Type.String(),
    contentType: Type.Optional(Type.String()),
    size: Type.Optional(Type.Number({ minimum: 0 })),
    dataBase64: Type.Optional(Type.String())
  },
  { $id: 'Slicer3Mf', additionalProperties: false }
)
export type Slicer3Mf = Static<typeof slicer3MfSchema>
export const slicer3MfValidator = getValidator(slicer3MfSchema, dataValidator)
export const slicer3MfResolver = resolve<Slicer3Mf, HookContext<Slicer3MfService>>({})

export const slicer3MfExternalResolver = resolve<Slicer3Mf, HookContext<Slicer3MfService>>({})

// Schema for creating new entries (request)
export const slicer3MfDataSchema = Type.Object(
  {
    // Opcional: caminho do arquivo de entrada caso não use multipart
    filePath: Type.Optional(Type.String()),
    // Opcional: nome do campo multipart (se aplicável). Padrão: "file"
    field: Type.Optional(Type.String()),
    // Opcional: plate quando input tiver múltiplas placas
    plate: Type.Optional(Type.Number({ minimum: 1 })),
    // Opcional: nomes de perfis do Orca (impressora/filamento/processo)
    printerProfile: Type.Optional(Type.String()),
    filamentProfile: Type.Optional(Type.String()),
    processProfile: Type.Optional(Type.String()),
    // Opcional: caminho de saída; recomenda-se terminar com .gcode.3mf
    output: Type.Optional(Type.String())
  },
  { $id: 'Slicer3MfData', additionalProperties: false }
)
export type Slicer3MfData = Static<typeof slicer3MfDataSchema>
export const slicer3MfDataValidator = getValidator(slicer3MfDataSchema, dataValidator)
export const slicer3MfDataResolver = resolve<Slicer3Mf, HookContext<Slicer3MfService>>({})

// Schema for updating existing entries (não usado neste serviço)
export const slicer3MfPatchSchema = Type.Partial(slicer3MfSchema, {
  $id: 'Slicer3MfPatch'
})
export type Slicer3MfPatch = Static<typeof slicer3MfPatchSchema>
export const slicer3MfPatchValidator = getValidator(slicer3MfPatchSchema, dataValidator)
export const slicer3MfPatchResolver = resolve<Slicer3Mf, HookContext<Slicer3MfService>>({})

// Schema for allowed query properties (nenhuma por enquanto)
export const slicer3MfQueryProperties = Type.Object({})
export const slicer3MfQuerySchema = Type.Intersect(
  [
    querySyntax(slicer3MfQueryProperties),
    // Add additional query properties here
    Type.Object({}, { additionalProperties: false })
  ],
  { additionalProperties: false }
)
export type Slicer3MfQuery = Static<typeof slicer3MfQuerySchema>
export const slicer3MfQueryValidator = getValidator(slicer3MfQuerySchema, queryValidator)
export const slicer3MfQueryResolver = resolve<Slicer3MfQuery, HookContext<Slicer3MfService>>({})
