// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { NotFound } from '@feathersjs/errors'
import * as crypto from 'crypto'
import * as path from 'path'

import type { Application } from '../../../declarations'
import type { FilesUpload, FilesUploadData, FilesUploadPatch, FilesUploadQuery } from './upload.schema'
import { appConfig, APP_CONSTANTS } from '../../../config/app-config'
import { logger, loggerHelpers } from '../../../logger'
import { ErrorFactory } from '../../../errors/custom-errors'
import { FileUploadHelper } from '../../../utils/file-upload-handler'

export type { FilesUpload, FilesUploadData, FilesUploadPatch, FilesUploadQuery }

export interface FilesUploadServiceOptions {
  app: Application
}

export interface FilesUploadParams extends Params<FilesUploadQuery> {}

// In-memory storage for upload metadata (in production, use database)
const uploadStore = new Map<string, FilesUpload>()

export class FilesUploadService<ServiceParams extends FilesUploadParams = FilesUploadParams>
  implements ServiceInterface<FilesUpload, FilesUploadData, ServiceParams, FilesUploadPatch>
{
  constructor(public options: FilesUploadServiceOptions) {
    // Start cleanup interval for expired uploads
    this.startCleanupInterval()
  }

  /**
   * Find uploads with optional filtering
   */
  async find(params?: ServiceParams): Promise<FilesUpload[]> {
    const logContext = {
      service: 'FilesUploadService',
      method: 'find',
      metadata: { params }
    }

    try {
      const query = params?.query || {}
      let uploads = Array.from(uploadStore.values())

      // Filter by status
      if (query.status) {
        uploads = uploads.filter(upload => upload.status === query.status)
      }

      // Filter by file extension
      if (query.fileExtension) {
        uploads = uploads.filter(upload => upload.fileExtension === query.fileExtension)
      }

      // Filter by MIME type
      if (query.mimeType) {
        uploads = uploads.filter(upload => upload.mimeType === query.mimeType)
      }

      // Filter by date range
      if (query.uploadedAfter) {
        const afterDate = new Date(query.uploadedAfter)
        uploads = uploads.filter(upload => new Date(upload.uploadedAt) > afterDate)
      }

      if (query.uploadedBefore) {
        const beforeDate = new Date(query.uploadedBefore)
        uploads = uploads.filter(upload => new Date(upload.uploadedAt) < beforeDate)
      }

      // Exclude expired uploads unless specifically requested
      if (!query.includeExpired) {
        const now = new Date()
        uploads = uploads.filter(upload => new Date(upload.expiresAt) > now)
      }

      logger.info('Uploads found', {
        ...logContext,
        metadata: { ...logContext.metadata, count: uploads.length }
      })

      return uploads
    } catch (error) {
      loggerHelpers.logError('Failed to find uploads', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('find', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Get a specific upload by ID
   */
  async get(id: Id, params?: ServiceParams): Promise<FilesUpload> {
    const logContext = {
      service: 'FilesUploadService',
      method: 'get',
      metadata: { id }
    }

    try {
      const upload = uploadStore.get(String(id))
      
      if (!upload) {
        logger.warn('Upload not found', logContext)
        throw new NotFound(`Upload with id ${id} not found`)
      }

      // Check if upload has expired
      if (new Date(upload.expiresAt) <= new Date()) {
        logger.warn('Upload has expired', logContext)
        throw new NotFound(`Upload with id ${id} has expired`)
      }

      logger.info('Upload retrieved', logContext)
      return upload
    } catch (error) {
      loggerHelpers.logError('Failed to get upload', error as Error, logContext)
      throw error
    }
  }

  /**
   * Create a new upload from multipart form data
   */
  async create(data: FilesUploadData, params?: ServiceParams): Promise<FilesUpload> {
    const logContext = {
      service: 'FilesUploadService',
      method: 'create',
      metadata: { data }
    }

    try {
      // Extract file from Koa context
      const ctx = (params as any)?.request?.ctx
      if (!ctx || !ctx.request || !ctx.request.files || !ctx.request.files.file) {
        throw ErrorFactory.fileUpload.noFileProvided(logContext.metadata)
      }

      const file = ctx.request.files.file
      
      // Validate file using validation service
      const validationService = this.options.app.service('validation')
      const validationResult = await validationService.create({
        type: 'file',
        data: file,
        options: {
          maxFileSize: appConfig.get('fileUpload.maxSize', APP_CONSTANTS.FILE_UPLOAD.MAX_FILE_SIZE),
          allowedExtensions: [...appConfig.get('fileUpload.allowedExtensions', APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_EXTENSIONS)],
          allowedMimeTypes: [...APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_MIME_TYPES],
          deepValidation: appConfig.get('fileUpload.deepValidation', true)
        }
      }) as any

      if (!validationResult.isValid) {
        // Check if it's a format issue vs missing file
        const hasFormatError = validationResult.errors?.some((error: any) =>
          error.includes('extension') || error.includes('format') || error.includes('allowed')
        )

        if (hasFormatError) {
          const fileExtension = path.extname(file.originalFilename || '').toLowerCase()
          const { BadRequest } = require('@feathersjs/errors')
          throw new BadRequest(
            `Unsupported file format: ${fileExtension}. Supported formats: ${APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_EXTENSIONS.join(', ')}`,
            logContext.metadata
          )
        } else {
          throw ErrorFactory.fileUpload.noFileProvided({
            ...logContext.metadata,
            validationErrors: validationResult.errors
          })
        }
      }

      // Generate unique ID for upload
      const uploadId = crypto.randomUUID()

      // Create temporary file
      const sanitizedFilename = FileUploadHelper.sanitizeFileName(file.originalFilename || file.name || 'unknown')
      const tempFilePath = FileUploadHelper.createTempFilePath(sanitizedFilename)

      // Write file to temporary location
      await FileUploadHelper.writeFileToTemp(file, tempFilePath)

      // Calculate expiration time
      const retentionPeriod = appConfig.get('fileUpload.retentionPeriod', APP_CONSTANTS.FILE_UPLOAD.CLEANUP_TIMEOUT)
      const expiresAt = new Date(Date.now() + retentionPeriod)

      // Create upload record
      const upload: FilesUpload = {
        id: uploadId,
        originalFilename: file.originalFilename || file.name || 'unknown',
        sanitizedFilename: sanitizedFilename,
        fileSize: file.size || 0,
        mimeType: file.mimetype,
        fileExtension: path.extname(sanitizedFilename).toLowerCase(),
        tempPath: tempFilePath,
        uploadedAt: new Date().toISOString(),
        status: 'uploaded',
        expiresAt: expiresAt.toISOString()
      }

      // Store upload metadata
      uploadStore.set(uploadId, upload)

      loggerHelpers.logSuccess('File uploaded successfully', undefined, {
        ...logContext,
        metadata: {
          ...logContext.metadata,
          uploadId,
          filename: upload.originalFilename,
          size: upload.fileSize
        }
      })

      return upload
    } catch (error) {
      loggerHelpers.logError('Failed to create upload', error as Error, logContext)
      throw error
    }
  }

  /**
   * Update upload status or metadata
   */
  async patch(id: NullableId, data: FilesUploadPatch, params?: ServiceParams): Promise<FilesUpload> {
    const logContext = {
      service: 'FilesUploadService',
      method: 'patch',
      metadata: { id, data }
    }

    try {
      const upload = await this.get(id as Id, params)
      
      // Update allowed fields
      if (data.status) {
        upload.status = data.status
      }
      
      if (data.metadata) {
        upload.metadata = { ...upload.metadata, ...data.metadata }
      }

      // Save updated upload
      uploadStore.set(upload.id, upload)

      logger.info('Upload updated', logContext)
      return upload
    } catch (error) {
      loggerHelpers.logError('Failed to update upload', error as Error, logContext)
      throw error
    }
  }

  /**
   * Remove an upload and cleanup associated files
   */
  async remove(id: NullableId, params?: ServiceParams): Promise<FilesUpload> {
    const logContext = {
      service: 'FilesUploadService',
      method: 'remove',
      metadata: { id }
    }

    try {
      const upload = await this.get(id as Id, params)
      
      // Remove from store
      uploadStore.delete(upload.id)

      // Cleanup temporary file
      try {
        const fs = require('fs').promises
        await fs.unlink(upload.tempPath)
      } catch (cleanupError) {
        logger.warn('Failed to cleanup temporary file', {
          ...logContext,
          metadata: { ...logContext.metadata, tempPath: upload.tempPath, error: cleanupError }
        })
      }

      logger.info('Upload removed', logContext)
      return upload
    } catch (error) {
      loggerHelpers.logError('Failed to remove upload', error as Error, logContext)
      throw error
    }
  }

  /**
   * Start cleanup interval for expired uploads
   */
  private startCleanupInterval(): void {
    const cleanupInterval = 5 * 60 * 1000 // 5 minutes
    
    setInterval(async () => {
      try {
        const now = new Date()
        const expiredUploads = Array.from(uploadStore.values())
          .filter(upload => new Date(upload.expiresAt) <= now)

        for (const upload of expiredUploads) {
          await this.remove(upload.id)
        }

        if (expiredUploads.length > 0) {
          logger.info('Cleaned up expired uploads', {
            service: 'FilesUploadService',
            metadata: { cleanedCount: expiredUploads.length }
          })
        }
      } catch (error) {
        loggerHelpers.logError('Failed to cleanup expired uploads', error as Error, {
          service: 'FilesUploadService'
        })
      }
    }, cleanupInterval)
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
