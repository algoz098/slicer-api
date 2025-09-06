// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest } from '@feathersjs/errors'
import * as path from 'path'

import type { Application } from '../../../declarations'
import type { FilesInfo, FilesInfoData, FilesInfoPatch, FilesInfoQuery } from './info.schema'
import { logger, loggerHelpers } from '../../../logger'
import { ErrorFactory } from '../../../errors/custom-errors'
import { ProfileFileManager, type ProfileEntry } from '../../../utils/profile-file-manager'

export type { FilesInfo, FilesInfoData, FilesInfoPatch, FilesInfoQuery }

export interface FilesInfoServiceOptions {
  app: Application
}

export interface FilesInfoParams extends Params<FilesInfoQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class FilesInfoService<ServiceParams extends FilesInfoParams = FilesInfoParams>
  implements ServiceInterface<FilesInfo, FilesInfoData, ServiceParams, FilesInfoPatch>
{
  constructor(public options: FilesInfoServiceOptions) {}

  async find(_params?: ServiceParams): Promise<FilesInfo[]> {
    return []
  }

  async get(id: Id, params?: ServiceParams): Promise<FilesInfo> {
    const logContext = {
      service: 'FilesInfoService',
      method: 'get',
      metadata: { id, params }
    }

    try {
      // For now, return basic info - this could be extended to retrieve stored file info
      // The main functionality is in the create method for processing uploaded files
      logger.info('Getting file info', logContext)

      return {
        printer: undefined,
        nozzle: undefined,
        technicalName: undefined
      }
    } catch (error) {
      loggerHelpers.logError('Failed to get file info', error as Error, logContext)
      throw error
    }
  }

  async create(data: FilesInfoData, params?: ServiceParams): Promise<FilesInfo>
  async create(data: FilesInfoData[], params?: ServiceParams): Promise<FilesInfo[]>
  async create(
    data: FilesInfoData | FilesInfoData[],
    params?: ServiceParams
  ): Promise<FilesInfo | FilesInfo[]> {
    if (Array.isArray(data)) {
      // throw invalid request
      throw new BadRequest('Batch requests not supported')
    }

    const logContext = {
      service: 'FilesInfoService',
      method: 'create',
      metadata: { data }
    }

    try {
      // Step 1: Upload file using upload service
      const uploadService = this.options.app.service('files/upload')
      const upload = await uploadService.create({}, params as any)

      logger.info('File uploaded successfully', {
        ...logContext,
        metadata: {
          ...logContext.metadata,
          uploadId: upload.id,
          filename: upload.originalFilename
        }
      })

      // Step 2: Process 3MF file synchronously for better test reliability
      let finalResult: any

      // Check if we're in test environment
      const isTestEnv = process.env.NODE_ENV === 'test'

      if (isTestEnv) {
        // In test environment, process synchronously to avoid polling issues
        try {
          const { ThreeMFProcessorService } = await import('../processors/3mf/3mf.class')
          const tempProcessorService = new ThreeMFProcessorService({ app: this.options.app })

          // Create a temporary processing result
          const tempResult = {
            id: 'temp-' + Date.now(),
            uploadId: upload.id,
            status: 'processing' as const,
            filePath: upload.tempPath,
            extractedData: null,
            errors: [],
            processingTime: 0,
            createdAt: new Date().toISOString(),
            processedAt: null
          }

          // Process directly using the internal method
          const extractedData = await (tempProcessorService as any).extractProfileInfo(
            upload.tempPath,
            ['Metadata/slice_info.config', 'Metadata/model_settings.config', 'Metadata/process_settings_1.config', 'Metadata/project_settings.config'],
            { includeArchiveInfo: false, validateStructure: true }
          )

          finalResult = {
            ...tempResult,
            status: 'completed' as const,
            extractedData,
            processedAt: new Date().toISOString()
          }
        } catch (error) {
          throw ErrorFactory.processing.threeMFCorrupted(upload.tempPath, {
            error: error instanceof Error ? error.message : 'Unknown error',
            uploadId: upload.id
          })
        }
      } else {
        // Simplify: process synchronously in production as well (no async/polling)
        try {
          const { ThreeMFProcessorService } = await import('../processors/3mf/3mf.class')
          const tempProcessorService = new ThreeMFProcessorService({ app: this.options.app })

          const tempResult = {
            id: 'temp-' + Date.now(),
            uploadId: upload.id,
            status: 'processing' as const,
            filePath: upload.tempPath,
            extractedData: null,
            errors: [],
            processingTime: 0,
            createdAt: new Date().toISOString(),
            processedAt: null
          }

          const extractedData = await (tempProcessorService as any).extractProfileInfo(
            upload.tempPath,
            ['Metadata/slice_info.config', 'Metadata/model_settings.config', 'Metadata/process_settings_1.config', 'Metadata/project_settings.config'],
            { includeArchiveInfo: false, validateStructure: true }
          )

          finalResult = {
            ...tempResult,
            status: 'completed' as const,
            extractedData,
            processedAt: new Date().toISOString()
          }
        } catch (error) {
          throw ErrorFactory.processing.threeMFCorrupted(upload.tempPath, {
            error: error instanceof Error ? error.message : 'Unknown error',
            uploadId: upload.id
          })
        }
      }

      // Step 4: Get plate count using plates/count service
      let plateCount: number | undefined
      try {
        const platesCountService = this.options.app.service('plates/count')
        const plateCountResult = await platesCountService.create({}, params as any)
        plateCount = plateCountResult.count

        logger.info('Plate count retrieved successfully from plates/count service', {
          ...logContext,
          metadata: {
            ...logContext.metadata,
            plateCount,
            fileName: plateCountResult.fileName,
            serviceUsed: 'plates/count'
          }
        })
      } catch (plateCountError) {
        logger.warn('Failed to get plate count from plates/count service, continuing without it', {
          ...logContext,
          metadata: {
            ...logContext.metadata,
            error: plateCountError instanceof Error ? plateCountError.message : plateCountError,
            serviceUsed: 'plates/count'
          }
        })
        // Continue without plate count - it's not critical for file info
      }

      // Step 5: Validate extracted data using validation service (pass compareWithProfile for type-guided normalization)
      const query = (params as any)?.query || {}
      const validatedData = await this.validateExtractedData(
        finalResult.extractedData || {},
        plateCount,
        query.compareWithProfile
      )

      // Step 6: Compare with printer profile if requested
      if (query.includeComparison && query.compareWithProfile) {
        validatedData.profileComparison = await this.compareWithProfile(
          finalResult.extractedData || {},
          query.compareWithProfile
        )
      }

      // Step 6: Cleanup upload (optional, could be kept for audit)
      try {
        await this.options.app.service('files/upload').remove(upload.id)
      } catch (cleanupError) {
        logger.warn('Failed to cleanup upload after processing', {
          ...logContext,
          metadata: { ...logContext.metadata, uploadId: upload.id, error: cleanupError }
        })
      }

      loggerHelpers.logSuccess('File info extraction completed', finalResult.processingTime, {
        ...logContext,
        metadata: {
          ...logContext.metadata,
          uploadId: upload.id,
          processingId: finalResult.id,
          extractedData: validatedData
        }
      })

      return validatedData
    } catch (error) {
      loggerHelpers.logError('Failed to extract file info', error as Error, logContext)
      throw error
    }
  }

  /**
   * Validates and sanitizes extracted profile data using validation service
   */
  private async validateExtractedData(profileInfo: any, plateCount?: number, compareWithProfileId?: string): Promise<FilesInfo> {
    // Generate technical name based on the actual printer detected, not the profile
    let technicalName = profileInfo?.technicalName
    if (!technicalName && profileInfo?.printer) {
      // Generate technical name from actual printer model
      if (profileInfo.printer.toLowerCase().includes('p1s')) {
        technicalName = '@BBL P1S'
      } else if (profileInfo.printer.toLowerCase().includes('x1 carbon')) {
        technicalName = '@BBL X1C'
      } else if (profileInfo.printer.toLowerCase().includes('x1e')) {
        technicalName = '@BBL X1E'
      } else if (profileInfo.printer.toLowerCase().includes('x1')) {
        technicalName = '@BBL X1'
      } else if (profileInfo.printer.toLowerCase().includes('a1')) {
        technicalName = '@BBL A1'
      } else if (profileInfo.printer.toLowerCase().includes('bambu')) {
        // Fallback for other Bambu printers
        technicalName = '@BBL'
      } else {
        // Fallback for non-Bambu printers
        technicalName = `@${profileInfo.printer.replace(/\s+/g, '_').toUpperCase()}`
      }
    }

    // Build differences strictly from the raw file content, priorizando Metadata/project_settings.config
    // Agora "differences" deve ser um OBJETO contendo os pares chave/valor dos parâmetros do 3MF.
    const differences: Record<string, any> = {}

    try {
      const raw = profileInfo?.rawSettings || {}
      const names = Object.keys(raw)
      const projectOnly = names.filter(n => /(^|\/)?Metadata\/project_settings\.config$/.test(n))
      const chosen = projectOnly.length ? projectOnly : names

      for (const fname of chosen) {
        const content = raw[fname]
        if (typeof content === 'string') {
          try {
            const parsed = JSON.parse(content)
            if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
              Object.assign(differences, parsed)
            }
          } catch {
            // Se não for JSON, ignoramos aqui pois não representa pares parâmetro/valor
          }
        } else if (content && typeof content === 'object' && !Array.isArray(content)) {
          Object.assign(differences, content)
        }
      }
    } catch {}

	    // If the 3MF lists which settings differ from the system preset, restrict differences to only those keys
	    try {
	      const listed = (differences as any)?.different_settings_to_system
	      if (Array.isArray(listed)) {
	        const reduced: Record<string, any> = {}
	        for (const item of listed) {
	          if (typeof item === 'string') {
	            if (Object.prototype.hasOwnProperty.call(differences, item)) {
	              reduced[item] = (differences as any)[item]
	            }
	          } else if (item && typeof item === 'object') {
	            const key = (item as any).key || (item as any).name
	            if (key && Object.prototype.hasOwnProperty.call(differences, key)) {
	              reduced[key] = (differences as any)[key]
	            }
	          }
	        }
	        // Replace differences with the reduced set and drop the marker array
	        for (const k of Object.keys(differences)) delete (differences as any)[k]
	        Object.assign(differences, reduced)
	      }
	    } catch {}



    // Build expected type map from the best-matching printer profile to guide normalization
    const getExpectedTypesFromProfiles = async (): Promise<Record<string, 'number' | 'boolean' | 'string'>> => {
      try {
        const fsMod = await import('fs')
        const candidates = [
          path.join(__dirname, '../../../../config/orcaslicer/profiles/resources/profiles'), // when compiled
          path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'),
          path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'),
          path.join(process.cwd(), 'source_OrcaSlicer/resources/profiles')
        ]
        const baseDir = candidates.find(p => fsMod.existsSync(p)) || candidates[0]
        const printer = (profileInfo?.printer || '').toString().toLowerCase()
        const nozzle = (profileInfo?.nozzle || '').toString()
        const vendorDir = printer.includes('bambu') ? path.join(baseDir, 'BBL') : baseDir
        const manager = new ProfileFileManager(vendorDir)
        const entries = await manager.loadAllProfiles({ allowedTypes: ['process', 'machine', 'machine_model'] })

        const modelTokens = ['p1p', 'p1s', 'x1e', 'x1c', 'x1', 'a1']
        const model = modelTokens.find(t => printer.includes(t)) || ''

        const scoreEntry = (entry: ProfileEntry): number => {
          const text = (entry.text || '').toLowerCase()
          const p = entry.profile || {}
          const pStr = JSON.stringify(p).toLowerCase()
          let score = 0
          if (model && !(text.includes(model) || pStr.includes(model))) return -1
          if (model) score += 5
          if (printer.includes('bambu') && (text.includes('bbl') || text.includes('bambu') || pStr.includes('bambu'))) score += 2
          if (nozzle && new RegExp(`${nozzle}\\s*nozzle`).test(text)) score += 2
          const hasLayer = /layer[_\s-]?height/.test(pStr)
          const hasInfill = /(sparse[_\s-]?infill|infill[_\s-]?density|fill[_\s-]?density)/.test(pStr)
          const hasSpeed = /print[_\s-]?speed/.test(pStr)
          if (hasLayer) score += 1
          if (hasInfill) score += 1
          if (hasSpeed) score += 1
          return score
        }

        const ranked = entries.map(e => ({ e, s: scoreEntry(e) })).filter(x => x.s >= 0).sort((a, b) => b.s - a.s)
        if (!ranked.length || ranked[0].s === 0) return {}
        const best = ranked[0].e.profile

        const typeMap: Record<string, 'number' | 'boolean' | 'string'> = {}
        const addTypes = (obj: any) => {
          if (!obj || typeof obj !== 'object') return
          for (const [k, v] of Object.entries(obj)) {
            if (typeof v === 'number') typeMap[k] = 'number'
            else if (typeof v === 'boolean') typeMap[k] = 'boolean'
            else if (typeof v === 'string') typeMap[k] = 'string'
          }
        }
        addTypes(best)
        // Additionally, load the referenced process JSON (sub_path) to infer actual setting types
        try {
          if (typeof (best as any).sub_path === 'string' && (best as any).sub_path) {
            const full = path.join(vendorDir, (best as any).sub_path)
            const raw = await fsMod.promises.readFile(full, 'utf8')
            const json = JSON.parse(raw)
            const collect = (obj: any) => {
              if (!obj || typeof obj !== 'object') return
              for (const [k, v] of Object.entries(obj)) {
                if (typeof v === 'number') typeMap[k] = 'number'
                else if (typeof v === 'boolean') typeMap[k] = 'boolean'
                else if (typeof v === 'string') typeMap[k] = 'string'
                else if (v && typeof v === 'object') {
                  for (const [kk, vv] of Object.entries(v)) {
                    if (typeof vv === 'number') typeMap[kk] = 'number'
                    else if (typeof vv === 'boolean') typeMap[kk] = 'boolean'
                    else if (typeof vv === 'string') typeMap[kk] = 'string'
                  }
                }
              }
            }
            collect(json)
          }
        } catch {}
        // Fallback: if still too sparse, scan a handful of vendor process JSONs to build a broader type map
        try {
          if (Object.keys(typeMap).length < 5) {
            const procDir = path.join(vendorDir, 'process')
            const entries = await fsMod.promises.readdir(procDir)
            let count = 0
            for (const f of entries) {
              if (!f.toLowerCase().endsWith('.json')) continue
              const p = path.join(procDir, f)
              try {
                const raw = await fsMod.promises.readFile(p, 'utf8')
                const json = JSON.parse(raw)
                const collect = (obj: any) => {
                  if (!obj || typeof obj !== 'object') return
                  for (const [k, v] of Object.entries(obj)) {
                    if (typeof v === 'number') typeMap[k] = 'number'
                    else if (typeof v === 'boolean') typeMap[k] = 'boolean'
                    else if (typeof v === 'string') typeMap[k] = 'string'
                  }
                }
                collect(json)
                count++
                if (count >= 20) break
              } catch {}
            }
          }
        } catch {}
        return typeMap
      } catch {
        return {}
      }
    }

    // Use expected types when available for normalization; fallback to generic normalization
    const normalizeByExpectedType = (key: string, val: any, expected?: 'number' | 'boolean' | 'string') => {
      if (!expected) return normalizeScalar(key, val)
      if (expected === 'number') {
        if (typeof val === 'number') return val
        if (typeof val === 'string') {
          const t = val.trim()
          if (/^-?\d+(?:\.\d+)?%$/.test(t)) return parseFloat(t.replace('%',''))
          if (/^-?\d+(?:\.\d+)?$/.test(t)) return parseFloat(t)
        }
        return normalizeScalar(key, val)
      }
      if (expected === 'boolean') {
        if (typeof val === 'boolean') return val
        if (typeof val === 'number') return val !== 0
        if (typeof val === 'string') {
          const lower = val.trim().toLowerCase()
          if (lower === 'true' || lower === 'yes' || lower === 'on' || lower === '1') return true
          if (lower === 'false' || lower === 'no' || lower === 'off' || lower === '0') return false
        }
        return normalizeScalar(key, val)
      }
      // expected string
      if (typeof val === 'string') return val.trim()
      return String(val)
    }

    // Build expected type map from compareWithProfile if provided, otherwise fallback to heuristic
    const getExpectedTypesFromProfileId = async (profileId?: string): Promise<Record<string, 'number' | 'boolean' | 'string'>> => {
      if (!profileId) return {}
      try {
        const fsMod = await import('fs')
        const candidates = [
          path.join(__dirname, '../../../../config/orcaslicer/profiles/resources/profiles'),
          path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'),
          path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'),
          path.join(process.cwd(), 'source_OrcaSlicer/resources/profiles')
        ]
        const baseDir = candidates.find(p => fsMod.existsSync(p)) || candidates[0]
        const rel = profileId.replace(/_-_/g, '/')

        const typeMap: Record<string, 'number' | 'boolean' | 'string'> = {}
        const collect = (obj: any) => {
          if (!obj || typeof obj !== 'object') return
          for (const [k, v] of Object.entries(obj)) {
            if (typeof v === 'number') typeMap[k] = 'number'
            else if (typeof v === 'boolean') typeMap[k] = 'boolean'
            else if (typeof v === 'string') typeMap[k] = 'string'
          }
        }

        // Case A: rel already includes vendor folder (e.g., 'BBL/process/...')
        let tried = false
        const fullDirect = path.join(baseDir, rel)
        if (fsMod.existsSync(fullDirect)) {
          const raw = await fsMod.promises.readFile(fullDirect, 'utf8')
          const json = JSON.parse(raw)
          collect(json)
          tried = true
        }

        // Case B: rel is 'process/...': try under each vendor dir
        if (!tried) {
          const dirs = (await fsMod.promises.readdir(baseDir, { withFileTypes: true })).filter(d => d.isDirectory())
          for (const d of dirs) {
            const full = path.join(baseDir, d.name, rel)
            if (fsMod.existsSync(full)) {
              const raw = await fsMod.promises.readFile(full, 'utf8')
              const json = JSON.parse(raw)
              collect(json)
              break
            }
          }
        }

        return typeMap
      } catch {
        return {}
      }
    }

    const expectedTypes = compareWithProfileId
      ? await getExpectedTypesFromProfileId(compareWithProfileId)
      : await getExpectedTypesFromProfiles()


    // Build a flat map of printer profile values from allSettings, prioritizing project_settings over process_settings

    // Normalization helper to coerce values to match printer profile typing
    const normalizeScalar = (key: string, val: any): string | number | boolean | null => {
      if (val === null || val === undefined) return null
      if (typeof val === 'number' || typeof val === 'boolean') return val
      if (typeof val === 'string') {
        const t = val.trim()
        const lower = t.toLowerCase()
        // Common booleans
        if (lower === 'true') return true
        if (lower === 'false') return false
        if (lower === 'yes' || lower === 'on') return true
        if (lower === 'no' || lower === 'off') return false
        // 0/1 for boolean-ish keys (check this first before numeric conversion)
        if ((t === '0' || t === '1') && /(_enable|_enabled|enable|enabled|use_|has_|is_|support|fan|adhesion)/i.test(key)) {
          return t === '1'
        }
        // Percentage -> number
        if (/^-?\d+(?:\.\d+)?%$/.test(t)) return parseFloat(t.replace('%', ''))
        // Numeric string -> number
        if (/^-?\d+(?:\.\d+)?$/.test(t)) return parseFloat(t)
        // Numeric with units -> extract first number
        const numMatch = t.match(/-?\d+(?:\.\d+)?/)
        if (numMatch) return parseFloat(numMatch[0])
        return t
      }
      return null
    }



    // Normalize differences values using expectedTypes only
    try {
      for (const [key, val] of Object.entries(differences)) {
        const expected = expectedTypes[key]

        if (typeof val === 'string') {
          differences[key] = normalizeByExpectedType(key, val, expected)
        } else if (Array.isArray(val) && val.length === 1 && typeof val[0] === 'string') {
          // Common case in 3MF configs: single-element string arrays
          differences[key] = normalizeByExpectedType(key, val[0], expected)
        }
        // For other complex types, keep as-is
      }
    } catch {}

    // Option A: Do not filter by baseline. Keep only keys listed by the slicer (handled above),
    // and preserve their values from the 3MF.
    // No further filtering against printerProfileValues is applied here.

    return {
      printer: profileInfo?.printer,
      nozzle: profileInfo?.nozzle,
      technicalName: technicalName,

      profile: profileInfo?.profile,
      printSettings: profileInfo?.printSettings,
      plateCount: plateCount,
      differences
    }
  }

  /**
   * Generate nozzle profiles based on printer and current settings
   */
  // Deprecated: nozzleProfiles no longer returned by API
  private generateNozzleProfiles(profileInfo: any): any {
    // Determine the selected nozzle (from file or default for printer)
    let selectedNozzle = profileInfo?.nozzle

    // If no nozzle detected, use printer default
    if (!selectedNozzle) {
      if (profileInfo?.printer?.toLowerCase().includes('bambu')) {
        selectedNozzle = '0.4' // Bambu default
      } else if (profileInfo?.printer?.toLowerCase().includes('prusa')) {
        selectedNozzle = '0.4' // Prusa default
      } else if (profileInfo?.printer?.toLowerCase().includes('ender') ||
                 profileInfo?.printer?.toLowerCase().includes('creality')) {
        selectedNozzle = '0.4' // Creality default
      } else {
        selectedNozzle = '0.4' // Universal default
      }
    }

  }

  /**
   * Resolves a dynamic baseline from Orca profile files, scoring candidates by printer/nozzle match
   */
  private async resolveDynamicBaseline(fileData: any): Promise<{ nozzle: string | null; printSettings: { layerHeight: number | null; sparseInfillPercentage: number | null; printSpeed: number | null } } | null> {
    try {
      const baseDir = path.resolve(process.cwd(), 'config', 'orcaslicer', 'profiles', 'resources', 'profiles')
      const printer = (fileData?.printer || '').toString().toLowerCase()
      const nozzle = (fileData?.nozzle || '').toString()

      // Restrict search to vendor directory when possível (ex.: Bambu -> BBL)
      const vendorDir = printer.includes('bambu') ? path.join(baseDir, 'BBL') : baseDir
      const manager = new ProfileFileManager(vendorDir)
      // Consider also machine/machine_model to increase chance de defaults completos
      const entries = await manager.loadAllProfiles({ allowedTypes: ['process', 'machine', 'machine_model'] })

      // Extract model token from printer (ex.: 'p1p', 'p1s', 'x1c')
      const modelTokens = ['p1p', 'p1s', 'x1e', 'x1c', 'x1', 'a1']
      const model = modelTokens.find(t => printer.includes(t)) || ''

      // Score function for a profile entry
      const scoreEntry = (entry: ProfileEntry): number => {
        const text = (entry.text || '').toLowerCase()
        const p = entry.profile || {}
        const pStr = JSON.stringify(p).toLowerCase()
        let score = 0

        // Hard filter: require model token when available
        if (model && !(text.includes(model) || pStr.includes(model))) return -1

        // Prefer exact model matches
        if (model) score += 5

        // Prefer brand mention
        if (printer.includes('bambu') && (text.includes('bbl') || text.includes('bambu') || pStr.includes('bambu'))) {
          score += 2
        }

        // Match nozzle mention in text
        if (nozzle && new RegExp(`${nozzle}\\s*nozzle`).test(text)) score += 2

        // Presence of key print settings increases usefulness
        const hasLayer = /layer[_\s-]?height/.test(pStr)
        const hasInfill = /(sparse[_\s-]?infill|infill[_\s-]?density|fill[_\s-]?density)/.test(pStr)
        const hasSpeed = /print[_\s-]?speed/.test(pStr)
        if (hasLayer) score += 1
        if (hasInfill) score += 1
        if (hasSpeed) score += 1

        return score
      }

      const ranked = entries
        .map(e => ({ e, s: scoreEntry(e) }))
        .filter(x => x.s >= 0)
        .sort((a, b) => b.s - a.s)
        .slice(0, 10)

      if (ranked.length === 0 || ranked[0].s === 0) return null

      const best = ranked[0].e.profile

      // Extract defaults generically from profile content
      const extractNum = (obj: any, keys: string[], percent = false): number | null => {
        for (const k of keys) {
          const v = obj?.[k]
          if (typeof v === 'number') return percent ? v : v
          if (typeof v === 'string') {
            const m = v.match(percent ? /(\d+(?:\.\d+)?)%/ : /(\d+(?:\.\d+)?)/)
            if (m) return parseFloat(m[1])
          }
        }
        return null
      }

      const defaults = {
        nozzle: nozzle || null, // baseline should reflect same nozzle family when possible
        printSettings: {
          layerHeight: extractNum(best, ['layer_height', 'layerHeight']),
          sparseInfillPercentage: extractNum(best, ['sparse_infill_density', 'sparse_infill_percentage'], true),
          printSpeed: extractNum(best, ['print_speed', 'printSpeed'])
        }
      }

      return defaults
    } catch {
      return null
    }
  }

  /**
   * Compares file parameters with a selected printer profile
   */
  private async compareWithProfile(fileData: any, profileId: string): Promise<any> {
    const logContext = {
      service: 'FilesInfoService',
      method: 'compareWithProfile',
      metadata: { profileId, fileData }
    }

    try {
      // Get the printer profile
      const printerProfilesService = this.options.app.service('printer-profiles')
      const profile = await printerProfilesService.get(profileId)

      logger.info('Retrieved printer profile for comparison', {
        ...logContext,
        metadata: { ...logContext.metadata, profileName: profile.text }
      })

      // Define parameters to compare
      const parametersToCompare = [
        { key: 'printer', label: 'Printer Model', critical: true },
        { key: 'nozzle', label: 'Nozzle Diameter', critical: true },
        { key: 'profile', label: 'Print Profile', critical: false },
        { key: 'technicalName', label: 'Technical Name', critical: false }
      ]

      // Add print settings parameters if available
      const printSettingsToCompare = [
        { key: 'sparseInfillPercentage', label: 'Infill Percentage', critical: false },
        { key: 'layerHeight', label: 'Layer Height', critical: true },
        { key: 'printSpeed', label: 'Print Speed', critical: false },
        { key: 'bedTemperature', label: 'Bed Temperature', critical: false },
        { key: 'nozzleTemperature', label: 'Nozzle Temperature', critical: false },
        { key: 'supportEnabled', label: 'Support Enabled', critical: false },
        { key: 'adhesionType', label: 'Adhesion Type', critical: false },
        { key: 'filamentType', label: 'Filament Type', critical: true }
      ]

      const differences: any[] = []
      let criticalDifferences = 0

      // Compare each basic parameter
      for (const param of parametersToCompare) {
        const fileValue = this.normalizeValue(fileData[param.key])
        const profileValue = this.normalizeValue(
          (profile as any)[param.key] || (profile.fileContent as any)?.[param.key]
        )

        if (fileValue !== profileValue) {
          differences.push({
            parameter: param.label,
            fileValue: fileValue,
            profileValue: profileValue
          })

          if (param.critical) {
            criticalDifferences++
          }
        }
      }

      // Compare print settings if available
      if (fileData.printSettings) {
        for (const param of printSettingsToCompare) {
          const fileValue = this.normalizeValue(fileData.printSettings[param.key])
          const profileValue = this.normalizeValue(
            (profile as any).printSettings?.[param.key] ||
            (profile.fileContent as any)?.printSettings?.[param.key]
          )

          if (fileValue !== profileValue && fileValue !== null) {
            differences.push({
              parameter: param.label,
              fileValue: fileValue,
              profileValue: profileValue
            })

            if (param.critical) {
              criticalDifferences++
            }
          }
        }
      }

      // Calculate compatibility score
      const totalBasicParameters = parametersToCompare.length
      const totalPrintParameters = fileData.printSettings ? printSettingsToCompare.length : 0
      const totalParameters = totalBasicParameters + totalPrintParameters
      const matchingParameters = totalParameters - differences.length
      const compatibilityScore = totalParameters > 0 ? Math.round((matchingParameters / totalParameters) * 100) : 0

      // Apply penalty for critical differences
      const finalScore = criticalDifferences > 0
        ? Math.max(0, compatibilityScore - (criticalDifferences * 25))
        : compatibilityScore

      const comparison = {
        selectedProfileId: profileId,
        differences: differences,
        summary: {
          totalDifferences: differences.length,
          criticalDifferences: criticalDifferences,
          compatibilityScore: finalScore
        }
      }

      logger.info('Profile comparison completed', {
        ...logContext,
        metadata: {
          ...logContext.metadata,
          totalDifferences: differences.length,
          criticalDifferences: criticalDifferences,
          compatibilityScore: finalScore
        }
      })

      return comparison
    } catch (error) {
      loggerHelpers.logError('Failed to compare with profile', error as Error, logContext)

      // Return empty comparison on error
      return {
        selectedProfileId: profileId,
        differences: [],
        summary: {
          totalDifferences: 0,
          criticalDifferences: 0,
          compatibilityScore: 0
        }
      }
    }
  }

  /**
   * Normalizes values for comparison (handles different data types and formats)
   */
  private normalizeValue(value: any): string | null {
    if (value === null || value === undefined) {
      return null
    }

    if (typeof value === 'string') {
      return value.trim().toLowerCase()
    }

    if (typeof value === 'number') {
      return value.toString()
    }

    if (typeof value === 'boolean') {
      return value.toString()
    }

    // For objects/arrays, convert to JSON string
    return JSON.stringify(value).toLowerCase()
  }

  /**
   * Returns default values for a given printer (and optional nozzle) to compare against
   */
  private getDefaultForPrinter(printer?: string, nozzle?: string) {
    const p = (printer || '').toLowerCase()
    // Baseline defaults for P1S
    const p1sDefaults = {
      nozzle: nozzle || '0.4',
      printSettings: {
        layerHeight: nozzle === '0.2' ? 0.12 : 0.2,
        sparseInfillPercentage: 15,
        printSpeed: 250
      }
    }

    if (p.includes('p1s')) return p1sDefaults

    // Generic Bambu fallback
    if (p.includes('bambu')) {
      return {
        nozzle: nozzle || '0.4',
        printSettings: {
          layerHeight: 0.2,
          sparseInfillPercentage: 15,
          printSpeed: 250
        }
      }
    }

    // Fallback for unknown printers
    return {
      nozzle: nozzle || '0.4',
      printSettings: {
        layerHeight: 0.2,
        sparseInfillPercentage: 15,
        printSpeed: 200
      }
    }
  }

  // This method has to be added to the 'methods' option to make it available to clients
  async update(_id: NullableId, data: FilesInfoData, _params?: ServiceParams): Promise<FilesInfo> {
    return {
      printer: undefined,
      nozzle: undefined,
      technicalName: undefined,
      ...data
    }
  }

  async patch(_id: NullableId, data: FilesInfoPatch, _params?: ServiceParams): Promise<FilesInfo> {
    return {
      printer: undefined,
      nozzle: undefined,
      technicalName: undefined,
      ...data
    }
  }

  async remove(_id: NullableId, _params?: ServiceParams): Promise<FilesInfo> {
    return {
      printer: undefined,
      nozzle: undefined,
      technicalName: undefined
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
