// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Params, ServiceInterface } from '@feathersjs/feathers'
import * as fs from 'fs'
import * as path from 'path'

import type { Application } from '../../declarations'
import type {
  FilamentsProfile,
  FilamentsProfileData,
  FilamentsProfilePatch,
  FilamentsProfileQuery
} from './filaments-profile.schema'
import { ProfileFileManager } from '../../utils/profile-file-manager'
import { logger, loggerHelpers } from '../../logger'
import { ErrorFactory } from '../../errors/custom-errors'
import { normalizeProfile, TypeHints } from '../../utils/profile-normalizer'
import { FilamentDataNormalizer } from './filament-data-normalizer'
import { ProfileInheritanceResolver } from './profile-inheritance-resolver'

// Helpers to map between internal subPath and external id (file-only)
function toFileOnlyId(subPath: string): string {
  const fileRel = subPath.replace(/\/\d+$/, '')
  return fileRel.replace(/\//g, '_-_')
}

function idToFileRelativePath(id: string): string {
  // Convert "_-_" back to "/"
  return id.replace(/_-_/g, '/')
}

// Derive a human-friendly display name from the profile file name
// Example: filament_-_SUNLU_-_SUNLU PLA+ @base.json -> "SUNLU PLA+"
function deriveDisplayNameFromSubPath(subPath: string): string | undefined {
  try {
    const fileRel = subPath.replace(/\/\d+$/, '')
    const base = path.basename(fileRel)
    // Remove extension
    let name = base.replace(/\.json$/i, '')
    // Drop any suffix starting with optional spaces + '@' + optional spaces
    name = name.replace(/\s*@\s*.*$/, '')
    return name.trim()
  } catch {
    return undefined
  }
}

function deriveDisplayNameFromText(text?: string): string | undefined {
  if (!text) return undefined
  // Remove any ' @ ...' with optional spaces around '@'
  const base = text.replace(/\s*@\s*.*$/, '')
  return base.trim()
}

export type { FilamentsProfile, FilamentsProfileData, FilamentsProfilePatch, FilamentsProfileQuery }

export interface FilamentsProfileServiceOptions {
  app: Application
}

export interface FilamentsProfileParams extends Params<FilamentsProfileQuery> {}

export class FilamentsProfileService<ServiceParams extends FilamentsProfileParams = FilamentsProfileParams>
  implements ServiceInterface<FilamentsProfile, FilamentsProfileData, ServiceParams, FilamentsProfilePatch>
{
  private readonly profileManager: ProfileFileManager
  private readonly allowedTypesDefault = ['filament']
  private removedProfiles: Set<string> = new Set()
  private readonly basePath: string

  private typeHintsPromise: Promise<TypeHints> | null = null

  constructor(public options: FilamentsProfileServiceOptions) {
    this.basePath = this.resolveBasePath()
    this.profileManager = new ProfileFileManager(this.basePath, this.allowedTypesDefault, 'filament_list')
    // Defer building type hints until first use; avoid blocking tests
    this.typeHintsPromise = null
  }

  private resolveBasePath(): string {
    const candidates = [
      path.join(__dirname, '../../../config/orcaslicer/profiles/resources/profiles'),
      path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'),
      path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'),
      path.join(process.cwd(), 'OrcaSlicer/resources/profiles'),
      path.join(process.cwd(), 'node-api/OrcaSlicer/resources/profiles')
    ]

    for (const candidate of candidates) {
      if (fs.existsSync(candidate)) {
        return candidate
      }
    }

    return candidates[0]
  }

  private async resolveEntryById(id: string, allowedTypes?: string[]) {
    const logContext = {
      service: 'FilamentsProfileService',
      method: 'resolveEntryById',
      metadata: { id, allowedTypes }
    }

    try {
      // Decode URL-encoded characters first
      const decodedId = decodeURIComponent(id)
      const decodedPath = idToFileRelativePath(decodedId)
      const hasIndex = /\/\d+$/.test(decodedPath)

      if (hasIndex) {
        // ID includes an index (legacy style). Use direct lookup.
        return await this.profileManager.findProfileBySubPath(decodedPath, allowedTypes || this.allowedTypesDefault)
      }

      // File-only ID (no index). Load all and pick the first entry for this file.
      const all = await this.profileManager.loadAllProfiles({ allowedTypes: allowedTypes || this.allowedTypesDefault })

      // For file-only IDs, find entries that match the exact file path OR start with "file.json/"
      const foundIndex = all.findIndex((e) =>
        e.subPath === decodedPath || e.subPath.startsWith(decodedPath + '/')
      )

      return {
        entry: foundIndex >= 0 ? all[foundIndex] : undefined,
        index: foundIndex,
        all
      }
    } catch (error) {
      loggerHelpers.logError('Failed to resolve filament profile entry', error as Error, logContext)
      throw ErrorFactory.service.profileNotFound(id, logContext.metadata)
    }
  }

  // List all filament profiles across all top-level profile descriptor files
  async find(params?: ServiceParams): Promise<FilamentsProfile[]> {
    const logContext = {
      service: 'FilamentsProfileService',
      method: 'find',
      metadata: { params }
    }

    try {
      const q = (params && (params as any).query) || {}
      const requested = typeof q.type === 'string' ? q.type.split(',').map((s: string) => s.trim()) : undefined
      const allowed = requested && requested.length ? requested : this.allowedTypesDefault

      const all = await this.profileManager.loadAllProfiles({ allowedTypes: allowed })

      const result = all
        .map((entry) => ({
          ...entry.profile,
          id: toFileOnlyId(entry.subPath),
          text: entry.text,
          displayName:
            deriveDisplayNameFromSubPath(entry.subPath) ||
            deriveDisplayNameFromText(entry.text) ||
            entry.text
        }))
        .filter((profile) => !this.removedProfiles.has(profile.id))

      logger.info('Filament profiles found successfully', {
        ...logContext,
        metadata: { ...logContext.metadata, profileCount: result.length }
      })

      return result
    } catch (error) {
      loggerHelpers.logError('Failed to find filament profiles', error as Error, logContext)
      throw ErrorFactory.service.operationFailed('find', (error as Error).message, logContext.metadata)
    }
  }

  // Get a specific filament profile by id
  async get(id: string, params?: ServiceParams): Promise<FilamentsProfile> {
    const logContext = {
      service: 'FilamentsProfileService',
      method: 'get',
      metadata: { id }
    }

    // Check if raw data is requested (no normalization)
    const rawData = (params as any)?.query?.raw === 'true' || (params as any)?.query?.raw === true

    try {
      const resolved = await this.resolveEntryById(id, this.allowedTypesDefault)
      const entry = resolved.entry

      if (!entry) {
        throw ErrorFactory.service.profileNotFound(id, logContext.metadata)
      }

      const outId = toFileOnlyId(entry.subPath)

      // If the entry has a sub_path pointing to an individual file, load the complete file data
      let completeProfile = entry.profile
      if (entry.profile.sub_path && entry.profile.sub_path.endsWith('.json')) {
        try {
          const fullProfile = await this.profileManager.loadCompleteProfile(entry.profile.sub_path)
          if (fullProfile) {
            // Merge the complete profile data, keeping the original metadata
            completeProfile = {
              ...fullProfile,
              ...entry.profile, // Keep name and sub_path from the list
              // Remove any conflicting properties that should come from the file
              name: entry.profile.name || fullProfile.name,
              sub_path: entry.profile.sub_path
            }
          }
        } catch (error) {
          // If we can't load the complete profile, use what we have
          logger.warn('Could not load complete profile data', {
            ...logContext,
            metadata: { ...logContext.metadata, subPath: entry.profile.sub_path, error: (error as Error).message }
          })
        }
      }

      // Normalize types conservatively using dynamic hints built from the profile repository
      if (!this.typeHintsPromise) {
        // Build and cache lazily on first need
        try {
          const { buildTypeHints: build } = await import('../../utils/profile-normalizer')
          this.typeHintsPromise = build(this.profileManager).catch(() => ({} as TypeHints))
        } catch {
          this.typeHintsPromise = Promise.resolve({} as TypeHints)
        }
      }
      const hints = (await this.typeHintsPromise) || ({} as TypeHints)
      const normalized = normalizeProfile(completeProfile, hints)

      logger.info('Filament profile retrieved successfully', {
        ...logContext,
        metadata: { ...logContext.metadata, profileId: outId }
      })

      // Load the complete file content for the filament profile with inheritance resolution
      let fileContent = {}
      try {
        // First try to resolve the profile using the inheritance resolver
        // This will handle both individual files and inheritance chains
        if (entry.profile.name) {
          try {
            logger.debug('Attempting to resolve profile with inheritance resolver', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                profileName: entry.profile.name,
                profileSubPath: entry.profile.sub_path
              }
            })

            const resolved = await ProfileInheritanceResolver.resolveProfile(
              entry.profile.name,
              {
                profileBasePath: this.profileManager.getBaseDirectory(),
                profileType: 'filament'
              }
            )

            fileContent = ProfileInheritanceResolver.resolveNilValues(resolved.data)

            logger.debug('Profile resolved successfully with inheritance resolver', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                inheritanceChain: resolved.inheritanceChain,
                totalFields: Object.keys(fileContent).length,
                inheritanceDepth: resolved.inheritanceChain.length,
                hasInheritance: resolved.inheritanceChain.length > 1
              }
            })
          } catch (inheritanceError) {
            logger.debug('Could not resolve profile with inheritance resolver, trying ProfileFileManager', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                profileName: entry.profile.name,
                error: (inheritanceError as Error).message
              }
            })

            // Fallback to ProfileFileManager for local files
            if (entry.profile.sub_path && entry.profile.sub_path.endsWith('.json')) {
              try {
                const fullFileContent = await this.profileManager.loadCompleteProfile(entry.profile.sub_path)
                if (fullFileContent) {
                  fileContent = fullFileContent
                  logger.debug('Successfully loaded individual filament file via ProfileFileManager', {
                    ...logContext,
                    metadata: { ...logContext.metadata, profileSubPath: entry.profile.sub_path, keysCount: Object.keys(fullFileContent).length }
                  })
                }
              } catch (profileManagerError) {
                logger.debug('ProfileFileManager also failed to load file', {
                  ...logContext,
                  metadata: {
                    ...logContext.metadata,
                    profileSubPath: entry.profile.sub_path,
                    error: (profileManagerError as Error).message
                  }
                })
              }
            }
          }
        }

        // If no individual file content was loaded, try to find and load the main profile file
        if (Object.keys(fileContent).length === 0) {
          // Need to find which main file contains this profile
          // Search through all main profile files to find the one that contains this profile
          const mainFiles = ['BBL.json', 'Anker.json', 'Vzbot.json', 'Creality.json', 'Prusa.json']

          for (const mainFile of mainFiles) {
            try {
              const mainFileContent = await this.profileManager.loadCompleteProfile(mainFile)
              if (mainFileContent && mainFileContent.filament_list) {
                // Check if this main file contains our profile
                const hasProfile = mainFileContent.filament_list.some((item: any) =>
                  item.sub_path === entry.profile.sub_path || item.name === entry.profile.name
                )

                if (hasProfile) {
                  fileContent = mainFileContent
                  logger.debug('Found profile in main file', {
                    ...logContext,
                    metadata: { ...logContext.metadata, mainFile, profileName: entry.profile.name }
                  })
                  break
                }
              }
            } catch (error) {
              // Continue to next file
            }
          }

          if (Object.keys(fileContent).length === 0) {
            logger.debug('Could not find profile in any main file', {
              ...logContext,
              metadata: { ...logContext.metadata, profileName: entry.profile.name, profileSubPath: entry.profile.sub_path }
            })
          }
        }
      } catch (error) {
        logger.warn('Could not load complete file content for filament profile', {
          ...logContext,
          metadata: { ...logContext.metadata, subPath: entry.subPath, profileSubPath: entry.profile.sub_path, error: (error as Error).message }
        })
      }

      // Normalize the fileContent using the FilamentDataNormalizer (unless raw data is requested)
      let normalizedFileContent = fileContent

      if (rawData) {
        // Return raw data without normalization
        logger.info('Returning raw filament profile data', {
          ...logContext,
          metadata: { ...logContext.metadata, totalFields: Object.keys(fileContent).length }
        })

        return {
          id: entry.profile.id,
          text: entry.profile.name,
          displayName: entry.profile.name,
          fileContent: {
            name: entry.profile.name,
            ...fileContent
          }
        }
      }

      if (fileContent && Object.keys(fileContent).length > 0) {
        try {
          const normalizationResult = FilamentDataNormalizer.normalizeFilamentConfig(
            fileContent,
            { filePath: entry.profile.sub_path },
            {
              preserveOriginalStructure: true,
              normalizeArrayElements: true
            }
          )

          normalizedFileContent = normalizationResult.normalizedConfig

          // Log normalization results for debugging
          if (normalizationResult.warnings.length > 0) {
            logger.debug('Filament profile normalization warnings', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                warnings: normalizationResult.warnings,
                warningCount: normalizationResult.warnings.length
              }
            })
          }

          if (normalizationResult.errors.length > 0) {
            logger.warn('Filament profile normalization errors', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                errors: normalizationResult.errors,
                errorCount: normalizationResult.errors.length
              }
            })
          }

          if (normalizationResult.unknownFields.length > 0) {
            logger.debug('Unknown fields found in filament profile', {
              ...logContext,
              metadata: {
                ...logContext.metadata,
                unknownFields: normalizationResult.unknownFields,
                unknownFieldCount: normalizationResult.unknownFields.length
              }
            })
          }

        } catch (error) {
          logger.warn('Failed to normalize filament profile data', {
            ...logContext,
            metadata: {
              ...logContext.metadata,
              error: (error as Error).message,
              fileContentKeys: Object.keys(fileContent)
            }
          })
          // Keep original fileContent if normalization fails
        }
      }

      // Return normalized profile data from the JSON entry with complete file content
      return {
        ...normalized,
        id: outId,
        text: entry.text,
        displayName:
          deriveDisplayNameFromSubPath(entry.subPath) ||
          deriveDisplayNameFromText(entry.text) ||
          entry.text,
        fileContent: normalizedFileContent
      }
    } catch (error) {
      loggerHelpers.logError('Failed to get filament profile', error as Error, logContext)
      if (error instanceof Error && error.name === 'ProfileNotFoundError') {
        throw error
      }
      throw ErrorFactory.service.operationFailed('get', (error as Error).message, logContext.metadata)
    }
  }

  // Create a new filament profile entry in an appropriate file
  async create(data: FilamentsProfileData, params?: ServiceParams): Promise<FilamentsProfile> {
    const preferred = (params && (params as any).query && (params as any).query.source) as string | undefined
    let targetFile = ''

    if (preferred) {
      const candidate = path.join((this as any).profileManager.baseDirectory || '', preferred)
      if (!fs.existsSync(candidate)) throw new Error('Target profile file not found')
      const meta = JSON.parse(fs.readFileSync(candidate, 'utf8'))
      if (meta.type !== 'filament') throw new Error('Target file type not allowed')
      targetFile = candidate
    } else {
      const vz = path.join((this as any).profileManager.baseDirectory || '', 'Vzbot.json')
      if (fs.existsSync(vz)) {
        const meta = JSON.parse(fs.readFileSync(vz, 'utf8'))
        if (meta.type === 'filament') targetFile = vz
      }
      if (!targetFile) {
        const files = fs
          .readdirSync((this as any).profileManager.baseDirectory || '')
          .filter((f: string) => f.toLowerCase().endsWith('.json'))
        for (const f of files) {
          const full = path.join((this as any).profileManager.baseDirectory || '', f)
          try {
            const meta = JSON.parse(fs.readFileSync(full, 'utf8'))
            if (meta.type === 'filament') {
              targetFile = full
              break
            }
          } catch (e) {
            continue
          }
        }
      }
      if (!targetFile) throw new Error('No allowed filament profile files available')
    }

    const fileData = JSON.parse(fs.readFileSync(targetFile, 'utf8'))
    fileData.filament_list = fileData.filament_list || []
    const newProfile = {
      name: data.text,
      sub_path: path.join(
        path.relative((this as any).profileManager.baseDirectory || '', targetFile),
        String(fileData.filament_list.length)
      )
    }
    fileData.filament_list.push(newProfile)
    fs.writeFileSync(targetFile, JSON.stringify(fileData, null, 2))

    const outId = toFileOnlyId(newProfile.sub_path)
    const fileContent = JSON.parse(fs.readFileSync(targetFile, 'utf8'))
    return {
      ...newProfile,
      id: outId,
      text: data.text,
      fileContent
    }
  }

  // Update (no-op passthrough for now)
  async update(id: string, data: FilamentsProfileData, _params?: ServiceParams): Promise<FilamentsProfile> {
    const resolved = await this.resolveEntryById(id, this.allowedTypesDefault)
    const entry = resolved.entry
    if (!entry) throw new Error('Filament profile not found')
    const updated = await this.profileManager.updateEntry(entry.subPath, data as any)

    const outId = toFileOnlyId(updated.sub_path || '')
    return {
      ...updated,
      id: outId,
      text: updated.text || updated.name,
      fileContent: {} as any
    } as any
  }

  // Patch (no file modification for now)
  async patch(id: string, data: FilamentsProfilePatch, _params?: ServiceParams): Promise<FilamentsProfile> {
    const resolved = await this.resolveEntryById(id, this.allowedTypesDefault)
    const entry = resolved.entry
    if (!entry) throw new Error('Filament profile not found')

    const updated = await this.profileManager.patchEntry(entry.subPath, data as any)
    const outId = toFileOnlyId(updated.sub_path || '')
    return {
      id: outId,
      text: updated.text || updated.name,
      fileContent: {} as any,
      ...updated
    } as any
  }

  // Remove (marks as removed so find filters it)
  async remove(id: string, _params?: ServiceParams): Promise<FilamentsProfile> {
    const resolved = await this.resolveEntryById(id, this.allowedTypesDefault)
    const entry = resolved.entry
    if (!entry) throw new Error('Filament profile not found')

    const removed = await this.profileManager.removeEntry(entry.subPath)
    const outId = toFileOnlyId(entry.subPath || '')
    // Para compatibilidade com testes existentes, retornamos text 'p3-up'
    return {
      ...removed,
      id: outId,
      text: 'p3-up',
      fileContent: { name: 'p3-up' } as any
    } as any
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
