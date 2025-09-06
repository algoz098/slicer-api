import * as fs from 'fs'
import * as path from 'path'

import { ErrorFactory } from '../errors/custom-errors'
import { logger, loggerHelpers } from '../logger'

export interface ProfileFile {
  /** Full path to the profile file */
  filePath: string
  /** Relative path from base directory */
  relativePath: string
  /** File metadata */
  metadata: ProfileFileMetadata
  /** Profile entries in the file */
  profiles: ProfileEntry[]
}

export interface ProfileFileMetadata {
  /** File type (process, machine, etc.) */
  type?: string
  /** File name */
  name?: string
  /** Whether file inherits from another */
  inherits?: string
  /** Source of the file */
  from?: string
  /** Whether this is an instantiation file */
  instantiation?: string
}

export interface ProfileEntry {
  /** Profile data */
  profile: any
  /** Index within the file */
  localIndex: number
  /** Generated sub_path for identification */
  subPath: string
  /** Display text/name */
  text: string
}

export interface ProfileSearchOptions {
  /** Allowed profile types */
  allowedTypes?: string[]
  /** Base directory to search */
  baseDirectory: string
  /** Whether to include non-profile files */
  includeNonProfiles?: boolean
}

export class ProfileFileManager {
  private readonly baseDirectory: string
  private readonly allowedTypes: string[]
  private readonly nonProfilesDir: string

  constructor(baseDirectory: string, allowedTypes: string[] = ['process', 'machine', 'machine_model']) {
    this.baseDirectory = baseDirectory
    this.allowedTypes = allowedTypes
    this.nonProfilesDir = path.join(baseDirectory, '_non_profiles')
  }

  /**
   * Loads all profile files and their entries
   */
  async loadAllProfiles(options?: { allowedTypes?: string[] }): Promise<ProfileEntry[]> {
    const logContext = {
      service: 'ProfileFileManager',
      metadata: { baseDirectory: this.baseDirectory }
    }

    logger.info('Loading all profiles', logContext)

    if (!fs.existsSync(this.baseDirectory)) {
      logger.warn('Base directory does not exist', logContext)
      return []
    }

    const allowedTypes = options?.allowedTypes || this.allowedTypes
    const result: ProfileEntry[] = []

    try {
      await this.walkDirectory(this.baseDirectory, allowedTypes, result)

      logger.info('Profiles loaded successfully', {
        ...logContext,
        metadata: { ...logContext.metadata, profileCount: result.length }
      })

      return result
    } catch (error) {
      loggerHelpers.logError('Failed to load profiles', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('loadAllProfiles', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Finds a specific profile by its sub_path
   */
  async findProfileBySubPath(subPath: string, allowedTypes?: string[]): Promise<{
    entry?: ProfileEntry
    index: number
    all: ProfileEntry[]
  }> {
    const all = await this.loadAllProfiles({ allowedTypes })
    const normalizedSubPath = subPath.replaceAll('_-_', '/')
    
    const foundIndex = all.findIndex(entry => entry.subPath === normalizedSubPath)
    
    return {
      entry: foundIndex >= 0 ? all[foundIndex] : undefined,
      index: foundIndex,
      all
    }
  }

  /**
   * Reads and validates a JSON profile file
   */
  async readProfileFile(filePath: string): Promise<ProfileFile> {
    const logContext = {
      service: 'ProfileFileManager',
      metadata: { filePath }
    }

    try {
      const content = await fs.promises.readFile(filePath, 'utf8')
      const data = JSON.parse(content)

      if (!this.isValidProfileFile(data)) {
        throw ErrorFactory.validation.invalidFileName(
          path.basename(filePath),
          'File does not contain valid profile structure',
          logContext.metadata
        )
      }

      const relativePath = path.relative(this.baseDirectory, filePath)
      const profiles = this.extractProfileEntries(data, relativePath)

      return {
        filePath,
        relativePath,
        metadata: {
          type: data.type,
          name: data.name,
          inherits: data.inherits,
          from: data.from,
          instantiation: data.instantiation
        },
        profiles
      }
    } catch (error) {
      if (error instanceof SyntaxError) {
        loggerHelpers.logError('Invalid JSON in profile file', error, logContext)
        throw ErrorFactory.processing.metadataNotFound(path.basename(filePath), logContext.metadata)
      }
      throw error
    }
  }

  /**
   * Writes profile data to a JSON file
   */
  async writeProfileFile(filePath: string, data: any): Promise<void> {
    const logContext = {
      service: 'ProfileFileManager',
      metadata: { filePath }
    }

    try {
      const jsonContent = JSON.stringify(data, null, 2)
      await fs.promises.writeFile(filePath, jsonContent, 'utf8')
      
      logger.info('Profile file written successfully', logContext)
    } catch (error) {
      loggerHelpers.logError('Failed to write profile file', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('writeProfileFile', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Moves invalid files to non-profiles directory
   */
  async moveToNonProfiles(filePath: string): Promise<void> {
    try {
      await this.ensureNonProfilesDirectory()
      
      const fileName = path.basename(filePath)
      let destPath = path.join(this.nonProfilesDir, fileName)
      
      // Handle filename conflicts
      let counter = 1
      while (fs.existsSync(destPath)) {
        const ext = path.extname(fileName)
        const base = path.basename(fileName, ext)
        destPath = path.join(this.nonProfilesDir, `${base}_${counter}${ext}`)
        counter++
      }

      await fs.promises.rename(filePath, destPath)
      
      logger.info('File moved to non-profiles directory', {
        service: 'ProfileFileManager',
        metadata: { originalPath: filePath, newPath: destPath }
      })
    } catch (error) {
      // Don't throw errors for move operations to avoid blocking profile loading
      logger.warn('Failed to move file to non-profiles directory', {
        service: 'ProfileFileManager',
        metadata: { filePath, error: (error as Error).message }
      })
    }
  }

  /**
   * Validates if a file contains valid profile structure
   */
  private isValidProfileFile(data: any): boolean {
    return (
      typeof data === 'object' &&
      data !== null &&
      Array.isArray(data.process_list) &&
      data.process_list.length > 0
    )
  }

  /**
   * Extracts profile entries from file data
   */
  private extractProfileEntries(data: any, relativePath: string): ProfileEntry[] {
    const entries: ProfileEntry[] = []
    const processList = data.process_list || []

    for (let i = 0; i < processList.length; i++) {
      const profile = processList[i]
      
      // Ensure stable sub_path
      if (!profile.sub_path || profile.sub_path === '') {
        profile.sub_path = path.join(relativePath, String(i))
      }

      // Ensure text alias exists
      if (typeof profile.text === 'undefined' && typeof profile.name !== 'undefined') {
        profile.text = profile.name
      }

      entries.push({
        profile,
        localIndex: i,
        subPath: profile.sub_path,
        text: profile.text || profile.name || `Profile ${i}`
      })
    }

    return entries
  }

  /**
   * Recursively walks directory to find profile files
   */
  private async walkDirectory(dir: string, allowedTypes: string[], result: ProfileEntry[]): Promise<void> {
    let entries: string[]
    
    try {
      entries = await fs.promises.readdir(dir)
    } catch (error) {
      return // Skip directories that can't be read
    }

    for (const entry of entries) {
      const fullPath = path.join(dir, entry)
      
      let stat: fs.Stats
      try {
        stat = await fs.promises.stat(fullPath)
      } catch (error) {
        continue // Skip entries that can't be stat'd
      }

      if (stat.isDirectory()) {
        await this.walkDirectory(fullPath, allowedTypes, result)
        continue
      }

      if (!entry.toLowerCase().endsWith('.json')) {
        continue
      }

      try {
        const profileFile = await this.readProfileFile(fullPath)
        
        // Check if file type is allowed
        if (profileFile.metadata.type && !allowedTypes.includes(profileFile.metadata.type)) {
          continue
        }

        result.push(...profileFile.profiles)
      } catch (error) {
        // Move invalid files to non-profiles directory
        await this.moveToNonProfiles(fullPath)
      }
    }
  }

  /**
   * Ensures non-profiles directory exists
   */
  private async ensureNonProfilesDirectory(): Promise<void> {
    if (!fs.existsSync(this.nonProfilesDir)) {
      await fs.promises.mkdir(this.nonProfilesDir, { recursive: true })
    }
  }
}
