import * as fs from 'fs'
import * as path from 'path'
import { ProfileFileManager } from './profile-file-manager'

export type TypeHint = 'number' | 'boolean'
export type TypeHints = Record<string, TypeHint>

// Keys that are likely booleans by naming convention
const BOOLEAN_KEY_REGEX = /^(enable|activate|use|disable|allow)_[a-z0-9_]+$|^(instantiation)$/i

// Keys we should never coerce (identifiers, names, paths, lists of profiles, etc.)
const EXCLUDED_KEYS_REGEX = /(^|_)(id|type|name|vendor|from|inherits|sub_path|path|url|author|license|tags)$|^(compatible_printers|machine_model_list|process_list|filament_list|machine_list)$/i

// Keys that are likely to be lists even if they sometimes have a single element
const LIKELY_LIST_KEYS_REGEX = /(_list$|_types$|^compatible_printers$|^filament_type$|^filament_vendor$|^tags$|_gcode$)/i

function isNumericString(str: string): boolean {
  return /^\s*-?\d+(?:\.\d+)?\s*$/.test(str)
}

function isPercentString(str: string): boolean {
  return /^\s*-?\d+(?:\.\d+)?\s*%\s*$/.test(str)
}

function toNumberFromString(str: string): number {
  const s = str.trim()
  // We intentionally keep percentages as 0-100 numbers (not 0-1) for consistency
  if (isPercentString(s)) {
    const n = parseFloat(s.replace('%', '').trim())
    return n
  }
  return parseFloat(s)
}

function toBooleanFromString(str: string): boolean | undefined {
  const s = str.trim().toLowerCase()
  if (s === 'true') return true
  if (s === 'false') return false
  if (s === '1') return true
  if (s === '0') return false
  return undefined
}

/**
 * Build type hints by scanning existing JSON profile files.
 * - If a key appears with number values across files, hint 'number'
 * - If a key appears with boolean values or boolean-like strings, hint 'boolean'
 * - If both appear, prefer boolean for keys matching BOOLEAN_KEY_REGEX, otherwise prefer number
 */
export async function buildTypeHints(profileManager: ProfileFileManager): Promise<TypeHints> {
  const baseDir = profileManager.getBaseDirectory()
  const counts: Record<string, { num: number; bool: number }> = {}

  function consider(key: string, value: unknown) {
    if (EXCLUDED_KEYS_REGEX.test(key)) return

    const add = (k: string, kind: 'num' | 'bool') => {
      if (!counts[k]) counts[k] = { num: 0, bool: 0 }
      counts[k][kind]++
    }

    const handle = (k: string, v: unknown) => {
      if (typeof v === 'number') add(k, 'num')
      else if (typeof v === 'boolean') add(k, 'bool')
      else if (typeof v === 'string') {
        if (toBooleanFromString(v) !== undefined) add(k, 'bool')
        else if (isNumericString(v) || isPercentString(v)) add(k, 'num')
      }
    }

    if (Array.isArray(value)) {
      for (const el of value) handle(key, el)
    } else {
      handle(key, value)
    }
  }

  async function scanJsonFile(filePath: string) {
    try {
      const content = await fs.promises.readFile(filePath, 'utf8')
      const data = JSON.parse(content)
      if (data && typeof data === 'object') {
        for (const [k, v] of Object.entries<any>(data)) consider(k, v)
      }
    } catch {
      // ignore parse or IO errors
    }
  }

  async function walk(dir: string) {
    const entries = await fs.promises.readdir(dir, { withFileTypes: true })
    for (const ent of entries) {
      const full = path.join(dir, ent.name)
      if (ent.isDirectory()) await walk(full)
      else if (ent.isFile() && ent.name.toLowerCase().endsWith('.json')) await scanJsonFile(full)
    }
  }

  if (fs.existsSync(baseDir)) await walk(baseDir)

  const out: TypeHints = {}
  for (const [k, c] of Object.entries(counts)) {
    if (BOOLEAN_KEY_REGEX.test(k)) out[k] = 'boolean'
    else if (c.bool > c.num) out[k] = 'boolean'
    else if (c.num > 0) out[k] = 'number'
  }
  return out
}

/**
 * Normalize a profile object using type hints and safe heuristics.
 * - Converts numeric strings (including percentages like "70%") to numbers
 * - Converts boolean-like strings ("true"/"false"/"1"/"0") to booleans
 * - Preserves excluded keys and unknown complex structures
 */
export function normalizeProfile(profile: any, hints: TypeHints): any {
  if (!profile || typeof profile !== 'object') return profile

  const out: any = Array.isArray(profile) ? [] : {}

  const normalizeValue = (key: string, value: any): any => {
    if (value === null || value === undefined) return value
    if (EXCLUDED_KEYS_REGEX.test(key)) return value

    const hint = hints[key]

    const convertOne = (val: any) => {
      if (val === null || val === undefined) return val
      if (typeof val === 'number' || typeof val === 'boolean') return val
      if (typeof val === 'string') {
        if (hint === 'boolean') {
          const b = toBooleanFromString(val)
          if (b !== undefined) return b
        }
        if (hint === 'number') {
          if (isPercentString(val) || isNumericString(val)) return toNumberFromString(val)
        }
        // Without a hint, apply conservative heuristics
        const b2 = toBooleanFromString(val)
        if (b2 !== undefined && BOOLEAN_KEY_REGEX.test(key)) return b2
        if (isPercentString(val) || isNumericString(val)) return toNumberFromString(val)
        return val
      }
      return val
    }

    if (Array.isArray(value)) {
      const mapped = value.map((el) => convertOne(el))
      // Flatten arrays of length 1 for numeric/boolean scalar-like keys
      if (mapped.length === 1 && !LIKELY_LIST_KEYS_REGEX.test(key)) {
        const only = mapped[0]
        if (typeof only === 'number' || typeof only === 'boolean') return only
      }
      return mapped
    }

    return convertOne(value)
  }

  for (const [k, v] of Object.entries<any>(profile)) {
    if (Array.isArray(v)) out[k] = normalizeValue(k, v)
    else if (v && typeof v === 'object') out[k] = normalizeProfile(v, hints)
    else out[k] = normalizeValue(k, v)
  }

  return out
}

