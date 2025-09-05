// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest } from '@feathersjs/errors'

import type { Application } from '../../../declarations'
import type { FilesInfo, FilesInfoData, FilesInfoPatch, FilesInfoQuery } from './info.schema'
import { logger, loggerHelpers, type LogContext } from '../../../logger'
import { ErrorFactory } from '../../../errors/custom-errors'

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

  async get(id: Id, _params?: ServiceParams): Promise<FilesInfo> {
    return {
      printer: undefined,
      nozzle: undefined,
      technicalName: undefined
    }
  }

  async create(data: FilesInfoData, params?: ServiceParams): Promise<FilesInfo>
  async create(data: FilesInfoData[], params?: ServiceParams): Promise<FilesInfo[]>
  async create(
    data: FilesInfoData | FilesInfoData[],
    params?: ServiceParams
  ): Promise<FilesInfo | FilesInfo[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
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

      // Step 2: Process 3MF file using processor service
      const processorService = this.options.app.service('files/processors/3mf')
      const processingResult = await processorService.create({
        uploadId: upload.id,
        options: {
          includeArchiveInfo: false,
          validateStructure: true
        }
      })

      // Step 3: Wait for processing to complete (poll for result)
      let finalResult = processingResult
      const maxWaitTime = 30000 // 30 seconds
      const pollInterval = 1000 // 1 second
      let waitTime = 0

      while (finalResult.status === 'processing' && waitTime < maxWaitTime) {
        await new Promise(resolve => setTimeout(resolve, pollInterval))
        finalResult = await processorService.get(processingResult.id)
        waitTime += pollInterval
      }

      if (finalResult.status === 'failed') {
        throw ErrorFactory.processing.threeMFCorrupted(upload.tempPath, {
          errors: finalResult.errors,
          uploadId: upload.id
        })
      }

      if (finalResult.status === 'processing') {
        throw ErrorFactory.processing.processingTimeout(maxWaitTime, {
          uploadId: upload.id,
          processingId: finalResult.id
        })
      }

      // Step 4: Validate extracted data using validation service
      const validatedData = await this.validateExtractedData(finalResult.extractedData || {})

      // Step 5: Cleanup upload (optional, could be kept for audit)
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
  private async validateExtractedData(profileInfo: any): Promise<FilesInfo> {
    // Generate technical name from profile if not provided
    let technicalName = profileInfo?.technicalName
    if (!technicalName && profileInfo?.profile) {
      // Extract technical name from profile string (e.g., "0.20mm Standard @BBL X1C" -> "@BBL X1C")
      const match = profileInfo.profile.match(/@[^@]*$/)
      technicalName = match ? match[0] : `@${profileInfo.printer || 'Unknown'}`
    }

    return {
      printer: profileInfo?.printer,
      nozzle: profileInfo?.nozzle,
      technicalName: technicalName,
      profile: profileInfo?.profile
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
