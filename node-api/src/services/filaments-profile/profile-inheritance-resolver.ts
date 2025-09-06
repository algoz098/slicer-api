/**
 * Profile Inheritance Resolver
 * 
 * This module handles the resolution of profile inheritance chains in OrcaSlicer profiles.
 * OrcaSlicer uses an inheritance system where profiles can inherit from other profiles
 * using the "inherits" field, similar to object-oriented inheritance.
 * 
 * Example inheritance chain:
 * "Bambu ABS @base" inherits from "fdm_filament_abs" 
 * which inherits from "fdm_filament_common"
 * 
 * The resolver merges all inherited properties to create a complete profile.
 */

import * as fs from 'fs'
import * as path from 'path'
import { logger } from '../../logger'

export interface ProfileData {
  [key: string]: any
  inherits?: string
  name?: string
  type?: string
}

export interface ResolvedProfile {
  data: ProfileData
  inheritanceChain: string[]
  resolvedFields: Record<string, string> // field -> source profile name
}

export interface InheritanceResolutionContext {
  profileBasePath: string
  profileType: 'filament' | 'process' | 'machine'
  maxDepth?: number
  visited?: Set<string>
}

/**
 * Resolves profile inheritance chains and merges data from all inherited profiles
 */
export class ProfileInheritanceResolver {
  
  /**
   * Resolves a complete profile by following its inheritance chain
   */
  static async resolveProfile(
    profileName: string, 
    context: InheritanceResolutionContext
  ): Promise<ResolvedProfile> {
    const maxDepth = context.maxDepth || 10
    const visited = context.visited || new Set<string>()
    
    // Prevent infinite recursion
    if (visited.has(profileName)) {
      throw new Error(`Circular inheritance detected: ${Array.from(visited).join(' -> ')} -> ${profileName}`)
    }
    
    if (visited.size >= maxDepth) {
      throw new Error(`Maximum inheritance depth (${maxDepth}) exceeded for profile: ${profileName}`)
    }
    
    visited.add(profileName)
    
    try {
      // Load the current profile
      const profileData = await this.loadProfileData(profileName, context)
      
      if (!profileData) {
        throw new Error(`Profile not found: ${profileName}`)
      }
      
      const inheritanceChain: string[] = [profileName]
      const resolvedFields: Record<string, string> = {}
      let mergedData: ProfileData = { ...profileData }
      
      // Mark fields from current profile
      Object.keys(profileData).forEach(field => {
        if (field !== 'inherits') {
          resolvedFields[field] = profileName
        }
      })
      
      // Resolve inheritance chain if present
      if (profileData.inherits) {
        const parentContext: InheritanceResolutionContext = {
          ...context,
          visited: new Set(visited)
        }
        
        const parentResolution = await this.resolveProfile(profileData.inherits, parentContext)
        
        // Merge parent data (parent fields are overridden by child fields)
        const parentData = parentResolution.data
        Object.keys(parentData).forEach(field => {
          if (!(field in mergedData)) {
            mergedData[field] = parentData[field]
            resolvedFields[field] = parentResolution.resolvedFields[field] || profileData.inherits!
          }
        })
        
        // Add parent chain to inheritance chain
        inheritanceChain.push(...parentResolution.inheritanceChain)
      }
      
      return {
        data: mergedData,
        inheritanceChain,
        resolvedFields
      }
      
    } catch (error) {
      logger.error('Failed to resolve profile inheritance', {
        profileName,
        context: {
          profileType: context.profileType,
          profileBasePath: context.profileBasePath,
          visitedProfiles: Array.from(visited)
        },
        error: (error as Error).message
      })
      throw error
    }
  }
  
  /**
   * Loads profile data from file system
   */
  private static async loadProfileData(
    profileName: string, 
    context: InheritanceResolutionContext
  ): Promise<ProfileData | null> {
    
    // Try different possible file locations
    const possiblePaths = this.generatePossiblePaths(profileName, context)
    
    logger.debug('Searching for profile in possible paths', {
      profileName,
      possiblePathsCount: possiblePaths.length,
      possiblePaths: possiblePaths.slice(0, 5) // Log first 5 paths to avoid spam
    })

    for (const filePath of possiblePaths) {
      try {
        if (fs.existsSync(filePath)) {
          const fileContent = await fs.promises.readFile(filePath, 'utf-8')
          const profileData = JSON.parse(fileContent)

          logger.debug('Profile loaded successfully', {
            profileName,
            filePath,
            hasInheritance: !!profileData.inherits,
            inheritsFrom: profileData.inherits
          })

          return profileData
        }
      } catch (error) {
        logger.debug('Failed to load profile from path', {
          profileName,
          filePath,
          error: (error as Error).message
        })
        continue
      }
    }

    logger.debug('Profile not found in any of the possible paths', {
      profileName,
      totalPathsChecked: possiblePaths.length,
      samplePaths: possiblePaths.slice(0, 3)
    })
    
    return null
  }
  
  /**
   * Generates possible file paths for a profile
   */
  private static generatePossiblePaths(
    profileName: string,
    context: InheritanceResolutionContext
  ): string[] {
    const { profileBasePath, profileType } = context
    const paths: string[] = []

    // Standard profile path (local config)
    paths.push(path.join(profileBasePath, profileType, `${profileName}.json`))

    // Try in source OrcaSlicer directory structure first (most likely location)
    // The source_OrcaSlicer directory is one level up from the node-api directory
    const sourceBasePath = path.join(process.cwd(), '..', 'source_OrcaSlicer', 'resources', 'profiles')
    const vendorDirs = ['BBL', 'Generic', 'OrcaFilamentLibrary', 'Anker', 'Vzbot', 'Creality', 'Prusa']

    vendorDirs.forEach(vendor => {
      // Direct vendor directory
      paths.push(path.join(sourceBasePath, vendor, profileType, `${profileName}.json`))

      // Try with vendor subdirectories for filaments
      if (profileType === 'filament') {
        paths.push(path.join(sourceBasePath, vendor, profileType, vendor, `${profileName}.json`))

        // Try specific subdirectories that might exist
        const subDirs = ['Bambu', 'Generic', 'ABS', 'PLA', 'PETG', 'TPU']
        subDirs.forEach(subDir => {
          paths.push(path.join(sourceBasePath, vendor, profileType, subDir, `${profileName}.json`))
        })
      }
    })

    // Try in local vendor-specific directories (fallback)
    vendorDirs.forEach(vendor => {
      paths.push(path.join(profileBasePath, vendor, profileType, `${profileName}.json`))

      // Try with vendor subdirectories
      if (profileType === 'filament') {
        paths.push(path.join(profileBasePath, vendor, profileType, vendor, `${profileName}.json`))
      }
    })

    // Try without vendor directory (direct in profileType folder)
    paths.push(path.join(sourceBasePath, profileType, `${profileName}.json`))

    return paths
  }
  
  /**
   * Validates that a resolved profile has all required fields
   */
  static validateResolvedProfile(resolved: ResolvedProfile): {
    isValid: boolean
    missingFields: string[]
    warnings: string[]
  } {
    const requiredFields = ['name', 'type']
    const missingFields: string[] = []
    const warnings: string[] = []
    
    requiredFields.forEach(field => {
      if (!(field in resolved.data) || resolved.data[field] === null || resolved.data[field] === undefined) {
        missingFields.push(field)
      }
    })
    
    // Check for nil values that might need resolution
    Object.entries(resolved.data).forEach(([field, value]) => {
      if (Array.isArray(value) && value.length === 1 && value[0] === 'nil') {
        warnings.push(`Field ${field} has 'nil' value, might need default resolution`)
      }
    })
    
    return {
      isValid: missingFields.length === 0,
      missingFields,
      warnings
    }
  }
  
  /**
   * Gets inheritance information for debugging
   */
  static getInheritanceInfo(resolved: ResolvedProfile): {
    totalFields: number
    fieldsBySource: Record<string, string[]>
    inheritanceDepth: number
  } {
    const fieldsBySource: Record<string, string[]> = {}
    
    Object.entries(resolved.resolvedFields).forEach(([field, source]) => {
      if (!fieldsBySource[source]) {
        fieldsBySource[source] = []
      }
      fieldsBySource[source].push(field)
    })
    
    return {
      totalFields: Object.keys(resolved.data).length,
      fieldsBySource,
      inheritanceDepth: resolved.inheritanceChain.length
    }
  }
  
  /**
   * Resolves 'nil' values to appropriate defaults or removes them
   */
  static resolveNilValues(data: ProfileData): ProfileData {
    const resolved = { ...data }
    
    Object.entries(resolved).forEach(([field, value]) => {
      if (Array.isArray(value) && value.length === 1 && value[0] === 'nil') {
        // Remove nil values - they should be inherited from parent or use system defaults
        delete resolved[field]
      }
    })
    
    return resolved
  }
  
  /**
   * Creates a summary of profile resolution for logging
   */
  static createResolutionSummary(resolved: ResolvedProfile): {
    profileName: string
    inheritanceChain: string[]
    totalFields: number
    inheritedFields: number
    ownFields: number
  } {
    const profileName = resolved.inheritanceChain[0]
    const ownFields = Object.values(resolved.resolvedFields).filter(source => source === profileName).length
    const inheritedFields = Object.keys(resolved.resolvedFields).length - ownFields
    
    return {
      profileName,
      inheritanceChain: resolved.inheritanceChain,
      totalFields: Object.keys(resolved.data).length,
      inheritedFields,
      ownFields
    }
  }
}
