// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest } from '@feathersjs/errors'
import * as fs from 'node:fs/promises'
import * as fssync from 'node:fs'
import * as path from 'node:path'

import type { Application } from '../../declarations'
import type {
  ProfileConverter,
  ProfileConverterData,
  ProfileConverterPatch,
  ProfileConverterQuery
} from './profile-converter.schema'

export type { ProfileConverter, ProfileConverterData, ProfileConverterPatch, ProfileConverterQuery }

export interface ProfileConverterServiceOptions {
  app: Application
}

export interface ProfileConverterParams extends Params<ProfileConverterQuery> {}

// Helpers
function isLikelyBase64Zip(s: string): boolean {
  // UEsDB is 'PK' in base64; also check raw 'PK' header
  if (!s || typeof s !== 'string') return false
  return s.startsWith('UEsDB') || s.startsWith('PK\u0003\u0004') || s.startsWith('PK')
}

function isFilePath(s: string): boolean {
  if (typeof s !== 'string') return false
  // crude check: contains path separator or ends with a known extension
  return /[\\/]/.test(s) || /\.(json|ini|zip|orca|orca_profile|orca_printer|orca_filament)$/i.test(s)
}

function coerceValue(v: unknown): string | number | boolean {
  if (typeof v === 'boolean' || typeof v === 'number') return v
  if (v === null || v === undefined) return ''
  if (Array.isArray(v)) {
    // Most Orca profiles store single-extruder values as a single-element array.
    const first = v[0]
    return coerceValue(first)
  }
  const s = String(v)
  // Try boolean
  if (/^(true|false)$/i.test(s)) return /^true$/i.test(s)
  // Try number (int/float), but avoid turning IDs with leading zeros into numbers
  if (/^[+-]?(?:\d+\.\d+|\d+)$/i.test(s)) {
    const n = Number(s)
    if (!Number.isNaN(n)) return n
  }
  return s
}

function flattenPreset(obj: Record<string, any>): Record<string, string | number | boolean> {
  const out: Record<string, string | number | boolean> = {}
  for (const [k, v] of Object.entries(obj || {})) {
    // Skip common metadata keys that are not slicer config parameters
    if (k === 'name' || k === 'version' || k === 'force_update' || k === 'description') continue
    // Many Orca JSONs store values as arrays. Flatten sensibly.
    if (Array.isArray(v)) {
      if (v.length === 0) continue
      out[k] = coerceValue(v)
    } else if (v && typeof v === 'object') {
      // Nested objects are uncommon in preset JSON; keep as JSON string if present
      out[k] = JSON.stringify(v)
    } else {
      out[k] = coerceValue(v)
    }
  }
  return out
}

function tryRequireJsZip(): any {
  // Try to resolve explicitly to avoid path quirks and normalize default export
  try {
    // eslint-disable-next-line @typescript-eslint/no-var-requires
    const req = require as any
    const resolved = req.resolve('jszip')
    const mod = req(resolved)
    return (mod && (mod.default || mod)) || null
  } catch {
    try {
      // Fallback: direct require without resolve
      // eslint-disable-next-line @typescript-eslint/no-var-requires
      const mod = require('jszip')
      return (mod && (mod.default || mod)) || null
    } catch {
      return null
    }
  }
}


async function loadJsonFromZipBuffer(buf: Uint8Array, kind: 'printer' | 'filament' | 'process'): Promise<any> {
  const JSZip = tryRequireJsZip()
  if (JSZip) {
    const zip = await JSZip.loadAsync(buf)
    const jsonFiles = Object.values(zip.files).filter((f: any) => !f.dir && /\.json$/i.test(f.name))
    let firstObj: any | undefined
    const desiredType = kind === 'printer' ? 'machine' : kind
    const refKey = kind === 'printer' ? 'printer_config' : kind === 'filament' ? 'filament_config' : 'process_config'
    for (const f of jsonFiles as any[]) {
      try {
        const txt = await f.async('string')
        const obj = JSON.parse(txt)
        if (!firstObj) firstObj = obj
        // If this file is already the desired preset type, use it
        if (obj && typeof obj === 'object' && String(obj.type || '').toLowerCase() === desiredType) {
          return obj
        }
        // If this JSON looks like a bundle manifest, follow the referenced config path in the ZIP
        const ref = obj && typeof obj === 'object' ? obj[refKey] : undefined
        if (ref && typeof ref === 'string') {
          const norm = ref.replace(/\\/g, '/').replace(/^\//, '')
          const zf: any = (zip.files as any)[norm] || (zip.files as any)[`./${norm}`]
          if (zf && !zf.dir) {
            try {
              const txt2 = await zf.async('string')
              const obj2 = JSON.parse(txt2)
              if (!firstObj) firstObj = obj2
              if (obj2 && typeof obj2 === 'object' && String(obj2.type || '').toLowerCase() === desiredType) {
                return obj2
              }
            } catch {}
          }
        }
      } catch {}
    }
    if (firstObj) return firstObj
    throw new BadRequest('Could not find a JSON preset inside the provided ZIP/.orca file.')
  }
  // JSZip not available: fail fast with clear guidance (we no longer call system unzip/7z)
  throw new BadRequest('ZIP parsing requires the jszip package in the runtime. Please ensure jszip is installed in the container (npm install jszip).')
}

async function loadPresetFromInputs(
  kind: 'printer' | 'filament' | 'process',
  raw: any,
  params?: any
): Promise<any> {
  // 1) Multipart file support (koa-body)
  const anyParams: any = params ?? {}
  const filesContainer = anyParams?.koa?.request?.files ?? anyParams?.files ?? anyParams?.koa?.ctx?.request?.files
  let fileObj: any | undefined
  if (filesContainer) {
    fileObj = filesContainer['file'] ?? filesContainer.file
    if (Array.isArray(fileObj)) fileObj = fileObj[0]
    if (!fileObj && typeof filesContainer === 'object') {
      const first = Object.values(filesContainer)[0]
      fileObj = Array.isArray(first) ? first[0] : first
    }
  }

  if (fileObj) {
    const filePath = fileObj.filepath || fileObj.path || fileObj.tempFilePath
    if (!filePath) throw new BadRequest('Invalid uploaded file')
    const ext = path.extname(filePath).toLowerCase()
    if (ext === '.json') {
      const content = await fs.readFile(filePath, 'utf8')
      return JSON.parse(content)
    }
    if (ext === '.zip' || ext === '.orca' || ext === '.orca_profile' || ext === '.orca_printer' || ext === '.orca_filament') {
      const buf = await fs.readFile(filePath)
      return loadJsonFromZipBuffer(buf, kind)
    }
    throw new BadRequest(`Unsupported file extension: ${ext}`)
  }

  // 2) Raw already-parsed object
  if (raw && typeof raw === 'object') return raw

  // 3) Raw string: path, JSON, or base64 ZIP
  if (typeof raw === 'string') {
    if (isFilePath(raw) && fssync.existsSync(raw)) {
      const ext = path.extname(raw).toLowerCase()
      if (ext === '.json') {
        const content = await fs.readFile(raw, 'utf8')
        return JSON.parse(content)
      }
      if (ext === '.zip' || ext === '.orca' || ext === '.orca_profile' || ext === '.orca_printer' || ext === '.orca_filament') {
        const buf = await fs.readFile(raw)
        return loadJsonFromZipBuffer(buf, kind)
      }
    }
    // Try JSON text
    try {
      return JSON.parse(raw)
    } catch {}
    // Base64 ZIP detection
    if (isLikelyBase64Zip(raw)) {
      const buf = (globalThis as any).Buffer.from(raw, 'base64')
      return loadJsonFromZipBuffer(buf, kind)
    }
  }

  throw new BadRequest('No valid input found. Send a multipart file, a path/JSON string, or an object.')
}

// Custom service: converts Orca exported profiles into overrides JSON
export class ProfileConverterService<ServiceParams extends ProfileConverterParams = ProfileConverterParams>
  implements ServiceInterface<ProfileConverter, ProfileConverterData, ServiceParams, ProfileConverterPatch>
{
  constructor(public options: ProfileConverterServiceOptions) {}

  async find(_params?: ServiceParams): Promise<ProfileConverter[]> {
    return []
  }

  async get(_id: Id, _params?: ServiceParams): Promise<ProfileConverter> {
    throw new BadRequest('GET is not supported on this service')
  }

  async create(data: ProfileConverterData, params?: ServiceParams): Promise<ProfileConverter>
  async create(data: ProfileConverterData[], params?: ServiceParams): Promise<ProfileConverter[]>
  async create(
    data: ProfileConverterData | ProfileConverterData[],
    _params?: ServiceParams
  ): Promise<ProfileConverter | ProfileConverter[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, _params)))
    }

    const { type, data: raw } = data as { type: 'printer' | 'filament' | 'process'; data?: any }

    // Load the right preset JSON from file/raw
    const obj = await loadPresetFromInputs(type, raw, _params)

    // Flatten to options record expected by slicer services
    const options = flattenPreset(obj)
    return { options }
  }

  // Not exposed/used; keep minimal stubs in case Feathers calls them internally
  async update(_id: NullableId, _data: ProfileConverterData, _params?: ServiceParams): Promise<ProfileConverter> {
    throw new BadRequest('UPDATE is not supported on this service')
  }

  async patch(_id: NullableId, _data: ProfileConverterPatch, _params?: ServiceParams): Promise<ProfileConverter> {
    throw new BadRequest('PATCH is not supported on this service')
  }

  async remove(_id: NullableId, _params?: ServiceParams): Promise<ProfileConverter> {
    throw new BadRequest('REMOVE is not supported on this service')
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
