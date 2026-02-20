// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'node:fs'
import * as path from 'node:path'
import * as os from 'node:os'
import { randomUUID } from 'node:crypto'
import JSZip from 'jszip'
import { sanitizeBblGcodeTemplates } from './gcode-sanitizer'

import type { Application } from '../../../declarations'
import type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery } from './3mf.schema'
import { BadRequest } from '@feathersjs/errors'

export type { Slicer3Mf, Slicer3MfData, Slicer3MfPatch, Slicer3MfQuery }
export interface Slicer3MfServiceOptions {
  app: Application
}

export interface Slicer3MfParams extends Params<Slicer3MfQuery> { }

export class Slicer3MfService<ServiceParams extends Slicer3MfParams = Slicer3MfParams>
  implements ServiceInterface<Slicer3Mf, Slicer3MfData, ServiceParams, Slicer3MfPatch> {
  constructor(public options: Slicer3MfServiceOptions) { }

  async find(_params?: ServiceParams): Promise<Slicer3Mf[]> {
    return []
  }

  async get(id: Id, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id),
      filename: undefined,
      outputPath: ''
    }
  }

  async create(data: Slicer3MfData, params?: ServiceParams): Promise<Slicer3Mf>
  async create(data: Slicer3MfData[], params?: ServiceParams): Promise<Slicer3Mf[]>
  async create(
    data: Slicer3MfData | Slicer3MfData[],
    params?: ServiceParams
  ): Promise<Slicer3Mf | Slicer3Mf[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    const orca = this.options.app.get('orca')
    if (!orca) {
      throw new Error('OrcaSlicer addon not loaded')
    }

    const { options, printerProfileName, filamentProfileName, processProfileName, center, bedType } =
      data ?? {
        options: {}
      }
    const reqField = data.field ?? 'file'
    const anyParams: any = params ?? {}

    const filesContainer =
      anyParams?.koa?.request?.files ?? anyParams?.files ?? anyParams?.koa?.ctx?.request?.files

    let fileObj: any | undefined
    if (filesContainer) {
      fileObj = filesContainer[reqField] ?? filesContainer.file
      if (Array.isArray(fileObj)) fileObj = fileObj[0]
      if (!fileObj && typeof filesContainer === 'object') {
        const first = Object.values(filesContainer)[0]
        fileObj = Array.isArray(first) ? first[0] : first
      }
    }

    let inputPath: string | undefined = data.filePath
    let originalFilename: string | undefined

    if (fileObj) {
      inputPath = fileObj.filepath || fileObj.path || fileObj.tempFilePath || inputPath
      originalFilename = fileObj.originalFilename || fileObj.name || fileObj.filename || originalFilename
    }

    if (!inputPath) {
      throw new Error('Nenhum arquivo recebido. Envie um multipart field "file" ou informe "filePath".')
    }

    if (!fs.existsSync(inputPath)) {
      throw new BadRequest('Input file not found')
    }

    const inputStats = fs.statSync(inputPath)
    console.log(`[3MF] Input file: ${inputPath}, Size: ${inputStats.size} bytes`)
    if (inputStats.size === 0) {
      console.error('[3MF] Check: Input file is empty!')
    }

    // Força o caminho de saída para ser no diretório temporário para segurança
    // Ignora data.output enviado pelo usuário para evitar Arbitrary File Write
    const outputFilename = `orca-${randomUUID()}.gcode.3mf`
    const outPath = path.join(os.tmpdir(), outputFilename)

    // Validate input file is a valid ZIP/3MF
    try {
      const fileContent = fs.readFileSync(inputPath)
      const zip = await JSZip.loadAsync(fileContent)
      console.log('[3MF] Input file is a valid ZIP. Contents:')
      const files = Object.keys(zip.files)

      for (const f of files) {
        const fileData = zip.files[f]
        // Log size for .model files or config
        if (f.endsWith('.model') || f.endsWith('.config')) {
          const content = await fileData.async('nodebuffer')
          console.log(`  - ${f} (Size: ${content.length} bytes)`)
          if (content.length === 0) {
            console.error(`[3MF] WARNING: Internal file ${f} is empty!`)
          }
        } else {
          console.log(`  - ${f}`)
        }
      }

      if (files.length === 0) {
        console.error('[3MF] Input ZIP is empty!')
      }
    } catch (err: any) {
      console.error('[3MF] Input file is NOT a valid ZIP:', err.message)
    }

    // NOTE: Nao carregamos vendors/profiles aqui.
    // O addon funciona em modo on-the-fly puro:
    // - A configuracao completa e passada via `options` em cada chamada de slice
    // - O addon usa FullPrintConfig::defaults() como fallback
    console.log('[3MF] On-the-fly mode - configuration passed via options')

    let output: string
    let usedOptions: string[] | undefined
    let ignoredOptions: string[] | undefined
    let estimatedTimeSec: number | undefined
    let filamentUsedGrams: number | undefined

    // Mescla options e config, onde config tem precedencia
    // Precedencia: config > options > profiles
    const configOverrides = (data as any).config ?? {}
    const finalOptions = { ...options, ...configOverrides }
    finalOptions.curr_bed_type = bedType ?? 'High Temp Plate'

    // Sanitize BBL-proprietary G-code template variables before passing to OrcaSlicer.
    // BBL profiles reference variables like flush_volumetric_speeds and flush_temperatures
    // that don't exist in this OrcaSlicer fork, and use previous_extruder (which starts at -1)
    // as a vector index. This prevents PlaceholderParser errors during export_gcode.
    sanitizeBblGcodeTemplates(finalOptions)

    // Remove flush_volumes_matrix from overrides if it doesn't match the filament count
    // This prevents "Flush volumes matrix do not match to the correct size" errors
    // The addon will automatically synchronize the matrix based on filament_colour
    if (finalOptions.flush_volumes_matrix) {
      const filamentCount = Array.isArray(finalOptions.filament_colour)
        ? finalOptions.filament_colour.length
        : 1
      const headsCount = Array.isArray(finalOptions.flush_multiplier)
        ? finalOptions.flush_multiplier.length
        : 1
      const expectedSize = filamentCount * filamentCount * headsCount
      const actualSize = Array.isArray(finalOptions.flush_volumes_matrix)
        ? finalOptions.flush_volumes_matrix.length
        : 0
      if (actualSize !== expectedSize) {
        console.log(
          `[3MF] Removing inconsistent flush_volumes_matrix: expected=${expectedSize}, actual=${actualSize}`
        )
        delete finalOptions.flush_volumes_matrix
      }
    }

    // Guarda as chaves de options para validacao posterior
    const optionsKeys = new Set(Object.keys(options ?? {}))

    // Create a safe copy of the input file to ensure access and simple path
    const safeInputPath = path.join(os.tmpdir(), `safe_input_${randomUUID()}.3mf`)
    fs.copyFileSync(inputPath, safeInputPath)
    console.log(`[3MF] Copied input to safe path: ${safeInputPath} (Size: ${fs.statSync(safeInputPath).size})`)

    try {
      // Silencia logs por padrao para evitar spam no terminal
      ; (orca as any).setLoggingSilenced(true)
      let res: any
      try {
        res = await orca.slice({
          input: safeInputPath,
          output: outPath,
          plate: data.plate,
          options: finalOptions,
          center: true,
          autoRealignIfNeeded: true,
          // Display names for profiles in output 3MF (metadata only, does not load any preset)
          printerProfileName: printerProfileName,
          filamentProfileName: filamentProfileName,
          processProfileName: processProfileName,
          transferPrinterCustomizations: data.transferPrinterCustomizations ?? true,
          transferFilamentCustomizations: data.transferFilamentCustomizations ?? true,
          transferProcessCustomizations: data.transferProcessCustomizations ?? true,
          transferProjectOverrides: data.transferProjectOverrides ?? true
        })
      } finally {
        ; (orca as any).setLoggingSilenced(false)
      }
      output = res.output

      usedOptions = (res as any)?.usedOptions
      ignoredOptions = (res as any)?.ignoredOptions
      estimatedTimeSec = (res as any)?.estimatedTimeSec
      filamentUsedGrams = (res as any)?.filamentUsedGrams
    } catch (err: any) {
      const msg = String(err?.message ?? err ?? '')
      const lower = msg.toLowerCase()

      // Erro de elementos fora da area de impressao
      if (
        lower.includes('fora da área') ||
        lower.includes('fora da area') ||
        lower.includes('outside') ||
        lower.includes('out of bounds') ||
        lower.includes('does not fit')
      ) {
        throw new BadRequest(msg, { code: 'OBJECTS_OUT_OF_BOUNDS' })
      }

      if (
        lower.includes('unknown') ||
        lower.includes('invalid') ||
        lower.includes('unrecognized') ||
        lower.includes('failed to set')
      ) {
        throw new BadRequest(`Invalid override option(s): ${msg}`)
      }

      // Model empty (no objects found for the requested plate / broken plate metadata)
      if (lower.includes('empty')) {
        throw new BadRequest(
          `No printable objects found for the requested plate. The 3MF plate metadata may be invalid.`,
          { code: 'MODEL_EMPTY' }
        )
      }

      // Always throw a proper Error instance to avoid "error: undefined" logs
      throw new Error(msg || 'Slice failed')
    }

    // Verifica se alguma opcao de 'options' foi ignorada (erro 400)
    // Opcoes de 'config' sao ignoradas silenciosamente
    const ignoredFromOptions = (ignoredOptions ?? []).filter((k: string) => optionsKeys.has(k))
    if (ignoredFromOptions.length > 0) {
      throw new BadRequest(`Invalid override option(s): unknown keys: ${ignoredFromOptions.join(', ')}`)
    }

    // Garante existência do arquivo antes de responder.
    if (!fs.existsSync(output)) {
      throw new Error('Falha ao gerar .gcode.3mf')
    }

    const content = await fs.promises.readFile(output)

    // Validação defensiva: .3mf deve ser um ZIP válido com pelo menos um G-code embutido.
    // Isso evita retornar arquivos inválidos quando um binário antigo do addon exporta
    // G-code puro com extensão .3mf.
    try {
      const zip = await JSZip.loadAsync(content)
      const fileNames = Object.keys(zip.files)
      const hasEmbeddedGcode = fileNames.some(
        name => /(^|\/)Metadata\/.+\.gcode$/i.test(name) || /\.gcode$/i.test(name)
      )
      if (!hasEmbeddedGcode) {
        throw new Error('3MF sem G-code embutido (Metadata/*.gcode não encontrado)')
      }
    } catch (zipErr: any) {
      const details = String(zipErr?.message ?? zipErr ?? 'erro desconhecido')
      throw new Error(
        `3MF inválido gerado pelo addon: ${details}. ` +
        'Verifique se o runtime está carregando o binário local atualizado do OrcaSlicerAddon.'
      )
    }
    // const dataBase64 = content.toString('base64')

    return {
      id: randomUUID(),
      filename: originalFilename,
      outputPath: output,
      contentType: 'model/3mf',
      size: content.length,
      // dataBase64,
      usedOptions,
      ignoredOptions,
      estimatedTimeSec,
      filamentUsedGrams
    }
  }

  async update(id: NullableId, _data: Slicer3MfData, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }

  async patch(id: NullableId, _data: Slicer3MfPatch, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }

  async remove(id: NullableId, _params?: ServiceParams): Promise<Slicer3Mf> {
    return {
      id: String(id ?? ''),
      filename: undefined,
      outputPath: ''
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
