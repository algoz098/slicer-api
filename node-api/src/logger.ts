// For more information about this file see https://dove.feathersjs.com/guides/cli/logging.html
import { createLogger, format, transports } from 'winston'

import type { HookContext } from './declarations'
import { isAppError } from './errors/custom-errors'

export interface LogContext {
  /** Request ID for tracing */
  requestId?: string
  /** User ID if available */
  userId?: string
  /** Service name */
  service?: string
  /** Method being called */
  method?: string
  /** Additional metadata */
  metadata?: Record<string, any>
}

export interface ErrorLogContext extends LogContext {
  /** Error stack trace */
  stack?: string
  /** Error code */
  errorCode?: string | number
  /** Error data */
  errorData?: any
}

// Configure the Winston logger. For the complete documentation see https://github.com/winstonjs/winston
export const logger = createLogger({
  // To see more detailed errors, change this to 'debug'
  level: process.env.LOG_LEVEL || (process.env.NODE_ENV === 'test' ? 'error' : 'info'),
  format: format.combine(
    format.timestamp(),
    format.errors({ stack: true }),
    format.splat(),
    format.json(),
    format.printf(({ timestamp, level, message, ...meta }) => {
      const logEntry = {
        timestamp,
        level,
        message,
        ...meta
      }
      return JSON.stringify(logEntry)
    })
  ),
  transports: process.env.NODE_ENV === 'test' ? [
    // Silent transport for tests to avoid Winston warnings
    new transports.Console({
      silent: true
    })
  ] : [
    new transports.Console({
      format: format.combine(
        format.colorize(),
        format.simple(),
        format.printf(({ timestamp, level, message, ...meta }) => {
          const metaStr = Object.keys(meta).length > 0 ? ` ${JSON.stringify(meta)}` : ''
          return `${timestamp} [${level}]: ${message}${metaStr}`
        })
      )
    })
  ]
})

// Add file transport in production (but not in test)
if (process.env.NODE_ENV === 'production') {
  logger.add(new transports.File({
    filename: 'logs/error.log',
    level: 'error',
    maxsize: 5242880, // 5MB
    maxFiles: 5
  }))

  logger.add(new transports.File({
    filename: 'logs/combined.log',
    maxsize: 5242880, // 5MB
    maxFiles: 5
  }))
}

/**
 * Enhanced logging functions for FeathersJS
 */
export const loggerHelpers = {
  /**
   * Create a logger context from a Feathers hook context
   */
  createContextFromHook(context: HookContext): LogContext {
    return {
      requestId: generateRequestId(),
      service: context.path,
      method: context.method,
      userId: context.params?.user?.id,
      metadata: {
        type: context.type,
        id: context.id,
        params: sanitizeParams(context.params)
      }
    }
  },

  /**
   * Log error with enhanced context
   */
  logError(message: string, error?: Error, context?: ErrorLogContext): void {
    const errorContext: ErrorLogContext = {
      ...context,
      stack: error?.stack,
      errorCode: isAppError(error) ? error.code : undefined,
      errorData: isAppError(error) ? error.data : undefined
    }

    logger.error(message, errorContext)
  },

  /**
   * Log file upload events
   */
  logFileUpload(fileName: string, fileSize: number, context?: LogContext): void {
    logger.info('File upload started', {
      ...context,
      metadata: {
        ...context?.metadata,
        fileName,
        fileSize,
        event: 'file_upload_start'
      }
    })
  },

  /**
   * Log file processing events
   */
  logFileProcessing(fileName: string, processingType: string, context?: LogContext): void {
    logger.info('File processing started', {
      ...context,
      metadata: {
        ...context?.metadata,
        fileName,
        processingType,
        event: 'file_processing_start'
      }
    })
  },

  /**
   * Log successful operations
   */
  logSuccess(operation: string, duration?: number, context?: LogContext): void {
    logger.info(`Operation completed successfully: ${operation}`, {
      ...context,
      metadata: {
        ...context?.metadata,
        operation,
        duration,
        event: 'operation_success'
      }
    })
  },

  /**
   * Log performance metrics
   */
  logPerformance(operation: string, duration: number, context?: LogContext): void {
    const level = duration > 5000 ? 'warn' : 'info' // Warn if operation takes more than 5 seconds

    logger.log(level, `Performance: ${operation} took ${duration}ms`, {
      ...context,
      metadata: {
        ...context?.metadata,
        operation,
        duration,
        event: 'performance_metric'
      }
    })
  },

  /**
   * Log security events
   */
  logSecurityEvent(event: string, severity: 'low' | 'medium' | 'high', context?: LogContext): void {
    const level = severity === 'high' ? 'error' : severity === 'medium' ? 'warn' : 'info'

    logger.log(level, `Security event: ${event}`, {
      ...context,
      metadata: {
        ...context?.metadata,
        securityEvent: event,
        severity,
        event: 'security_event'
      }
    })
  }
}

/**
 * Generate a unique request ID
 */
function generateRequestId(): string {
  return `req_${Date.now()}_${Math.random().toString(36).substring(2, 15)}`
}

/**
 * Sanitize parameters to remove sensitive information
 */
function sanitizeParams(params: any): any {
  if (!params) return params

  const sanitized = { ...params }

  // Remove sensitive fields
  const sensitiveFields = ['password', 'token', 'authorization', 'cookie']
  sensitiveFields.forEach(field => {
    if (sanitized[field]) {
      sanitized[field] = '[REDACTED]'
    }
  })

  // Sanitize headers
  if (sanitized.headers) {
    const sanitizedHeaders = { ...sanitized.headers }
    sensitiveFields.forEach(field => {
      if (sanitizedHeaders[field]) {
        sanitizedHeaders[field] = '[REDACTED]'
      }
    })
    sanitized.headers = sanitizedHeaders
  }

  return sanitized
}
