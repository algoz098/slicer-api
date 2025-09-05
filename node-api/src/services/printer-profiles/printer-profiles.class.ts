// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'

import type { Application } from '../../declarations'
import * as fs from 'fs'
import * as path from 'path'
import type {
  PrinterProfiles,
  PrinterProfilesData,
  PrinterProfilesPatch,
  PrinterProfilesQuery
} from './printer-profiles.schema'

export type { PrinterProfiles, PrinterProfilesData, PrinterProfilesPatch, PrinterProfilesQuery }

export interface PrinterProfilesServiceOptions {
  app: Application
}

export interface PrinterProfilesParams extends Params<PrinterProfilesQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class PrinterProfilesService<ServiceParams extends PrinterProfilesParams = PrinterProfilesParams>
  implements ServiceInterface<PrinterProfiles, PrinterProfilesData, ServiceParams, PrinterProfilesPatch>
{

  // Resolve basePath with fallbacks so service works both in dev and compiled contexts
  private basePath: string = (() => {
    const candidates = [
      path.join(__dirname, '../../../config/orcaslicer/profiles/resources/profiles'), // when compiled into lib
      path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'), // running inside node-api dir
      path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'), // running from repo root
      path.join(process.cwd(), 'OrcaSlicer/resources/profiles'), // alternate location
      path.join(process.cwd(), 'node-api/OrcaSlicer/resources/profiles')
    ]
    for (const c of candidates) if (fs.existsSync(c)) return c
    return candidates[0]
  })()
  private allowedTypesDefault = ['process', 'machine', 'machine_model']

  constructor(public options: PrinterProfilesServiceOptions) {}

  // Helpers
  private toIndex(id: Id): number {
    const idx = typeof id === 'number' ? id : Number(id)
    if (!Number.isInteger(idx) || idx < 0) throw new Error('Invalid id')
    return idx
  }

  // Resolve an entry by the string ID used in `find` (derived from profile.sub_path.replaceAll('/', '_-_')).
  private resolveEntryById(id: string, allowedTypes?: string[]) {
    const all = this.loadAllProfiles(allowedTypes || this.allowedTypesDefault)
    const idStr = String(id)
    const foundIdx = all.findIndex(it => ((it.profile.sub_path || '').replaceAll('/', '_-_')) === idStr)
    if (foundIdx === -1) return { entry: undefined, index: -1, all }
    return { entry: all[foundIdx], index: foundIdx, all }
  }

  private readJson(filePath: string) {
    return JSON.parse(fs.readFileSync(filePath, 'utf8'))
  }

  private writeJson(filePath: string, data: any) {
    fs.writeFileSync(filePath, JSON.stringify(data, null, 2))
  }

  private ensureNonProfileDir() {
    const d = path.join(this.basePath, '_non_profiles')
    if (!fs.existsSync(d)) fs.mkdirSync(d)
    return d
  }

  private moveToNonProfiles(full: string, fileName: string) {
    try {
      const dir = this.ensureNonProfileDir()
      let dest = path.join(dir, fileName)
      if (fs.existsSync(dest)) {
        const ext = path.extname(fileName)
        const base = path.basename(fileName, ext)
        let i = 1
        while (fs.existsSync(dest)) {
          dest = path.join(dir, `${base}_${i}${ext}`)
          i++
        }
      }
      fs.renameSync(full, dest)
    } catch (e) {
      // ignore move failures to avoid blocking profile loading
    }
  }

  private isProfileFile(full: string) {
    try {
      const data = this.readJson(full)
      return Array.isArray(data.process_list) && data.process_list.length > 0
    } catch (e) {
      return false
    }
  }

  /**
   * Loads all process profiles from every top-level .json file in basePath.
   * Returns a flat array with metadata to map a global index to file and local index.
   */
  private loadAllProfiles(allowedTypes?: string[]) {
    if (!fs.existsSync(this.basePath)) return []
    const result: Array<{ filePath: string; fileName: string; profile: any; localIndex: number }> = []

    const walk = (dir: string) => {
      let entries: string[]
      try { entries = fs.readdirSync(dir) } catch (e) { return }
      for (const f of entries) {
        const full = path.join(dir, f)
        let stat: fs.Stats
        try { stat = fs.statSync(full) } catch (e) { continue }
        if (stat.isDirectory()) { walk(full); continue }
        if (!f.toLowerCase().endsWith('.json')) continue

        // If file is not a profile descriptor, move it out and skip
        if (!this.isProfileFile(full)) {
          this.moveToNonProfiles(full, path.relative(this.basePath, full))
          continue
        }

        // Check top-level type and skip if not allowed. If type is missing, allow (some group files omit type)
        try {
          const meta = this.readJson(full)
          if (typeof meta.type === 'string') {
            if (Array.isArray(allowedTypes) && allowedTypes.length && !allowedTypes.includes(meta.type)) {
              continue
            }
          }
        } catch (e) {
          this.moveToNonProfiles(full, path.relative(this.basePath, full))
          continue
        }

        try {
          const data = this.readJson(full)
          const list = data.process_list || []
          for (let i = 0; i < list.length; i++) {
            // Ensure a stable sub_path exists so callers relying on it (e.g. find's ID logic)
            if (typeof list[i].sub_path !== 'string' || list[i].sub_path === '') {
              // default sub_path uses relative file name and local index
              list[i].sub_path = path.join(path.relative(this.basePath, full), String(i))
            }
            // Ensure a `text` alias exists for compatibility with callers/tests expecting it
            if (typeof list[i].text === 'undefined' && typeof list[i].name !== 'undefined') {
              list[i].text = list[i].name
            }
            result.push({ filePath: full, fileName: path.relative(this.basePath, full), profile: list[i], localIndex: i })
          }
        } catch (e) {
          this.moveToNonProfiles(full, path.relative(this.basePath, full))
        }
      }
    }

    walk(this.basePath)
    return result
  }


  // List all printer profiles across all top-level profile descriptor files
  async find(params?: ServiceParams): Promise<PrinterProfiles[]> {
  const q = (params && (params as any).query) || {}
  const requested = typeof q.type === 'string' ? q.type.split(',').map((s: string) => s.trim()) : undefined
  const allowed = requested && requested.length ? requested : this.allowedTypesDefault
  const all = this.loadAllProfiles(allowed)
    return all.map((it) => ({id: it.profile.sub_path.replaceAll('/', '_-_'), ...it.profile }))
  }


  // Get a specific profile by id (index in process_list)
  async get(id: string, _params?: ServiceParams): Promise<PrinterProfiles> {
  const resolved = this.resolveEntryById(id, this.allowedTypesDefault)
  const entry = resolved.entry
  if (!entry) throw new Error('Profile not found')
  const outId = (entry.profile.sub_path || '').replaceAll('/', '_-_')
  const fileContent = this.readJson(entry.filePath)
  return { id: outId, text: entry.profile.name, fileContent, ...entry.profile }
  }


  // Add a new printer profile to Vzbot.json
  async create(data: PrinterProfilesData, params?: ServiceParams): Promise<PrinterProfiles> {
    // Determine target file: prefer query.source (must be allowed profile file), else Vzbot.json if profile+allowed,
    // else first available profile file with allowed type.
    const preferred = (params && (params as any).query && (params as any).query.source) as string | undefined
    let targetFile = ''
    const allowed = this.allowedTypesDefault
    if (preferred) {
      const candidate = path.join(this.basePath, preferred)
      if (!fs.existsSync(candidate)) throw new Error('Target profile file not found')
      if (!this.isProfileFile(candidate)) throw new Error('Target file is not a profile file')
      const meta = this.readJson(candidate)
      if (!allowed.includes(meta.type)) throw new Error('Target file type not allowed')
      targetFile = candidate
    } else {
      const vz = path.join(this.basePath, 'Vzbot.json')
      if (fs.existsSync(vz) && this.isProfileFile(vz)) {
        const meta = this.readJson(vz)
        if (allowed.includes(meta.type)) targetFile = vz
      }
      if (!targetFile) {
        // find any profile file with allowed type
        const files = fs.readdirSync(this.basePath).filter(f => f.toLowerCase().endsWith('.json'))
        for (const f of files) {
          const full = path.join(this.basePath, f)
          if (!this.isProfileFile(full)) continue
          const meta = this.readJson(full)
          if (allowed.includes(meta.type)) { targetFile = full; break }
        }
      }
      if (!targetFile) throw new Error('No allowed profile files available')
    }

    const fileData = this.readJson(targetFile)
    fileData.process_list = fileData.process_list || []
    const newProfile = { name: data.text, sub_path: path.join(path.relative(this.basePath, targetFile), String(fileData.process_list.length)) }
    fileData.process_list.push(newProfile)
    this.writeJson(targetFile, fileData)

  // Rebuild global index and find the created entry, return ID in the same format as `find`
  const all = this.loadAllProfiles(allowed)
  const globalIdx = all.findIndex(it => it.filePath === targetFile && it.localIndex === fileData.process_list.length - 1)
  if (globalIdx === -1) throw new Error('Failed to locate created profile')
  const created = all[globalIdx]
  const outId = (created.profile.sub_path || '').replaceAll('/', '_-_')
  const fileContent = this.readJson(targetFile)
  return { id: outId, text: data.text, fileContent }
  }

  // This method has to be added to the 'methods' option to make it available to clients

  // Update a printer profile by id
  async update(id: string, data: PrinterProfilesData, _params?: ServiceParams): Promise<PrinterProfiles> {
  const resolved = this.resolveEntryById(id, this.allowedTypesDefault)
  const entry = resolved.entry
  if (!entry) throw new Error('Profile not found')
  const fileData = this.readJson(entry.filePath)
  fileData.process_list = fileData.process_list || []
  fileData.process_list[entry.localIndex] = { ...fileData.process_list[entry.localIndex], name: data.text }
  this.writeJson(entry.filePath, fileData)
  const outId = (entry.profile.sub_path || '').replaceAll('/', '_-_')
  return { id: outId, text: data.text, fileContent: fileData }
  }


  // Patch a printer profile by id (partial update)
  async patch(id: string, data: PrinterProfilesPatch, _params?: ServiceParams): Promise<PrinterProfiles> {
  const resolved = this.resolveEntryById(id, this.allowedTypesDefault)
  const entry = resolved.entry
  if (!entry) throw new Error('Profile not found')
  const fileData = this.readJson(entry.filePath)
  fileData.process_list = fileData.process_list || []
  fileData.process_list[entry.localIndex] = { ...fileData.process_list[entry.localIndex], ...data }
  this.writeJson(entry.filePath, fileData)
  const updated = fileData.process_list[entry.localIndex]
  const outId = (updated.sub_path || entry.profile.sub_path || '').replaceAll('/', '_-_')
  return { id: outId, fileContent: fileData, ...updated }
  }


  // Remove a printer profile by id
  async remove(id: string, _params?: ServiceParams): Promise<PrinterProfiles> {
  const resolved = this.resolveEntryById(id, this.allowedTypesDefault)
  const entry = resolved.entry
  if (!entry) throw new Error('Profile not found')
  const fileData = this.readJson(entry.filePath)
  fileData.process_list = fileData.process_list || []
  const removed = fileData.process_list.splice(entry.localIndex, 1)[0]
  this.writeJson(entry.filePath, fileData)
  const outId = (removed && removed.sub_path ? removed.sub_path : entry.profile.sub_path || '').replaceAll('/', '_-_')
  return { id: outId, text: removed ? removed.name : '', fileContent: fileData }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
