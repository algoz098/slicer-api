// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, Params, ServiceInterface } from '@feathersjs/feathers'
import { NotFound } from '@feathersjs/errors'
import * as fs from 'node:fs'
import * as path from 'node:path'

import type { Application } from '../../declarations'
import type { Profiles, ProfilesData, ProfilesQuery } from './profiles.schema'

export type { Profiles, ProfilesData, ProfilesQuery }

export interface ProfilesServiceOptions {
  app: Application
}

export interface ProfilesParams extends Params<ProfilesQuery> {}

type ProfileType = 'machine' | 'filament' | 'process'

interface ProfileInfo {
  name: string
  type: ProfileType
  vendor: string
  filePath: string
}

// Cache for profile index
let profileIndex: ProfileInfo[] | null = null
let profilesRoot: string | null = null

// Parse a profile JSON file
function parseProfile(filePath: string): Record<string, unknown> | null {
  try {
    const content = fs.readFileSync(filePath, 'utf8')
    return JSON.parse(content)
  } catch {
    return null
  }
}

// Resolve inheritance chain and merge configs
function resolveInheritance(
  profileName: string,
  type: ProfileType,
  vendor: string,
  index: ProfileInfo[],
  root: string,
  visited: Set<string> = new Set()
): Record<string, unknown> {
  if (visited.has(profileName)) {
    return {} // Circular reference protection
  }
  visited.add(profileName)

  // Find the profile file
  const typeDir = type === 'machine' ? 'machine' : type
  const profilePath = path.join(root, vendor, typeDir, `${profileName}.json`)

  let profile = parseProfile(profilePath)
  if (!profile) {
    // Try to find by name in the index
    const found = index.find(p => p.name === profileName && p.type === type && p.vendor === vendor)
    if (found) {
      profile = parseProfile(found.filePath)
    }
    if (!profile) {
      // Try to find in same vendor without exact match
      const vendorDir = path.join(root, vendor, typeDir)
      if (fs.existsSync(vendorDir)) {
        const files = fs.readdirSync(vendorDir)
        for (const file of files) {
          if (file.endsWith('.json')) {
            const fp = path.join(vendorDir, file)
            const parsed = parseProfile(fp)
            if (parsed && parsed.name === profileName) {
              profile = parsed
              break
            }
          }
        }
      }
    }
  }

  if (!profile) {
    return {}
  }

  const inherits = profile.inherits as string | undefined
  let baseConfig: Record<string, unknown> = {}

  if (inherits) {
    // Resolve parent config first
    baseConfig = resolveInheritance(inherits, type, vendor, index, root, visited)
  }

  // Merge: parent config overwritten by current profile
  const merged = { ...baseConfig }
  for (const [key, value] of Object.entries(profile)) {
    if (key !== 'inherits') {
      merged[key] = value
    }
  }

  return merged
}

// Build the profile index by scanning all vendors
function buildProfileIndex(resourcesPath: string): ProfileInfo[] {
  const profiles: ProfileInfo[] = []
  const profilesDir = path.join(resourcesPath, 'profiles')

  if (!fs.existsSync(profilesDir)) {
    return profiles
  }

  const entries = fs.readdirSync(profilesDir)

  for (const entry of entries) {
    const vendorPath = path.join(profilesDir, entry)
    const stat = fs.statSync(vendorPath)

    if (!stat.isDirectory()) {
      continue
    }

    const vendor = entry
    const typeDirs: { dir: string; type: ProfileType }[] = [
      { dir: 'machine', type: 'machine' },
      { dir: 'filament', type: 'filament' },
      { dir: 'process', type: 'process' }
    ]

    for (const { dir, type } of typeDirs) {
      const typeDir = path.join(vendorPath, dir)
      if (!fs.existsSync(typeDir)) {
        continue
      }

      const files = fs.readdirSync(typeDir)
      for (const file of files) {
        if (!file.endsWith('.json')) {
          continue
        }

        const filePath = path.join(typeDir, file)
        const parsed = parseProfile(filePath)
        if (!parsed) {
          continue
        }

        const name = (parsed.name as string) || path.basename(file, '.json')
        // Skip common/base profiles that are not instantiable
        if (parsed.instantiation === 'false' || name.startsWith('fdm_')) {
          continue
        }

        profiles.push({ name, type, vendor, filePath })
      }
    }
  }

  return profiles
}

// Get resource path from app
function getResourcesPath(app: Application): string {
  return (app.get('orca_resourcesPath') as string) || ''
}

// Ensure index is built
function ensureIndex(app: Application): { index: ProfileInfo[]; root: string } {
  const resourcesPath = getResourcesPath(app)
  if (!profileIndex || profilesRoot !== resourcesPath) {
    profilesRoot = resourcesPath
    profileIndex = buildProfileIndex(resourcesPath)
  }
  return { index: profileIndex, root: path.join(resourcesPath, 'profiles') }
}

export class ProfilesService<ServiceParams extends ProfilesParams = ProfilesParams>
  implements ServiceInterface<Profiles, ProfilesData, ServiceParams>
{
  constructor(public options: ProfilesServiceOptions) {}

  // List all available profiles
  async find(params?: ServiceParams): Promise<Profiles[]> {
    const { index, root } = ensureIndex(this.options.app)
    const query = params?.query || {}

    let filtered = index
    if (query.type) {
      filtered = filtered.filter(p => p.type === query.type)
    }
    if (query.vendor) {
      filtered = filtered.filter(p => p.vendor === query.vendor)
    }

    return filtered.map(p => {
      const parsed = parseProfile(p.filePath) || {}
      return {
        name: p.name,
        type: p.type,
        vendor: p.vendor,
        inherits: parsed.inherits as string | undefined,
        config: {} // Empty for list, use get for full config
      }
    })
  }

  // Get a specific profile with fully resolved config
  async get(id: Id, params?: ServiceParams): Promise<Profiles> {
    const { index, root } = ensureIndex(this.options.app)
    const profileName = decodeURIComponent(String(id))
    const query = params?.query || {}

    // Find matching profile
    let matches = index.filter(p => p.name === profileName)
    if (query.type) {
      matches = matches.filter(p => p.type === query.type)
    }
    if (query.vendor) {
      matches = matches.filter(p => p.vendor === query.vendor)
    }

    if (matches.length === 0) {
      throw new NotFound(`Profile not found: ${profileName}`)
    }

    // Use first match if multiple
    const profile = matches[0]
    const parsed = parseProfile(profile.filePath) || {}

    // Resolve full config with inheritance
    const config = resolveInheritance(profile.name, profile.type, profile.vendor, index, root)

    return {
      name: profile.name,
      type: profile.type,
      vendor: profile.vendor,
      inherits: parsed.inherits as string | undefined,
      config
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
