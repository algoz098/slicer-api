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
    // Simplified parsing logic - use the patterns from APP_CONSTANTS or options
    const patterns = [
      /"ProfileTitle"\s*:\s*"([^"]+)"/i,
      /"printer_model"\s*:\s*"([^"]+)"/i,
      /"nozzle_diameter"\s*:\s*\[([^\]]+)\]/i,
      /"default_print_profile"\s*:\s*"([^"]+)"/i
    ]

    for (const pattern of patterns) {
      const match = content.match(pattern)
      if (match && match[1]) {
        const value = match[1].trim()
        
        if (pattern.source.includes('ProfileTitle') || pattern.source.includes('print_profile')) {
          this.extractedData.profile = value
        } else if (pattern.source.includes('printer_model')) {
          this.extractedData.printer = value
        } else if (pattern.source.includes('nozzle_diameter')) {
          this.extractedData.nozzle = value.replace(/["\[\]]/g, '')
        }
      }
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
