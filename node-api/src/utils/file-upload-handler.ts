import * as fs from 'fs'
import * as path from 'path'

import type { UploadedFile } from '../types/common'
import { APP_CONSTANTS } from '../config/app-config'

/**
 * Simple helper functions for file upload operations
 * Business logic has been moved to services
 */
export class FileUploadHelper {

  /**
   * Sanitizes a filename to prevent path traversal and other security issues
   */
  static sanitizeFileName(fileName: string): string {
    if (!fileName || typeof fileName !== 'string') {
      return 'uploaded_file'
    }

    // Remove path separators and other dangerous characters
    const sanitized = fileName
      .replace(/[/\\:*?"<>|]/g, '_') // Replace dangerous characters
      .replace(/\.\./g, '_') // Prevent directory traversal
      .replace(/^\.+/, '_') // Prevent hidden files
      .trim()

    // Ensure filename is not empty and has reasonable length
    if (!sanitized || sanitized.length === 0) {
      return 'uploaded_file'
    }

    if (sanitized.length > APP_CONSTANTS.FILE_UPLOAD.MAX_FILENAME_LENGTH) {
      const ext = path.extname(sanitized)
      const base = path.basename(sanitized, ext)
      return base.substring(0, APP_CONSTANTS.FILE_UPLOAD.MAX_FILENAME_LENGTH - ext.length) + ext
    }

    return sanitized
  }



  /**
   * Writes uploaded file content to temporary location
   */
  static async writeFileToTemp(file: UploadedFile, tempFilePath: string): Promise<void> {
    if (file.buffer) {
      await fs.promises.writeFile(tempFilePath, file.buffer)
    } else if (file.path) {
      await fs.promises.copyFile(file.path, tempFilePath)
    } else {
      // Fallback: try to write the file object directly
      await fs.promises.writeFile(tempFilePath, file as any)
    }

    // Verify file was written successfully
    const stats = await fs.promises.stat(tempFilePath)
    if (stats.size === 0) {
      throw new Error('Uploaded file appears to be empty')
    }
  }

  /**
   * Creates a unique temporary file path
   */
  static createTempFilePath(fileName: string, tempDir?: string): string {
    const sanitizedName = this.sanitizeFileName(fileName)
    const dir = tempDir || APP_CONSTANTS.FILE_UPLOAD.TEMP_DIR

    return path.join(
      dir,
      `temp_${Date.now()}_${Math.random().toString(36).substring(7)}_${sanitizedName}`
    )
  }

  /**
   * Cleanup temporary file
   */
  static async cleanupTempFile(filePath: string): Promise<void> {
    try {
      await fs.promises.unlink(filePath)
    } catch (error) {
      // Ignore cleanup errors to avoid breaking the main flow
      console.warn(`Failed to cleanup temporary file ${filePath}:`, error)
    }
  }

  /**
   * Get file extension from filename
   */
  static getFileExtension(fileName: string): string {
    return path.extname(fileName).toLowerCase()
  }

  /**
   * Check if file extension is supported
   */
  static isSupportedExtension(extension: string): boolean {
    return APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_EXTENSIONS.includes(extension as any)
  }

  /**
   * Check if MIME type is supported
   */
  static isSupportedMimeType(mimeType: string): boolean {
    return APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_MIME_TYPES.includes(mimeType as any)
  }
}
