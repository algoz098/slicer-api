// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest, NotFound } from '@feathersjs/errors'
import * as crypto from 'crypto'
import * as yauzl from 'yauzl'

import type { Application } from '../../../../declarations'
import type { ThreeMFProcessor, ThreeMFProcessorData, ThreeMFProcessorPatch, ThreeMFProcessorQuery } from './3mf.schema'
import { logger, loggerHelpers } from '../../../../logger'
import { ErrorFactory } from '../../../../errors/custom-errors'
import { APP_CONSTANTS } from '../../../../config/app-config'

export type { ThreeMFProcessor, ThreeMFProcessorData, ThreeMFProcessorPatch, ThreeMFProcessorQuery }

export interface ThreeMFProcessorServiceOptions {
  app: Application
}

export interface ThreeMFProcessorParams extends Params<ThreeMFProcessorQuery> {}

// In-memory storage for processing results (in production, use database)
const processingStore = new Map<string, ThreeMFProcessor>()

export class ThreeMFProcessorService<ServiceParams extends ThreeMFProcessorParams = ThreeMFProcessorParams>
  implements ServiceInterface<ThreeMFProcessor, ThreeMFProcessorData, ServiceParams, ThreeMFProcessorPatch>
{
  constructor(public options: ThreeMFProcessorServiceOptions) {}

  /**
   * Find processing results with optional filtering
   */
  async find(params?: ServiceParams): Promise<ThreeMFProcessor[]> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'find',
      metadata: { params }
    }

    try {
      const query = params?.query || {}
      let results = Array.from(processingStore.values())

      // Filter by upload ID
      if (query.uploadId) {
        results = results.filter(result => result.uploadId === query.uploadId)
      }

      // Filter by status
      if (query.status) {
        results = results.filter(result => result.status === query.status)
      }

      // Filter by date range
      if (query.processedAfter) {
        const afterDate = new Date(query.processedAfter)
        results = results.filter(result => new Date(result.processedAt) > afterDate)
      }

      if (query.processedBefore) {
        const beforeDate = new Date(query.processedBefore)
        results = results.filter(result => new Date(result.processedAt) < beforeDate)
      }

      // Filter by errors/warnings/data
      if (query.hasErrors) {
        results = results.filter(result => result.errors.length > 0)
      }

      if (query.hasWarnings) {
        results = results.filter(result => result.warnings.length > 0)
      }

      if (query.hasExtractedData) {
        results = results.filter(result => result.extractedData !== undefined)
      }

      logger.info('Processing results found', {
        ...logContext,
        metadata: { ...logContext.metadata, count: results.length }
      })

      return results
    } catch (error) {
      loggerHelpers.logError('Failed to find processing results', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('find', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Get a specific processing result by ID
   */
  async get(id: Id, params?: ServiceParams): Promise<ThreeMFProcessor> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'get',
      metadata: { id }
    }

    try {
      const result = processingStore.get(String(id))

      if (!result) {
        logger.warn('Processing result not found', logContext)
        throw new NotFound(`Processing result with id ${id} not found`)
      }

      logger.info('Processing result retrieved', logContext)
      return result
    } catch (error) {
      loggerHelpers.logError('Failed to get processing result', error as Error, logContext)
      throw error
    }
  }

  /**
   * Create a new 3MF processing request
   */
  async create(data: ThreeMFProcessorData, params?: ServiceParams): Promise<ThreeMFProcessor> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'create',
      metadata: { uploadId: data.uploadId }
    }

    try {
      // Get upload information from upload service
      const uploadService = this.options.app.service('files/upload')
      const upload = await uploadService.get(data.uploadId)

      if (!upload) {
        throw new NotFound(`Upload with id ${data.uploadId} not found`)
      }

      // Validate that it's a 3MF file
      if (upload.fileExtension !== '.3mf') {
        throw new BadRequest(`File is not a 3MF file. Extension: ${upload.fileExtension}`)
      }

      const processingId = crypto.randomUUID()
      const startTime = Date.now()

      // Create initial processing record
      const processingResult: ThreeMFProcessor = {
        id: processingId,
        uploadId: data.uploadId,
        filePath: upload.tempPath,
        status: 'processing',
        errors: [],
        warnings: [],
        processedAt: new Date().toISOString()
      }

      // Store initial result
      processingStore.set(processingId, processingResult)

      // Update upload status to processing
      await uploadService.patch(data.uploadId, { status: 'processing' })

      // Start processing asynchronously
      this.processFile(processingResult, data.options || {}, startTime)
        .catch(error => {
          loggerHelpers.logError('Async processing failed', error as Error, logContext)
        })

      logger.info('3MF processing started', {
        ...logContext,
        metadata: { ...logContext.metadata, processingId, filePath: upload.tempPath }
      })

      return processingResult
    } catch (error) {
      loggerHelpers.logError('Failed to create processing request', error as Error, logContext)
      throw error
    }
  }

  /**
   * Update processing status
   */
  async patch(id: NullableId, data: ThreeMFProcessorPatch, params?: ServiceParams): Promise<ThreeMFProcessor> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'patch',
      metadata: { id, data }
    }

    try {
      const result = await this.get(id as Id, params)

      // Update allowed fields
      if (data.status) {
        result.status = data.status
      }

      // Save updated result
      processingStore.set(result.id, result)

      logger.info('Processing result updated', logContext)
      return result
    } catch (error) {
      loggerHelpers.logError('Failed to update processing result', error as Error, logContext)
      throw error
    }
  }

  /**
   * Remove a processing result
   */
  async remove(id: NullableId, params?: ServiceParams): Promise<ThreeMFProcessor> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'remove',
      metadata: { id }
    }

    try {
      const result = await this.get(id as Id, params)

      // Remove from store
      processingStore.delete(result.id)

      logger.info('Processing result removed', logContext)
      return result
    } catch (error) {
      loggerHelpers.logError('Failed to remove processing result', error as Error, logContext)
      throw error
    }
  }

  /**
   * Process 3MF file asynchronously
   */
  private async processFile(
    processingResult: ThreeMFProcessor,
    options: any,
    startTime: number
  ): Promise<void> {
    const logContext = {
      service: 'ThreeMFProcessorService',
      method: 'processFile',
      metadata: { processingId: processingResult.id, filePath: processingResult.filePath }
    }

    try {
      loggerHelpers.logFileProcessing(processingResult.filePath, '3MF profile extraction', logContext)

      const candidateFiles = options.candidateFiles || APP_CONSTANTS.THREE_MF.CANDIDATE_FILES
      const timeout = options.timeout || APP_CONSTANTS.THREE_MF.PROCESSING_TIMEOUT

      // Process with timeout
      const extractedData = await Promise.race([
        this.extractProfileInfo(processingResult.filePath, candidateFiles, options),
        new Promise((_, reject) =>
          setTimeout(() => reject(new Error('Processing timeout')), timeout)
        )
      ]) as any

      // Calculate processing time
      const processingTime = Date.now() - startTime

      // Update result with extracted data
      processingResult.status = 'completed'
      processingResult.extractedData = extractedData
      processingResult.processingTime = processingTime
      processingResult.processedAt = new Date().toISOString()

      // Update upload status
      const uploadService = this.options.app.service('files/upload')
      await uploadService.patch(processingResult.uploadId, {
        status: 'processed',
        metadata: extractedData
      })

      // Store updated result
      processingStore.set(processingResult.id, processingResult)

      loggerHelpers.logSuccess('3MF processing completed', processingTime, {
        ...logContext,
        metadata: { ...logContext.metadata, extractedData }
      })

    } catch (error) {
      const processingTime = Date.now() - startTime

      // Update result with error
      processingResult.status = 'failed'
      processingResult.errors.push((error as Error).message)
      processingResult.processingTime = processingTime
      processingResult.processedAt = new Date().toISOString()

      // Update upload status
      try {
        const uploadService = this.options.app.service('files/upload')
        await uploadService.patch(processingResult.uploadId, { status: 'error' })
      } catch (uploadError) {
        loggerHelpers.logError('Failed to update upload status after processing error', uploadError as Error, logContext)
      }

      // Store updated result
      processingStore.set(processingResult.id, processingResult)

      loggerHelpers.logError('3MF processing failed', error as Error, logContext)
    }
  }

  /**
   * Extract profile information from 3MF file
   */
  private async extractProfileInfo(
    filePath: string,
    candidateFiles: string[],
    options: any
  ): Promise<any> {
    return new Promise((resolve, reject) => {
      yauzl.open(filePath, { lazyEntries: true }, (err, zipfile) => {
        if (err) {
          return reject(ErrorFactory.processing.threeMFCorrupted(filePath))
        }

        if (!zipfile) {
          return reject(ErrorFactory.processing.threeMFCorrupted(filePath))
        }

        const extractor = new ProfileExtractor(zipfile, candidateFiles, options)
        extractor.extract()
          .then(resolve)
          .catch(reject)
      })
    })
  }
}

// Helper class for profile extraction (simplified version of the original)
class ProfileExtractor {
  private found = false
  private extractedData: any = {}

  constructor(
    private readonly zipfile: yauzl.ZipFile,
    private readonly candidateFiles: string[],
    private readonly options: any
  ) {}

  async extract(): Promise<any> {
    return new Promise((resolve, reject) => {
      this.zipfile.readEntry()

      this.zipfile.on('entry', (entry: yauzl.Entry) => {
        if (this.candidateFiles.includes(entry.fileName)) {
          this.found = true
          this.processEntry(entry, resolve, reject)
        } else {
          this.zipfile.readEntry()
        }
      })

      this.zipfile.on('end', () => {
        this.zipfile.close()
        resolve(this.extractedData)
      })

      this.zipfile.on('error', (error) => {
        this.zipfile.close()
        reject(ErrorFactory.processing.threeMFCorrupted('', { error: error.message }))
      })
    })
  }

  private processEntry(
    entry: yauzl.Entry,
    resolve: (value: any) => void,
    reject: (reason: any) => void
  ): void {
    this.zipfile.openReadStream(entry, (err, readStream) => {
      if (err) {
        this.zipfile.close()
        return reject(ErrorFactory.processing.metadataNotFound(entry.fileName))
      }

      if (!readStream) {
        this.zipfile.close()
        return reject(ErrorFactory.processing.metadataNotFound(entry.fileName))
      }

      const chunks: Buffer[] = []

      readStream.on('data', (chunk: Buffer) => chunks.push(chunk))

      readStream.on('end', () => {
        try {
          const content = Buffer.concat(chunks).toString('utf8')
          this.parseContent(content, entry.fileName)
          this.zipfile.readEntry()
        } catch (error) {
          this.zipfile.close()
          reject(ErrorFactory.processing.metadataNotFound(entry.fileName))
        }
      })

      readStream.on('error', (error) => {
        this.zipfile.close()
        reject(ErrorFactory.processing.metadataNotFound(entry.fileName))
      })
    })
  }

  private parseContent(content: string, fileName: string): void {
    // Try to parse full JSON to capture all settings for this file
    try {
      const parsed = JSON.parse(content)
      if (!this.extractedData.allSettings) this.extractedData.allSettings = {}
      this.extractedData.allSettings[fileName] = parsed
    } catch {
      // ignore if not valid JSON; regex-based extraction will continue
    }

    // Basic profile patterns

    // Always keep raw content for this file
    try {
      if (!this.extractedData.rawSettings) this.extractedData.rawSettings = {}
      this.extractedData.rawSettings[fileName] = content
    } catch {}

    // Generic key-value parsing fallback (handles JSON-like and INI-like lines)
    try {
      if (!this.extractedData.allSettings) this.extractedData.allSettings = {}
      const acc: Record<string, any> = {}

      const applyKV = (k: string, v: string) => {
        const key = k.trim()
        let val: any = v.trim()
        // Strip trailing commas or quotes
        val = val.replace(/^"|^'|\s*,$|"$|'$/g, '')
        // Normalize numeric and percent
        if (/^\d+(?:\.\d+)?%$/.test(val)) {
          val = parseFloat(val.replace('%', ''))
        } else if (/^-?\d+(?:\.\d+)?$/.test(val)) {
          const num = parseFloat(val)
          if (!isNaN(num)) val = num
        }
        acc[key] = val
      }

      const regexes: RegExp[] = [
        /"([^"]+)"\s*:\s*"([^"]*)"/g,       // "key": "value"
        /'([^']+)'\s*:\s*'([^']*)'/g,           // 'key': 'value'
        /"([^"]+)"\s*:\s*([0-9.]+)/g,         // "key": 123
        /([A-Za-z0-9_.-]+)\s*:\s*"([^"]*)"/g, // key: "value"
        /([A-Za-z0-9_.-]+)\s*=\s*"([^"]*)"/g, // key = "value"
        /([A-Za-z0-9_.-]+)\s*=\s*([^\s,}]+)/g,  // key = value
        /([A-Za-z0-9_.-]+)\s*:\s*([^\s,}]+)/g   // key: value
      ]

      for (const re of regexes) {
        let m: RegExpExecArray | null
        while ((m = re.exec(content)) !== null) {
          applyKV(m[1], m[2])
        }
      }

      this.extractedData.allSettings[fileName] = {
        ...(this.extractedData.allSettings[fileName] || {}),
        ...acc
      }
    } catch {}

    const basicPatterns = [
      /"ProfileTitle"\s*:\s*"([^"]+)"/i,
      /"printer_model"\s*:\s*"([^"]+)"/i,
      /"nozzle_diameter"\s*:\s*\[([^\]]+)\]/i,
      /"default_print_profile"\s*:\s*"([^"]+)"/i
    ]

    // Enhanced patterns for nozzle and profile detection
    const nozzleProfilePatterns = [
      // Bambu Lab patterns
      /"nozzle_diameter"\s*:\s*\[([^\]]+)\]/gi,
      /"compatible_printers"\s*:\s*\[([^\]]+)\]/gi,
      /"layer_height"\s*:\s*"([^"]+)"/gi,
      /"print_profile"\s*:\s*"([^"]+)"/gi,
      // OrcaSlicer patterns
      /"printer_variant"\s*:\s*"([^"]+)"/gi,
      /"nozzle_type"\s*:\s*"([^"]+)"/gi,
      // PrusaSlicer patterns
      /"printer_settings_id"\s*:\s*"([^"]+)"/gi,
      /"print_settings_id"\s*:\s*"([^"]+)"/gi,
      // Additional nozzle detection patterns
      /"name"\s*:\s*"([^"]*(\d+\.?\d*)\s*nozzle[^"]*)"/gi
    ]

    // Print settings patterns
    const settingsPatterns = [
      /"sparse_infill_density"\s*:\s*"?([^",}]+)"?/i,
      /"sparse_infill_percentage"\s*:\s*"?([^",}]+)"?/i,
      /"layer_height"\s*:\s*"?([^",}]+)"?/i,
      /"outer_wall_speed"\s*:\s*"([^"]+)"/i,
      /"inner_wall_speed"\s*:\s*"([^"]+)"/i,
      /"infill_speed"\s*:\s*"([^"]+)"/i,
      /"bed_temperature"\s*:\s*"([^"]+)"/i,
      /"nozzle_temperature"\s*:\s*"([^"]+)"/i,
      /"support_enable"\s*:\s*"([^"]+)"/i,
      /"adhesion_type"\s*:\s*"([^"]+)"/i,
      /"filament_type"\s*:\s*"([^"]+)"/i,
      /"filament_material"\s*:\s*"([^"]+)"/i
    ]

    // Parse basic profile information
    for (const pattern of basicPatterns) {
      const match = content.match(pattern)
      if (match && match[1]) {
        const value = match[1].trim()

        if (pattern.source.includes('ProfileTitle') || pattern.source.includes('print_profile')) {
          // Store the original profile for reference, but we'll generate a corrected one later
          this.extractedData.originalProfile = value
        } else if (pattern.source.includes('printer_model')) {
          this.extractedData.printer = value
        } else if (pattern.source.includes('nozzle_diameter')) {
          this.extractedData.nozzle = value.replace(/["\[\]]/g, '')
        }
      }
    }


    // Track source precedence for settings: project_settings.config > process_settings_*.config
    if (!(this.extractedData as any)._printSettingsSource) {
      ;(this.extractedData as any)._printSettingsSource = {}
    }
    const srcMap = (this.extractedData as any)._printSettingsSource as Record<string, 'project' | 'process' | 'other'>
    const isProjectConfig = /(^|\/)Metadata\/project_settings\.config$/.test(fileName)
    const isProcessConfig = /(^|\/)Metadata\/process_settings.*\.config$/.test(fileName)
    const currentSource: 'project' | 'process' | 'other' = isProjectConfig ? 'project' : isProcessConfig ? 'process' : 'other'

    const shouldWrite = (key: string) => {
      const prev = srcMap[key]
      if (currentSource === 'project') return true
      if (!prev) return true
      if (prev !== 'project') return true
      return false
    }

    // Parse print settings
    if (!this.extractedData.printSettings) {
      this.extractedData.printSettings = {}
    }

    for (const pattern of settingsPatterns) {
      const match = content.match(pattern)
      if (match && match[1]) {
        const value = match[1].trim()

        if (pattern.source.includes('sparse_infill_density') || pattern.source.includes('sparse_infill_percentage')) {
          const numValue = parseFloat(value.replace('%', ''))
          if (!isNaN(numValue) && shouldWrite('sparseInfillPercentage')) {
            this.extractedData.printSettings.sparseInfillPercentage = numValue
            srcMap['sparseInfillPercentage'] = currentSource
          }
        } else if (pattern.source.includes('layer_height')) {
          const numValue = parseFloat(value)
          if (!isNaN(numValue) && shouldWrite('layerHeight')) {
            this.extractedData.printSettings.layerHeight = numValue
            srcMap['layerHeight'] = currentSource
          }
        } else if (pattern.source.includes('speed')) {
          const numValue = parseFloat(value)
          if (!isNaN(numValue) && shouldWrite('printSpeed')) {
            this.extractedData.printSettings.printSpeed = numValue
            srcMap['printSpeed'] = currentSource
          }
        } else if (pattern.source.includes('bed_temperature')) {
          const numValue = parseFloat(value)
          if (!isNaN(numValue) && shouldWrite('bedTemperature')) {
            this.extractedData.printSettings.bedTemperature = numValue
            srcMap['bedTemperature'] = currentSource
          }
        } else if (pattern.source.includes('nozzle_temperature')) {
          const numValue = parseFloat(value)
          if (!isNaN(numValue) && shouldWrite('nozzleTemperature')) {
            this.extractedData.printSettings.nozzleTemperature = numValue
            srcMap['nozzleTemperature'] = currentSource
          }
        } else if (pattern.source.includes('support_enable')) {
          if (shouldWrite('supportEnabled')) {
            this.extractedData.printSettings.supportEnabled = value === 'true' || value === '1'
            srcMap['supportEnabled'] = currentSource
          }
        } else if (pattern.source.includes('adhesion_type')) {
          if (shouldWrite('adhesionType')) {
            this.extractedData.printSettings.adhesionType = value
            srcMap['adhesionType'] = currentSource
          }
        } else if (pattern.source.includes('filament_type') || pattern.source.includes('filament_material')) {
          if (shouldWrite('filamentType')) {
            this.extractedData.printSettings.filamentType = value
            srcMap['filamentType'] = currentSource
          }
        }
      }
    }

    // Process nozzle profile patterns for enhanced nozzle detection
    for (const pattern of nozzleProfilePatterns) {
      const matches = content.match(pattern)
      if (matches) {
        for (const match of matches) {
          const fullMatch = match.match(pattern)
          if (fullMatch && fullMatch[1]) {
            const value = fullMatch[1].trim()

            if (pattern.source.includes('nozzle_diameter')) {
              // Extract nozzle diameter from array format: ["0.4"] or [0.4]
              const nozzleMatch = value.match(/["']?(\d+\.?\d*)["']?/)
              if (nozzleMatch && nozzleMatch[1]) {
                this.extractedData.nozzle = nozzleMatch[1]
              }
            } else if (pattern.source.includes('printer_settings_id') || pattern.source.includes('name')) {
              // Extract nozzle from printer name: "Bambu Lab P1S 0.4 nozzle"
              const nozzleMatch = value.match(/(\d+\.?\d*)\s*nozzle/i)
              if (nozzleMatch && nozzleMatch[1]) {
                this.extractedData.nozzle = nozzleMatch[1]
              }
            } else if (pattern.source.includes('compatible_printers')) {
              // Extract nozzle from compatible printers list
              const nozzleMatch = value.match(/(\d+\.?\d*)\s*nozzle/i)
              if (nozzleMatch && nozzleMatch[1] && !this.extractedData.nozzle) {
                this.extractedData.nozzle = nozzleMatch[1]
              }
            }
          }
        }
      }
    }

    // Extract detailed nozzle and profile information
    this.extractLayerProfiles()
  }



  /**
   * Extract layer profiles for the selected nozzle only
   */
  private extractLayerProfiles(): void {
    // Initialize nozzleProfiles if not exists
    if (!this.extractedData.nozzleProfiles) {
      this.extractedData.nozzleProfiles = {
        currentNozzle: null,
        layerProfiles: {},
        currentProfile: null
      }
    }

    // Determine the selected nozzle strictly from file content.
    // NOTE: We avoid "chutar"/guess defaults (ex.: 0.4) para não misturar valores.
    // Se o nozzle não for detectado nos arquivos Metadata, mantemos undefined aqui
    // e a API superior decide como comparar com defaults no campo `differences`.
    let selectedNozzle = this.extractedData.nozzle
    // (sem fallback automático aqui de propósito)

    // Common layer height profiles for different nozzle sizes
    const commonProfiles = {
      '0.2': [
        { name: 'Ultra Fine', layerHeight: 0.08, description: 'Highest quality, slowest print' },
        { name: 'Fine', layerHeight: 0.12, description: 'High quality' },
        { name: 'Standard', layerHeight: 0.16, description: 'Balanced quality and speed' }
      ],
      '0.4': [
        { name: 'Fine', layerHeight: 0.16, description: 'High quality' },
        { name: 'Standard', layerHeight: 0.2, description: 'Balanced quality and speed' },
        { name: 'Draft', layerHeight: 0.24, description: 'Fast print, lower quality' },
        { name: 'Fast', layerHeight: 0.28, description: 'Fastest print' }
      ],
      '0.6': [
        { name: 'Standard', layerHeight: 0.2, description: 'Balanced quality and speed' },
        { name: 'Draft', layerHeight: 0.3, description: 'Fast print' },
        { name: 'Fast', layerHeight: 0.4, description: 'Fastest print, thick layers' }
      ],
      '0.8': [
        { name: 'Standard', layerHeight: 0.3, description: 'Balanced for large nozzle' },
        { name: 'Draft', layerHeight: 0.4, description: 'Fast print' },
        { name: 'Fast', layerHeight: 0.6, description: 'Very fast, very thick layers' }
      ],
      '1.0': [
        { name: 'Standard', layerHeight: 0.4, description: 'Balanced for large nozzle' },
        { name: 'Draft', layerHeight: 0.6, description: 'Fast print' },
        { name: 'Fast', layerHeight: 0.8, description: 'Very fast, very thick layers' }
      ]
    }

    // Update nozzle profiles to show only the selected nozzle
    this.extractedData.nozzleProfiles.currentNozzle = selectedNozzle
    this.extractedData.nozzleProfiles.layerProfiles = {}

    // Add layer profiles only for the selected nozzle
    if (commonProfiles[selectedNozzle as keyof typeof commonProfiles]) {
      this.extractedData.nozzleProfiles.layerProfiles[selectedNozzle] =
        commonProfiles[selectedNozzle as keyof typeof commonProfiles]
    } else {
      // Fallback profiles for unknown nozzle sizes
      const nozzleSize = parseFloat(selectedNozzle)
      this.extractedData.nozzleProfiles.layerProfiles[selectedNozzle] = [
        { name: 'Fine', layerHeight: nozzleSize * 0.4, description: 'High quality' },
        { name: 'Standard', layerHeight: nozzleSize * 0.5, description: 'Balanced quality and speed' },
        { name: 'Draft', layerHeight: nozzleSize * 0.75, description: 'Fast print' }
      ]
    }

    // Generate corrected profile name based on actual printer and settings (format with 2 decimals)
    this.generateCorrectedProfile(selectedNozzle)
  }

  /**
   * Generate a corrected profile name based on the actual printer detected
   */
  private generateCorrectedProfile(selectedNozzle: string): void {
    const layerHeight = this.extractedData.printSettings?.layerHeight || 0.2
    const printer = this.extractedData.printer || ''

    // Determine the correct technical suffix based on actual printer
    let technicalSuffix = ''
    if (printer.toLowerCase().includes('p1s')) {
      technicalSuffix = '@BBL P1S'
    } else if (printer.toLowerCase().includes('x1 carbon')) {
      technicalSuffix = '@BBL X1C'
    } else if (printer.toLowerCase().includes('x1e')) {
      technicalSuffix = '@BBL X1E'
    } else if (printer.toLowerCase().includes('x1')) {
      technicalSuffix = '@BBL X1'
    } else if (printer.toLowerCase().includes('a1')) {
      technicalSuffix = '@BBL A1'
    } else if (printer.toLowerCase().includes('bambu')) {
      technicalSuffix = '@BBL'
    } else {
      technicalSuffix = `@${printer.replace(/\s+/g, '_').toUpperCase()}`
    }

    // Determine quality level based on layer height
    let qualityLevel = 'Standard'
    if (layerHeight <= 0.08) {
      qualityLevel = 'Ultra Fine'
    } else if (layerHeight <= 0.12) {
      qualityLevel = 'Fine'
    } else if (layerHeight <= 0.16) {
      qualityLevel = 'High Quality'
    } else if (layerHeight <= 0.2) {
      qualityLevel = 'Standard'
    } else if (layerHeight <= 0.24) {
      qualityLevel = 'Draft'
    } else {
      qualityLevel = 'Fast'
    }

    // Generate the corrected profile name (format layer height with 2 decimals)
    const lh2 = (Math.round(layerHeight * 100) / 100).toFixed(2)
    this.extractedData.profile = `${lh2}mm ${qualityLevel} ${technicalSuffix}`
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
