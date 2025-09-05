// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest, NotFound } from '@feathersjs/errors'
import * as crypto from 'crypto'

import type { Application } from '../../declarations'
import type { Validation, ValidationData, ValidationPatch, ValidationQuery } from './validation.schema'
import { logger, loggerHelpers } from '../../logger'
import { ErrorFactory } from '../../errors/custom-errors'
import { InputValidator } from '../../utils/input-validator'
import { SecurityValidator } from '../../utils/security-validator'

export type { Validation, ValidationData, ValidationPatch, ValidationQuery }

export interface ValidationServiceOptions {
  app: Application
}

export interface ValidationParams extends Params<ValidationQuery> {}

// In-memory storage for validation results (in production, use database or cache)
const validationStore = new Map<string, Validation>()

export class ValidationService<ServiceParams extends ValidationParams = ValidationParams>
  implements ServiceInterface<Validation, ValidationData, ServiceParams, ValidationPatch>
{
  private securityValidator: SecurityValidator

  constructor(public options: ValidationServiceOptions) {
    this.securityValidator = new SecurityValidator()
  }

  /**
   * Find validation results with optional filtering
   */
  async find(params?: ServiceParams): Promise<Validation[]> {
    const logContext = {
      service: 'ValidationService',
      method: 'find',
      metadata: { params }
    }

    try {
      const query = params?.query || {}
      let validations = Array.from(validationStore.values())

      // Filter by type
      if (query.type) {
        validations = validations.filter(validation => validation.type === query.type)
      }

      // Filter by validity
      if (typeof query.isValid === 'boolean') {
        validations = validations.filter(validation => validation.isValid === query.isValid)
      }

      // Filter by date range
      if (query.validatedAfter) {
        const afterDate = new Date(query.validatedAfter)
        validations = validations.filter(validation => new Date(validation.validatedAt) > afterDate)
      }

      if (query.validatedBefore) {
        const beforeDate = new Date(query.validatedBefore)
        validations = validations.filter(validation => new Date(validation.validatedAt) < beforeDate)
      }

      // Filter by errors/warnings
      if (query.hasErrors) {
        validations = validations.filter(validation => validation.errors.length > 0)
      }

      if (query.hasWarnings) {
        validations = validations.filter(validation => 
          validation.warnings && validation.warnings.length > 0
        )
      }

      logger.info('Validations found', {
        ...logContext,
        metadata: { ...logContext.metadata, count: validations.length }
      })

      return validations
    } catch (error) {
      loggerHelpers.logError('Failed to find validations', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('find', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Get a specific validation result by ID
   */
  async get(id: Id, params?: ServiceParams): Promise<Validation> {
    const logContext = {
      service: 'ValidationService',
      method: 'get',
      metadata: { id }
    }

    try {
      const validation = validationStore.get(String(id))
      
      if (!validation) {
        logger.warn('Validation not found', logContext)
        throw new NotFound(`Validation with id ${id} not found`)
      }

      logger.info('Validation retrieved', logContext)
      return validation
    } catch (error) {
      loggerHelpers.logError('Failed to get validation', error as Error, logContext)
      throw error
    }
  }

  /**
   * Create a new validation
   */
  async create(data: ValidationData, params?: ServiceParams): Promise<Validation> {
    const logContext = {
      service: 'ValidationService',
      method: 'create',
      metadata: { type: data.type }
    }

    try {
      const validationId = crypto.randomUUID()
      const validatedAt = new Date().toISOString()

      let result: Validation

      switch (data.type) {
        case 'file':
          result = await this.validateFile(validationId, data.data, data.options || {}, validatedAt)
          break
        case 'string':
          result = await this.validateString(validationId, data.data, data.options || {}, validatedAt)
          break
        case 'path':
          result = await this.validatePath(validationId, data.data, data.options || {}, validatedAt)
          break
        case 'nozzle':
          result = await this.validateNozzle(validationId, data.data, validatedAt)
          break
        case 'printer':
          result = await this.validatePrinter(validationId, data.data, validatedAt)
          break
        case 'technical-name':
          result = await this.validateTechnicalName(validationId, data.data, validatedAt)
          break
        default:
          throw new BadRequest(`Unsupported validation type: ${data.type}`)
      }

      // Store validation result
      validationStore.set(validationId, result)

      logger.info('Validation completed', {
        ...logContext,
        metadata: {
          ...logContext.metadata,
          validationId,
          isValid: result.isValid,
          errorCount: result.errors.length
        }
      })

      return result
    } catch (error) {
      loggerHelpers.logError('Failed to create validation', error as Error, logContext)
      throw error
    }
  }

  /**
   * Update validation metadata
   */
  async patch(id: NullableId, data: ValidationPatch, params?: ServiceParams): Promise<Validation> {
    const logContext = {
      service: 'ValidationService',
      method: 'patch',
      metadata: { id, data }
    }

    try {
      const validation = await this.get(id as Id, params)
      
      // Update metadata
      if (data.metadata) {
        validation.metadata = { ...validation.metadata, ...data.metadata }
      }

      // Save updated validation
      validationStore.set(validation.id, validation)

      logger.info('Validation updated', logContext)
      return validation
    } catch (error) {
      loggerHelpers.logError('Failed to update validation', error as Error, logContext)
      throw error
    }
  }

  /**
   * Remove a validation result
   */
  async remove(id: NullableId, params?: ServiceParams): Promise<Validation> {
    const logContext = {
      service: 'ValidationService',
      method: 'remove',
      metadata: { id }
    }

    try {
      const validation = await this.get(id as Id, params)
      
      // Remove from store
      validationStore.delete(validation.id)

      logger.info('Validation removed', logContext)
      return validation
    } catch (error) {
      loggerHelpers.logError('Failed to remove validation', error as Error, logContext)
      throw error
    }
  }

  /**
   * Validate a file
   */
  private async validateFile(id: string, file: any, options: any, validatedAt: string): Promise<Validation> {
    try {
      const securityResult = await this.securityValidator.validateFile(file)
      
      return {
        id,
        type: 'file',
        isValid: securityResult.isValid,
        errors: securityResult.issues,
        warnings: [],
        sanitizedValue: file,
        originalValue: file,
        validatedAt,
        sanitizedFilename: securityResult.sanitizedFilename,
        detectedType: securityResult.detectedType,
        metadata: {
          fileSize: file.size,
          mimeType: file.mimetype,
          originalFilename: file.originalFilename || file.name
        }
      }
    } catch (error) {
      return {
        id,
        type: 'file',
        isValid: false,
        errors: [`File validation failed: ${(error as Error).message}`],
        validatedAt,
        originalValue: file
      }
    }
  }

  /**
   * Validate a string
   */
  private async validateString(id: string, value: string, options: any, validatedAt: string): Promise<Validation> {
    const result = InputValidator.validateString(value, options)
    
    return {
      id,
      type: 'string',
      isValid: result.isValid,
      errors: result.errors,
      warnings: [],
      sanitizedValue: result.sanitizedValue,
      originalValue: value,
      validatedAt
    }
  }

  /**
   * Validate a path
   */
  private async validatePath(id: string, value: string, options: any, validatedAt: string): Promise<Validation> {
    const result = InputValidator.validatePath(value, options)
    
    return {
      id,
      type: 'path',
      isValid: result.isValid,
      errors: result.errors,
      warnings: [],
      sanitizedValue: result.sanitizedValue,
      originalValue: value,
      validatedAt
    }
  }

  /**
   * Validate a nozzle diameter
   */
  private async validateNozzle(id: string, value: string, validatedAt: string): Promise<Validation> {
    const result = InputValidator.validateNozzleDiameter(value)
    
    return {
      id,
      type: 'nozzle',
      isValid: result.isValid,
      errors: result.errors,
      warnings: [],
      sanitizedValue: result.sanitizedValue,
      originalValue: value,
      validatedAt
    }
  }

  /**
   * Validate a printer model
   */
  private async validatePrinter(id: string, value: string, validatedAt: string): Promise<Validation> {
    const result = InputValidator.validatePrinterModel(value)
    
    return {
      id,
      type: 'printer',
      isValid: result.isValid,
      errors: result.errors,
      warnings: [],
      sanitizedValue: result.sanitizedValue,
      originalValue: value,
      validatedAt
    }
  }

  /**
   * Validate a technical name
   */
  private async validateTechnicalName(id: string, value: string, validatedAt: string): Promise<Validation> {
    const result = InputValidator.validateTechnicalName(value)
    
    return {
      id,
      type: 'technical-name',
      isValid: result.isValid,
      errors: result.errors,
      warnings: result.errors.filter(error => error.includes('should typically contain')), // Convert some errors to warnings
      sanitizedValue: result.sanitizedValue,
      originalValue: value,
      validatedAt
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
