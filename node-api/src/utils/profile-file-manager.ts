import * as fs from 'fs'
import * as path from 'path'

import { ErrorFactory } from '../errors/custom-errors'
import { loggerHelpers } from '../logger'

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
  private readonly listKey: string

  constructor(
    baseDirectory: string,
    allowedTypes: string[] = ['process', 'machine', 'machine_model'],
    listKey: string = 'process_list'
  ) {
    this.baseDirectory = baseDirectory
    this.allowedTypes = allowedTypes
    this.nonProfilesDir = path.join(baseDirectory, '_non_profiles')
    this.listKey = listKey
  }


  /** Exposes the base directory (read-only) */
  getBaseDirectory(): string {
    return this.baseDirectory
  }

  /** Parses a subPath (relativeFile/index) into absolute file path and index */
  private parseSubPath(subPath: string): { filePath: string; index: number } {
    const normalized = subPath.replaceAll('_-_', '/')
    const parts = normalized.split('/')
    if (parts.length < 2) throw ErrorFactory.validation.invalidParameter('subPath', 'Invalid subPath')
    const indexStr = parts.pop() as string
    const index = Number(indexStr)
    if (!Number.isInteger(index) || index < 0) throw ErrorFactory.validation.invalidParameter('index', 'Invalid index')
    const relative = parts.join('/')
    const filePath = path.join(this.baseDirectory, relative)
    return { filePath, index }
  }

  /** Applies a shallow patch to a profile entry and writes to disk */
  async patchEntry(subPath: string, patch: any): Promise<any> {
    const { filePath, index } = this.parseSubPath(subPath)
    const content = JSON.parse(await fs.promises.readFile(filePath, 'utf8'))
    const list = content[this.listKey]
    if (!Array.isArray(list) || !list[index]) {
      throw ErrorFactory.service.profileNotFound(subPath)
    }
    const current = list[index]
    const updated = { ...current, ...patch }
    if (typeof patch?.text === 'string' && !patch?.name) {
      updated.name = patch.text
    }
    // preserve sub_path if exists
    if (!updated.sub_path) {
      updated.sub_path = current.sub_path || `${path.relative(this.baseDirectory, filePath)}/${index}`
    }
    list[index] = updated
    await fs.promises.writeFile(filePath, JSON.stringify(content, null, 2), 'utf8')
    return updated
  }

  /** Updates (replace-like) a profile entry's main fields (name/text) */
  async updateEntry(subPath: string, data: any): Promise<any> {
    return this.patchEntry(subPath, data)
  }

  /** Removes a profile entry from the file and writes to disk; returns removed entry */
  async removeEntry(subPath: string): Promise<any> {
    const { filePath, index } = this.parseSubPath(subPath)
    const content = JSON.parse(await fs.promises.readFile(filePath, 'utf8'))
    const list = content[this.listKey]
    if (!Array.isArray(list) || !list[index]) {
      throw ErrorFactory.service.profileNotFound(subPath)
    }
    const [removed] = list.splice(index, 1)
    await fs.promises.writeFile(filePath, JSON.stringify(content, null, 2), 'utf8')
    return removed
  }

  /**
   * Loads all profile files and their entries
   */
  async loadAllProfiles(options?: { allowedTypes?: string[]; listKey?: string }): Promise<ProfileEntry[]> {
    const logContext = {
      service: 'ProfileFileManager',
      metadata: { baseDirectory: this.baseDirectory }
    }

    // Reduced logging for lower memory/cpu overhead

    if (!fs.existsSync(this.baseDirectory)) {
      // Base directory does not exist; return empty set
      return []
    }

    const allowedTypes = options?.allowedTypes || this.allowedTypes
    if (options?.listKey && options.listKey !== this.listKey) {
      // If a different listKey is specified per call, clone a temp manager for that call
      const temp = new ProfileFileManager(this.baseDirectory, allowedTypes, options.listKey)
      return temp.loadAllProfiles({ allowedTypes })
    }
    const result: ProfileEntry[] = []

    try {
      await this.walkDirectory(this.baseDirectory, allowedTypes, result)

      // Loaded profiles successfully
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
   * Loads complete profile data from an individual file
   */
  async loadCompleteProfile(subPath: string): Promise<any | null> {
    try {
      const fullPath = path.join(this.baseDirectory, subPath)
      const data = await fs.promises.readFile(fullPath, 'utf8')
      return JSON.parse(data)
    } catch (error) {
      return null
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
    } catch (error) {
      loggerHelpers.logError('Failed to write profile file', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('writeProfileFile', (error as Error).message, logContext.metadata)
    }
  }

  /**
   * Do not move invalid files anywhere; keep them in place to avoid FS changes on reads
   */
  async moveToNonProfiles(_filePath: string): Promise<void> {
    // No-op by request: avoid renaming/creating directories that could trigger watchers
    return
  }

  /**
   * Validates if a file contains valid profile structure
   */
  private isValidProfileFile(data: any): boolean {
    return (
      typeof data === 'object' &&
      data !== null &&
      Array.isArray(data[this.listKey]) &&
      data[this.listKey].length > 0
    )
  }

  /**
   * Extracts profile entries from file data
   */
  private extractProfileEntries(data: any, relativePath: string): ProfileEntry[] {
    const entries: ProfileEntry[] = []
    const list = data[this.listKey] || []

    for (let i = 0; i < list.length; i++) {
      const profile = list[i]

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
    // Reduced logging for lower memory/cpu overhead
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
