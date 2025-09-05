import * as path from 'path'
import { APP_CONSTANTS } from '../config/app-config'
import { ErrorFactory } from '../errors/custom-errors'

export interface ValidationResult {
  isValid: boolean
  sanitizedValue?: any
  errors: string[]
}

export interface PathValidationOptions {
  allowAbsolute?: boolean
  allowRelative?: boolean
  maxLength?: number
  allowedExtensions?: string[]
}

export interface StringValidationOptions {
  minLength?: number
  maxLength?: number
  pattern?: RegExp
  allowEmpty?: boolean
  trim?: boolean
}

export class InputValidator {
  /**
   * Validates and sanitizes a filename
   */
  static validateFilename(filename: string): ValidationResult {
    const errors: string[] = []
    
    if (!filename || typeof filename !== 'string') {
      return {
        isValid: false,
        errors: ['Filename is required and must be a string']
      }
    }

    const originalFilename = filename
    let sanitized = filename.trim()

    // Check for empty filename after trimming
    if (sanitized.length === 0) {
      return {
        isValid: false,
        sanitizedValue: 'uploaded_file',
        errors: ['Filename cannot be empty']
      }
    }

    // Check for path traversal attempts
    if (APP_CONSTANTS.SECURITY.PATH_TRAVERSAL_PATTERN.test(sanitized)) {
      errors.push('Filename contains path traversal sequences')
      sanitized = sanitized.replace(APP_CONSTANTS.SECURITY.PATH_TRAVERSAL_PATTERN, '_')
    }

    // Check for dangerous characters
    if (APP_CONSTANTS.SECURITY.DANGEROUS_FILENAME_CHARS.test(sanitized)) {
      errors.push('Filename contains dangerous characters')
      sanitized = sanitized.replace(APP_CONSTANTS.SECURITY.DANGEROUS_FILENAME_CHARS, '_')
    }

    // Check for hidden files
    if (APP_CONSTANTS.SECURITY.HIDDEN_FILE_PATTERN.test(sanitized)) {
      errors.push('Hidden files are not allowed')
      sanitized = sanitized.replace(/^\.+/, '_')
    }

    // Remove path separators
    if (/[/\\]/.test(sanitized)) {
      errors.push('Filename cannot contain path separators')
      sanitized = sanitized.replace(/[/\\]/g, '_')
    }

    // Check length
    if (sanitized.length > APP_CONSTANTS.FILE_UPLOAD.MAX_FILENAME_LENGTH) {
      errors.push(`Filename too long (max ${APP_CONSTANTS.FILE_UPLOAD.MAX_FILENAME_LENGTH} characters)`)
      const ext = path.extname(sanitized)
      const base = path.basename(sanitized, ext)
      sanitized = base.substring(0, APP_CONSTANTS.FILE_UPLOAD.MAX_FILENAME_LENGTH - ext.length) + ext
    }

    // Ensure filename is not empty after sanitization
    if (!sanitized || sanitized.length === 0) {
      sanitized = 'uploaded_file'
    }

    return {
      isValid: errors.length === 0,
      sanitizedValue: sanitized,
      errors
    }
  }

  /**
   * Validates a file path for security issues
   */
  static validatePath(filePath: string, options: PathValidationOptions = {}): ValidationResult {
    const errors: string[] = []
    
    if (!filePath || typeof filePath !== 'string') {
      return {
        isValid: false,
        errors: ['Path is required and must be a string']
      }
    }

    const {
      allowAbsolute = false,
      allowRelative = true,
      maxLength = 1000,
      allowedExtensions
    } = options

    let sanitized = filePath.trim()

    // Check for empty path
    if (sanitized.length === 0) {
      return {
        isValid: false,
        errors: ['Path cannot be empty']
      }
    }

    // Check path length
    if (sanitized.length > maxLength) {
      errors.push(`Path too long (max ${maxLength} characters)`)
    }

    // Check for path traversal
    if (sanitized.includes('..')) {
      errors.push('Path contains directory traversal sequences')
    }

    // Check for null bytes
    if (sanitized.includes('\0')) {
      errors.push('Path contains null bytes')
    }

    // Validate absolute vs relative paths
    if (path.isAbsolute(sanitized)) {
      if (!allowAbsolute) {
        errors.push('Absolute paths are not allowed')
      }
    } else {
      if (!allowRelative) {
        errors.push('Relative paths are not allowed')
      }
    }

    // Validate file extension if specified
    if (allowedExtensions && allowedExtensions.length > 0) {
      const ext = path.extname(sanitized).toLowerCase()
      if (!allowedExtensions.includes(ext)) {
        errors.push(`File extension '${ext}' is not allowed`)
      }
    }

    // Normalize path separators
    sanitized = path.normalize(sanitized)

    return {
      isValid: errors.length === 0,
      sanitizedValue: sanitized,
      errors
    }
  }

  /**
   * Validates a string input with various options
   */
  static validateString(input: string, options: StringValidationOptions = {}): ValidationResult {
    const errors: string[] = []
    
    if (input === null || input === undefined) {
      if (options.allowEmpty) {
        return {
          isValid: true,
          sanitizedValue: '',
          errors: []
        }
      } else {
        return {
          isValid: false,
          errors: ['String is required']
        }
      }
    }

    if (typeof input !== 'string') {
      return {
        isValid: false,
        errors: ['Input must be a string']
      }
    }

    const {
      minLength = 0,
      maxLength = 1000,
      pattern,
      allowEmpty = false,
      trim = true
    } = options

    let sanitized = trim ? input.trim() : input

    // Check for empty string
    if (sanitized.length === 0 && !allowEmpty) {
      return {
        isValid: false,
        sanitizedValue: sanitized,
        errors: ['String cannot be empty']
      }
    }

    // Check length constraints
    if (sanitized.length < minLength) {
      errors.push(`String too short (min ${minLength} characters)`)
    }

    if (sanitized.length > maxLength) {
      errors.push(`String too long (max ${maxLength} characters)`)
      sanitized = sanitized.substring(0, maxLength)
    }

    // Check pattern if provided
    if (pattern && !pattern.test(sanitized)) {
      errors.push('String does not match required pattern')
    }

    return {
      isValid: errors.length === 0,
      sanitizedValue: sanitized,
      errors
    }
  }

  /**
   * Validates a nozzle diameter value
   */
  static validateNozzleDiameter(value: string): ValidationResult {
    const errors: string[] = []
    
    if (!value || typeof value !== 'string') {
      return {
        isValid: false,
        errors: ['Nozzle diameter is required and must be a string']
      }
    }

    const sanitized = value.trim()

    // Check format (should be decimal number)
    const nozzlePattern = /^[0-9]+\.[0-9]+$/
    if (!nozzlePattern.test(sanitized)) {
      errors.push('Nozzle diameter must be a decimal number (e.g., "0.4", "0.6")')
    }

    // Check reasonable range (0.1mm to 2.0mm)
    const numValue = parseFloat(sanitized)
    if (!isNaN(numValue)) {
      if (numValue < 0.1 || numValue > 2.0) {
        errors.push('Nozzle diameter must be between 0.1 and 2.0 mm')
      }
    }

    return {
      isValid: errors.length === 0,
      sanitizedValue: sanitized,
      errors
    }
  }

  /**
   * Validates a printer model name
   */
  static validatePrinterModel(value: string): ValidationResult {
    return this.validateString(value, {
      minLength: 1,
      maxLength: 200,
      allowEmpty: false,
      trim: true
    })
  }

  /**
   * Validates a technical name (usually contains @ symbol for slicer profiles)
   */
  static validateTechnicalName(value: string): ValidationResult {
    const result = this.validateString(value, {
      minLength: 1,
      maxLength: 500,
      allowEmpty: false,
      trim: true
    })

    // Additional validation for technical names
    if (result.isValid && result.sanitizedValue) {
      // Technical names often contain @ symbol for slicer profiles
      if (!result.sanitizedValue.includes('@')) {
        result.errors.push('Technical name should typically contain @ symbol for slicer profiles')
        // This is a warning, not a hard error
      }
    }

    return result
  }

  /**
   * Sanitizes user input to prevent XSS and other injection attacks
   */
  static sanitizeUserInput(input: string): string {
    if (!input || typeof input !== 'string') {
      return ''
    }

    return input
      .trim()
      .replace(/[<>]/g, '') // Remove potential HTML tags
      .replace(/javascript:/gi, '') // Remove javascript: protocol
      .replace(/on\w+=/gi, '') // Remove event handlers
      .replace(/[\x00-\x08\x0B\x0C\x0E-\x1F\x7F]/g, '') // Remove control characters
  }

  /**
   * Validates multiple inputs at once
   */
  static validateBatch(validations: Array<{
    name: string
    value: any
    validator: (value: any) => ValidationResult
  }>): { isValid: boolean; results: Record<string, ValidationResult> } {
    const results: Record<string, ValidationResult> = {}
    let allValid = true

    for (const validation of validations) {
      const result = validation.validator(validation.value)
      results[validation.name] = result
      if (!result.isValid) {
        allValid = false
      }
    }

    return {
      isValid: allValid,
      results
    }
  }
}
