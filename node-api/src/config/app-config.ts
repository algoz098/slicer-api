import * as path from 'path'
import * as os from 'os'

/**
 * Application constants and configuration
 */
export const APP_CONSTANTS = {
  // File upload constraints
  FILE_UPLOAD: {
    MAX_FILE_SIZE: 100 * 1024 * 1024, // 100MB
    MAX_FILENAME_LENGTH: 255,
    SUPPORTED_EXTENSIONS: ['.3mf', '.stl', '.gcode'] as const,
    SUPPORTED_MIME_TYPES: [
      'application/octet-stream',
      'application/zip',
      'model/3mf',
      'text/plain'
    ] as const,
    TEMP_DIR: os.tmpdir(),
    CLEANUP_TIMEOUT: 5 * 60 * 1000, // 5 minutes
  },

  // 3MF processing
  THREE_MF: {
    CANDIDATE_FILES: [
      'Metadata/model_settings.config',
      'Metadata/print_profile.config',
      'Metadata/project_settings.config',
      'Metadata/process_settings_0.config'
    ],
    PROCESSING_TIMEOUT: 30 * 1000, // 30 seconds
    MAX_METADATA_SIZE: 1024 * 1024, // 1MB
  },

  // Profile management
  PROFILES: {
    ALLOWED_TYPES: ['process', 'machine', 'machine_model'],
    NON_PROFILES_DIR: '_non_profiles',
    DEFAULT_PROFILE_FILE: 'Vzbot.json',
    MAX_PROFILES_PER_FILE: 1000,
  },

  // Security
  SECURITY: {
    ENABLE_DEEP_VALIDATION: true,
    MAX_PROCESSING_TIME: 60 * 1000, // 1 minute
    DANGEROUS_FILENAME_CHARS: /[<>:"|?*\x00-\x1f]/,
    PATH_TRAVERSAL_PATTERN: /\.\./,
    HIDDEN_FILE_PATTERN: /^\./,
  },

  // Logging
  LOGGING: {
    MAX_LOG_FILE_SIZE: 5 * 1024 * 1024, // 5MB
    MAX_LOG_FILES: 5,
    REQUEST_ID_LENGTH: 15,
    PERFORMANCE_THRESHOLD: 5000, // 5 seconds
  },

  // API
  API: {
    DEFAULT_PAGE_SIZE: 25,
    MAX_PAGE_SIZE: 100,
    RATE_LIMIT_WINDOW: 15 * 60 * 1000, // 15 minutes
    RATE_LIMIT_MAX_REQUESTS: 100,
  }
} as const

/**
 * Environment-based configuration
 */
export class AppConfig {
  private static instance: AppConfig
  private config: Record<string, any>

  private constructor() {
    this.config = this.loadConfiguration()
  }

  static getInstance(): AppConfig {
    if (!AppConfig.instance) {
      AppConfig.instance = new AppConfig()
    }
    return AppConfig.instance
  }

  /**
   * Get configuration value with optional default
   */
  get<T = any>(key: string, defaultValue?: T): T {
    const keys = key.split('.')
    let value = this.config

    for (const k of keys) {
      if (value && typeof value === 'object' && k in value) {
        value = value[k]
      } else {
        return defaultValue as T
      }
    }

    return value as T
  }

  /**
   * Set configuration value
   */
  set(key: string, value: any): void {
    const keys = key.split('.')
    let current = this.config

    for (let i = 0; i < keys.length - 1; i++) {
      const k = keys[i]
      if (!(k in current) || typeof current[k] !== 'object') {
        current[k] = {}
      }
      current = current[k]
    }

    current[keys[keys.length - 1]] = value
  }

  /**
   * Get all configuration
   */
  getAll(): Record<string, any> {
    return { ...this.config }
  }

  /**
   * Load configuration from environment and defaults
   */
  private loadConfiguration(): Record<string, any> {
    const env = process.env.NODE_ENV || 'development'
    
    return {
      // Environment
      environment: env,
      isDevelopment: env === 'development',
      isProduction: env === 'production',
      isTest: env === 'test',

      // Server
      server: {
        host: process.env.HOST || 'localhost',
        port: parseInt(process.env.PORT || '3030', 10),
        publicDir: process.env.PUBLIC_DIR || './public/',
      },

      // File upload
      fileUpload: {
        maxSize: parseInt(process.env.MAX_FILE_SIZE || String(APP_CONSTANTS.FILE_UPLOAD.MAX_FILE_SIZE), 10),
        tempDir: process.env.TEMP_DIR || APP_CONSTANTS.FILE_UPLOAD.TEMP_DIR,
        allowedExtensions: process.env.ALLOWED_EXTENSIONS?.split(',') || APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_EXTENSIONS,
        deepValidation: process.env.DEEP_VALIDATION !== 'false',
      },

      // Profiles
      profiles: {
        baseDirectory: this.resolveProfilesDirectory(),
        allowedTypes: process.env.ALLOWED_PROFILE_TYPES?.split(',') || APP_CONSTANTS.PROFILES.ALLOWED_TYPES,
        defaultFile: process.env.DEFAULT_PROFILE_FILE || APP_CONSTANTS.PROFILES.DEFAULT_PROFILE_FILE,
      },

      // Logging
      logging: {
        level: process.env.LOG_LEVEL || (env === 'production' ? 'info' : 'debug'),
        enableFileLogging: process.env.ENABLE_FILE_LOGGING === 'true' || env === 'production',
        logDir: process.env.LOG_DIR || './logs',
        maxFileSize: parseInt(process.env.MAX_LOG_FILE_SIZE || String(APP_CONSTANTS.LOGGING.MAX_LOG_FILE_SIZE), 10),
        maxFiles: parseInt(process.env.MAX_LOG_FILES || String(APP_CONSTANTS.LOGGING.MAX_LOG_FILES), 10),
      },

      // Security
      security: {
        enableDeepValidation: process.env.ENABLE_DEEP_VALIDATION !== 'false',
        maxProcessingTime: parseInt(process.env.MAX_PROCESSING_TIME || String(APP_CONSTANTS.SECURITY.MAX_PROCESSING_TIME), 10),
        rateLimitEnabled: process.env.RATE_LIMIT_ENABLED === 'true',
        rateLimitWindow: parseInt(process.env.RATE_LIMIT_WINDOW || String(APP_CONSTANTS.API.RATE_LIMIT_WINDOW), 10),
        rateLimitMaxRequests: parseInt(process.env.RATE_LIMIT_MAX_REQUESTS || String(APP_CONSTANTS.API.RATE_LIMIT_MAX_REQUESTS), 10),
      },

      // API
      api: {
        defaultPageSize: parseInt(process.env.DEFAULT_PAGE_SIZE || String(APP_CONSTANTS.API.DEFAULT_PAGE_SIZE), 10),
        maxPageSize: parseInt(process.env.MAX_PAGE_SIZE || String(APP_CONSTANTS.API.MAX_PAGE_SIZE), 10),
        enableCors: process.env.ENABLE_CORS !== 'false',
        corsOrigins: process.env.CORS_ORIGINS?.split(',') || ['http://localhost:3030'],
      },

      // Database (for future use)
      database: {
        url: process.env.DATABASE_URL,
        maxConnections: parseInt(process.env.DB_MAX_CONNECTIONS || '10', 10),
        connectionTimeout: parseInt(process.env.DB_CONNECTION_TIMEOUT || '30000', 10),
      },

      // External services (for future use)
      external: {
        slicerApiUrl: process.env.SLICER_API_URL,
        slicerApiKey: process.env.SLICER_API_KEY,
        slicerApiTimeout: parseInt(process.env.SLICER_API_TIMEOUT || '30000', 10),
      }
    }
  }

  /**
   * Resolve profiles directory with fallbacks
   */
  private resolveProfilesDirectory(): string {
    const candidates = [
      process.env.PROFILES_DIR,
      path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'),
      path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'),
      path.join(process.cwd(), 'OrcaSlicer/resources/profiles'),
      path.join(process.cwd(), 'node-api/OrcaSlicer/resources/profiles')
    ].filter(Boolean) as string[]

    // Return the first existing directory, or the first candidate as default
    for (const candidate of candidates) {
      try {
        const fs = require('fs')
        if (fs.existsSync(candidate)) {
          return candidate
        }
      } catch (error) {
        // Continue to next candidate
      }
    }

    return candidates[1] // Default fallback
  }
}

// Export singleton instance
export const appConfig = AppConfig.getInstance()

// Export type definitions
export type SupportedExtension = typeof APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_EXTENSIONS[number]
export type SupportedMimeType = typeof APP_CONSTANTS.FILE_UPLOAD.SUPPORTED_MIME_TYPES[number]
export type ProfileType = typeof APP_CONSTANTS.PROFILES.ALLOWED_TYPES[number]
