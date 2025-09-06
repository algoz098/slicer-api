import { FeathersError, BadRequest } from '@feathersjs/errors'

/**
 * Base class for application-specific errors
 */
export abstract class AppError extends FeathersError {
  public readonly isOperational: boolean = true
  public readonly context?: Record<string, any>

  constructor(
    message: string,
    name: string,
    code: number,
    className: string,
    data?: any,
    context?: Record<string, any>
  ) {
    super(message, name, code, className, data)
    this.context = context
    
    // Ensure the name of this error is the same as the class name
    this.name = this.constructor.name
    
    // This clips the constructor invocation from the stack trace
    Error.captureStackTrace(this, this.constructor)
  }
}

/**
 * File upload related errors
 */
export class FileUploadError extends AppError {
  constructor(
    message: string,
    data?: any,
    context?: Record<string, any>
  ) {
    super(message, 'FileUploadError', 400, 'bad-request', data, context)
  }
}

export class FileSizeExceededError extends FileUploadError {
  constructor(
    actualSize: number,
    maxSize: number,
    context?: Record<string, any>
  ) {
    const message = `File size ${actualSize} bytes exceeds maximum allowed size of ${maxSize} bytes`
    super(message, { actualSize, maxSize }, context)
  }
}

export class UnsupportedFileFormatError extends FileUploadError {
  constructor(
    fileExtension: string,
    supportedFormats: string[],
    context?: Record<string, any>
  ) {
    const message = `Unsupported file format: ${fileExtension}. Supported formats: ${supportedFormats.join(', ')}`
    super(message, { fileExtension, supportedFormats }, context)
  }
}

export class InvalidMimeTypeError extends FileUploadError {
  constructor(
    mimeType: string,
    supportedTypes: string[],
    context?: Record<string, any>
  ) {
    const message = `Unsupported MIME type: ${mimeType}. Supported types: ${supportedTypes.join(', ')}`
    super(message, { mimeType, supportedTypes }, context)
  }
}

/**
 * File processing related errors
 */
export class FileProcessingError extends AppError {
  constructor(
    message: string,
    data?: any,
    context?: Record<string, any>
  ) {
    super(message, 'FileProcessingError', 422, 'unprocessable-entity', data, context)
  }
}

export class ThreeMFProcessingError extends FileProcessingError {
  constructor(
    message: string,
    filePath?: string,
    context?: Record<string, any>
  ) {
    super(message, { filePath }, context)
  }
}

export class MetadataExtractionError extends FileProcessingError {
  constructor(
    message: string,
    fileName?: string,
    context?: Record<string, any>
  ) {
    super(message, { fileName }, context)
  }
}

/**
 * Validation related errors
 */
export class ValidationError extends AppError {
  constructor(
    message: string,
    field: string,
    value: any,
    constraint: string,
    context?: Record<string, any>
  ) {
    super(
      message,
      'ValidationError',
      400,
      'bad-request',
      { field, value, constraint },
      context
    )
  }
}

export class FileNameValidationError extends ValidationError {
  constructor(
    fileName: string,
    reason: string,
    context?: Record<string, any>
  ) {
    super(
      `Invalid filename: ${fileName}. Reason: ${reason}`,
      'fileName',
      fileName,
      reason,
      context
    )
  }
}

/**
 * Service operation errors
 */
export class ServiceOperationError extends AppError {
  constructor(
    message: string,
    operation: string,
    data?: any,
    context?: Record<string, any>
  ) {
    super(message, 'ServiceOperationError', 500, 'general-error', { operation, ...data }, context)
  }
}

export class ProfileNotFoundError extends AppError {
  constructor(
    profileId: string,
    context?: Record<string, any>
  ) {
    super(
      `Profile not found: ${profileId}`,
      'ProfileNotFoundError',
      404,
      'not-found',
      { profileId },
      context
    )
  }
}

/**
 * Configuration and system errors
 */
export class ConfigurationError extends AppError {
  constructor(
    message: string,
    configKey?: string,
    context?: Record<string, any>
  ) {
    super(
      message,
      'ConfigurationError',
      500,
      'general-error',
      { configKey },
      context
    )
  }
}

export class TemporaryFileError extends AppError {
  constructor(
    message: string,
    filePath?: string,
    context?: Record<string, any>
  ) {
    super(
      message,
      'TemporaryFileError',
      500,
      'general-error',
      { filePath },
      context
    )
  }
}

/**
 * Error factory functions for common scenarios
 */
export const ErrorFactory = {
  fileUpload: {
    noFileProvided: (context?: Record<string, any>) =>
      new BadRequest('No file provided', context),

    emptyFile: (context?: Record<string, any>) =>
      new BadRequest('Uploaded file appears to be empty', context),
    
    sizeExceeded: (actualSize: number, maxSize: number, context?: Record<string, any>) =>
      new FileSizeExceededError(actualSize, maxSize, context),
    
    unsupportedFormat: (extension: string, supported: string[], context?: Record<string, any>) =>
      new UnsupportedFileFormatError(extension, supported, context),
    
    invalidMimeType: (mimeType: string, supported: string[], context?: Record<string, any>) =>
      new InvalidMimeTypeError(mimeType, supported, context)
  },

  processing: {
    threeMFCorrupted: (filePath: string, context?: Record<string, any>) =>
      new ThreeMFProcessingError(`Failed to open 3MF file: file appears to be corrupted`, filePath, context),
    
    metadataNotFound: (fileName: string, context?: Record<string, any>) =>
      new MetadataExtractionError(`No extractable metadata found in file`, fileName, context),
    
    processingTimeout: (timeout: number, context?: Record<string, any>) =>
      new FileProcessingError(`File processing timed out after ${timeout}ms`, { timeout }, context)
  },

  validation: {
    invalidFileName: (fileName: string, reason: string, context?: Record<string, any>) =>
      new FileNameValidationError(fileName, reason, context),

    invalidNozzleDiameter: (value: string, context?: Record<string, any>) =>
      new ValidationError(
        `Invalid nozzle diameter format: ${value}`,
        'nozzle',
        value,
        'Must be a decimal number (e.g., "0.4")',
        context
      ),

    invalidParameter: (paramName: string, reason: string, context?: Record<string, any>) =>
      new ValidationError(
        `Invalid parameter ${paramName}: ${reason}`,
        paramName,
        undefined,
        reason,
        context
      )
  },

  service: {
    profileNotFound: (id: string, context?: Record<string, any>) =>
      new ProfileNotFoundError(id, context),
    
    operationFailed: (operation: string, reason: string, context?: Record<string, any>) =>
      new ServiceOperationError(`${operation} failed: ${reason}`, operation, { reason }, context)
  }
}

/**
 * Type guards for error checking
 */
export const isAppError = (error: any): error is AppError => {
  return error instanceof AppError
}

export const isFileUploadError = (error: any): error is FileUploadError => {
  return error instanceof FileUploadError
}

export const isFileProcessingError = (error: any): error is FileProcessingError => {
  return error instanceof FileProcessingError
}

export const isValidationError = (error: any): error is ValidationError => {
  return error instanceof ValidationError
}
