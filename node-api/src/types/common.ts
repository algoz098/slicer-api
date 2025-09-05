/**
 * Common types and interfaces used across the application
 */

// File upload related types
export interface UploadedFile {
  /** File buffer when stored in memory */
  buffer?: Buffer
  /** File path when stored on disk */
  path?: string
  /** Original filename from the client */
  originalFilename?: string
  /** Alternative name property */
  name?: string
  /** File size in bytes */
  size?: number
  /** MIME type of the file */
  mimetype?: string
}

export interface FileProcessingResult {
  /** Path to the temporary file */
  tempFilePath: string
  /** Sanitized filename */
  fileName: string
  /** File extension (lowercase, with dot) */
  fileExtension: string
  /** Cleanup function to remove temporary file */
  cleanup: () => Promise<void>
}

// 3D printing profile related types
export interface PrinterProfileInfo {
  /** Printer model or manufacturer name */
  printer?: string
  /** Nozzle diameter in mm */
  nozzle?: string
  /** Print profile name */
  profile?: string
  /** Technical name from slicer software */
  technicalName?: string
}

export interface ThreeMFMetadata {
  /** Profile title or name */
  profileTitle?: string
  /** Design profile identifier */
  designProfileId?: string
  /** Print settings identifier */
  printSettingsId?: string
  /** Printer settings identifier */
  printerSettingsId?: string
  /** Printer model name */
  printerModel?: string
  /** Nozzle diameter array */
  nozzleDiameter?: string[]
  /** Default filament profile */
  defaultFilamentProfile?: string
  /** Default print profile */
  defaultPrintProfile?: string
  /** Compatible printers list */
  printCompatiblePrinters?: string[]
}

// Error types for better error handling
export interface FileProcessingError extends Error {
  code: 'INVALID_FILE' | 'UNSUPPORTED_FORMAT' | 'FILE_TOO_LARGE' | 'PROCESSING_FAILED'
  details?: Record<string, any>
}

export interface ValidationError extends Error {
  code: 'VALIDATION_FAILED'
  field: string
  value: any
  constraint: string
}

// Configuration types
export interface FileUploadConfig {
  /** Maximum file size in bytes */
  maxFileSize: number
  /** Allowed file extensions */
  allowedExtensions: string[]
  /** Allowed MIME types */
  allowedMimeTypes: string[]
  /** Temporary directory for file processing */
  tempDir: string
}

export interface ProcessingConfig {
  /** Candidate metadata files to search in 3MF archives */
  candidateFiles: string[]
  /** Regex patterns for extracting profile information */
  patterns: RegExp[]
  /** Timeout for processing operations in milliseconds */
  processingTimeout: number
}

// Service response types
export interface ServiceResponse<T> {
  /** Response data */
  data: T
  /** Success status */
  success: boolean
  /** Error message if any */
  error?: string
  /** Additional metadata */
  metadata?: Record<string, any>
}

// Pagination types for future use
export interface PaginationParams {
  /** Number of items to skip */
  $skip?: number
  /** Maximum number of items to return */
  $limit?: number
  /** Sort criteria */
  $sort?: Record<string, 1 | -1>
}

export interface PaginatedResponse<T> {
  /** Array of data items */
  data: T[]
  /** Total number of items available */
  total: number
  /** Number of items skipped */
  skip: number
  /** Maximum number of items returned */
  limit: number
}

// Utility types
export type RequiredKeys<T, K extends keyof T> = T & Required<Pick<T, K>>
export type OptionalKeys<T, K extends keyof T> = Omit<T, K> & Partial<Pick<T, K>>

// Constants for validation
export const FILE_CONSTRAINTS = {
  MAX_FILENAME_LENGTH: 255,
  MAX_TECHNICAL_NAME_LENGTH: 500,
  MAX_PRINTER_NAME_LENGTH: 200,
  MAX_PROFILE_NAME_LENGTH: 200,
  NOZZLE_DIAMETER_PATTERN: /^[0-9]+\.[0-9]+$/,
  SUPPORTED_EXTENSIONS: ['.3mf', '.stl', '.gcode'] as const,
  SUPPORTED_MIME_TYPES: [
    'application/octet-stream',
    'application/zip',
    'model/3mf',
    'text/plain'
  ] as const
} as const

export type SupportedExtension = typeof FILE_CONSTRAINTS.SUPPORTED_EXTENSIONS[number]
export type SupportedMimeType = typeof FILE_CONSTRAINTS.SUPPORTED_MIME_TYPES[number]
