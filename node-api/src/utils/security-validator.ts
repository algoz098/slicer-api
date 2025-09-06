import * as path from 'path'
import * as fs from 'fs'

import { logger, loggerHelpers } from '../logger'
import type { UploadedFile } from '../types/common'

export interface SecurityValidationOptions {
  /** Maximum allowed file size in bytes */
  maxFileSize: number
  /** Allowed file extensions */
  allowedExtensions: string[]
  /** Allowed MIME types */
  allowedMimeTypes: string[]
  /** Whether to perform deep file content validation */
  deepValidation: boolean
  /** Maximum filename length */
  maxFilenameLength: number
}

export interface SecurityValidationResult {
  /** Whether the file passed all security checks */
  isValid: boolean
  /** List of security issues found */
  issues: string[]
  /** Sanitized filename */
  sanitizedFilename: string
  /** Detected file type */
  detectedType?: string
}

export class SecurityValidator {
  private readonly options: SecurityValidationOptions

  constructor(options: Partial<SecurityValidationOptions> = {}) {
    this.options = {
      maxFileSize: options.maxFileSize || 100 * 1024 * 1024, // 100MB
      allowedExtensions: options.allowedExtensions || ['.3mf', '.stl', '.gcode'],
      allowedMimeTypes: options.allowedMimeTypes || [
        'application/octet-stream',
        'application/zip',
        'model/3mf',
        'text/plain'
      ],
      deepValidation: options.deepValidation ?? true,
      maxFilenameLength: options.maxFilenameLength || 255
    }
  }

  /**
   * Performs comprehensive security validation on an uploaded file
   */
  async validateFile(file: UploadedFile): Promise<SecurityValidationResult> {
    const logContext = {
      service: 'SecurityValidator',
      metadata: { 
        originalFilename: file.originalFilename,
        size: file.size,
        mimetype: file.mimetype
      }
    }

    logger.info('Starting security validation', logContext)

    const issues: string[] = []
    let sanitizedFilename = ''

    try {
      // 1. Validate filename
      const filenameValidation = this.validateFilename(file.originalFilename || file.name || '')
      if (!filenameValidation.isValid) {
        issues.push(...filenameValidation.issues)
      }
      sanitizedFilename = filenameValidation.sanitizedFilename

      // 2. Validate file size
      if (file.size && file.size > this.options.maxFileSize) {
        issues.push(`File size ${file.size} exceeds maximum allowed size of ${this.options.maxFileSize} bytes`)
      }

      if (file.size === 0) {
        issues.push('File appears to be empty')
      }

      // 3. Validate file extension
      const extension = path.extname(sanitizedFilename).toLowerCase()
      if (!this.options.allowedExtensions.includes(extension)) {
        issues.push(`File extension '${extension}' is not allowed. Allowed extensions: ${this.options.allowedExtensions.join(', ')}`)
      }

      // 4. Validate MIME type
      if (file.mimetype && !this.options.allowedMimeTypes.includes(file.mimetype)) {
        issues.push(`MIME type '${file.mimetype}' is not allowed. Allowed types: ${this.options.allowedMimeTypes.join(', ')}`)
      }

      // 5. Deep content validation (if enabled and file has content)
      let detectedType: string | undefined
      if (this.options.deepValidation && (file.buffer || file.path)) {
        const contentValidation = await this.validateFileContent(file, extension)
        if (!contentValidation.isValid) {
          issues.push(...contentValidation.issues)
        }
        detectedType = contentValidation.detectedType
      }

      const isValid = issues.length === 0

      if (isValid) {
        logger.info('Security validation passed', {
          ...logContext,
          metadata: { ...logContext.metadata, sanitizedFilename, detectedType }
        })
      } else {
        loggerHelpers.logSecurityEvent('File validation failed', 'medium', {
          ...logContext,
          metadata: { ...logContext.metadata, issues, sanitizedFilename }
        })
      }

      return {
        isValid,
        issues,
        sanitizedFilename,
        detectedType
      }

    } catch (error) {
      loggerHelpers.logError('Security validation error', error as Error, logContext)
      return {
        isValid: false,
        issues: ['Security validation failed due to internal error'],
        sanitizedFilename: 'unknown_file'
      }
    }
  }

  /**
   * Validates and sanitizes filename
   */
  private validateFilename(filename: string): { isValid: boolean; issues: string[]; sanitizedFilename: string } {
    const issues: string[] = []

    if (!filename || filename.trim().length === 0) {
      return {
        isValid: false,
        issues: ['Filename is empty or missing'],
        sanitizedFilename: 'uploaded_file'
      }
    }

    // Check for path traversal attempts
    if (filename.includes('..') || filename.includes('/') || filename.includes('\\')) {
      issues.push('Filename contains path traversal characters')
    }

    // Check for dangerous characters
    const dangerousChars = /[<>:"|?*\x00-\x1f]/
    if (dangerousChars.test(filename)) {
      issues.push('Filename contains dangerous characters')
    }

    // Check filename length
    if (filename.length > this.options.maxFilenameLength) {
      issues.push(`Filename too long (${filename.length} > ${this.options.maxFilenameLength})`)
    }

    // Check for hidden files
    if (filename.startsWith('.')) {
      issues.push('Hidden files are not allowed')
    }

    // Sanitize filename
    let sanitized = filename
      .replace(/[<>:"|?*\x00-\x1f]/g, '_') // Replace dangerous characters
      .replace(/\.\./g, '_') // Remove path traversal
      .replace(/^\.+/, '_') // Remove leading dots
      .trim()

    // Ensure reasonable length
    if (sanitized.length > this.options.maxFilenameLength) {
      const ext = path.extname(sanitized)
      const base = path.basename(sanitized, ext)
      sanitized = base.substring(0, this.options.maxFilenameLength - ext.length) + ext
    }

    // Ensure not empty after sanitization
    if (!sanitized || sanitized.length === 0) {
      sanitized = 'uploaded_file'
    }

    return {
      isValid: issues.length === 0,
      issues,
      sanitizedFilename: sanitized
    }
  }

  /**
   * Validates file content based on file type
   */
  private async validateFileContent(
    file: UploadedFile, 
    expectedExtension: string
  ): Promise<{ isValid: boolean; issues: string[]; detectedType?: string }> {
    const issues: string[] = []
    let detectedType: string | undefined

    try {
      let buffer: Buffer

      if (file.buffer) {
        buffer = file.buffer
      } else if (file.path) {
        buffer = await fs.promises.readFile(file.path)
      } else {
        return {
          isValid: false,
          issues: ['Cannot access file content for validation']
        }
      }

      // Check for empty files
      if (buffer.length === 0) {
        issues.push('File content is empty')
        return { isValid: false, issues }
      }

      // Validate based on expected file type
      switch (expectedExtension) {
        case '.3mf':
          detectedType = this.validate3MFContent(buffer, issues)
          break
        case '.stl':
          detectedType = this.validateSTLContent(buffer, issues)
          break
        case '.gcode':
          detectedType = this.validateGCodeContent(buffer, issues)
          break
        default:
          issues.push(`Content validation not implemented for ${expectedExtension}`)
      }

      return {
        isValid: issues.length === 0,
        issues,
        detectedType
      }

    } catch (error) {
      issues.push(`Content validation failed: ${error instanceof Error ? error.message : 'Unknown error'}`)
      return { isValid: false, issues }
    }
  }

  /**
   * Validates 3MF file content (should be a ZIP archive)
   */
  private validate3MFContent(buffer: Buffer, issues: string[]): string {
    // Check ZIP file signature
    if (buffer.length < 4) {
      issues.push('File too small to be a valid 3MF file')
      return 'unknown'
    }

    const zipSignature = buffer.subarray(0, 4)
    const validZipSignatures = [
      Buffer.from([0x50, 0x4B, 0x03, 0x04]), // Standard ZIP
      Buffer.from([0x50, 0x4B, 0x05, 0x06]), // Empty ZIP
      Buffer.from([0x50, 0x4B, 0x07, 0x08])  // Spanned ZIP
    ]

    const isValidZip = validZipSignatures.some(sig => zipSignature.equals(sig))
    if (!isValidZip) {
      issues.push('File does not appear to be a valid ZIP/3MF archive')
    }

    return '3mf'
  }

  /**
   * Validates STL file content
   */
  private validateSTLContent(buffer: Buffer, issues: string[]): string {
    if (buffer.length < 80) {
      issues.push('File too small to be a valid STL file')
      return 'unknown'
    }

    // Check for ASCII STL
    const header = buffer.subarray(0, 5).toString('ascii').toLowerCase()
    if (header === 'solid') {
      return 'stl-ascii'
    }

    // Check for binary STL (has 80-byte header + 4-byte triangle count)
    if (buffer.length >= 84) {
      return 'stl-binary'
    }

    issues.push('File does not appear to be a valid STL file')
    return 'unknown'
  }

  /**
   * Validates G-code file content
   */
  private validateGCodeContent(buffer: Buffer, issues: string[]): string {
    try {
      const content = buffer.toString('utf8', 0, Math.min(1024, buffer.length))
      
      // Look for common G-code patterns
      const gcodePatterns = [
        /^[GM]\d+/m,  // G or M commands
        /^;/m,        // Comments
        /^T\d+/m      // Tool commands
      ]

      const hasGCodePattern = gcodePatterns.some(pattern => pattern.test(content))
      if (!hasGCodePattern) {
        issues.push('File does not appear to contain valid G-code')
      }

      return 'gcode'
    } catch (error) {
      issues.push('Failed to read G-code content as text')
      return 'unknown'
    }
  }
}
