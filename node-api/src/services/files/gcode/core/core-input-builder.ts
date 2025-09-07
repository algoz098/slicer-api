import * as os from 'os'
import * as fs from 'fs'
import type { Application } from '../../../../declarations'
import type { CoreInput } from '../../../../types/slicer'
import { FileUploadHelper } from '../../../../utils/file-upload-handler'
import { ThreeMFProcessor } from '../../../../utils/3mf-processor'
import { resolvePlaceholders } from '../../../../utils/profile-placeholders'
import { resolveProfilesBasePath } from '../../../../utils/profile-base-path'
import { ProfileInheritanceResolver } from '../../../filaments-profile/profile-inheritance-resolver'

export class CoreInputBuilder {
  constructor(private app: Application) {}

  /**
   * Constrói CoreInput a partir de um upload (3MF em memória ou disco) e parâmetros adicionais.
   * Reutiliza serviços existentes (files/info, printer-profiles, filaments-profile) e os estende conforme necessário.
   */
  async fromUploadedFile(file: { buffer?: Buffer; path?: string; originalFilename?: string; name?: string }, opts?: Partial<CoreInput>): Promise<CoreInput> {
    const fileName = file.originalFilename || file.name || 'model.3mf'
    const tempFilePath = FileUploadHelper.createTempFilePath(fileName, os.tmpdir())

    // Persistir 3MF em temp
    if (file.buffer) await fs.promises.writeFile(tempFilePath, file.buffer)
    else if (file.path) await fs.promises.copyFile(file.path, tempFilePath)
    else throw new Error('Invalid uploaded file')

    // Extrair metadados do 3MF (printer/profile/nozzle) para orientar seleção de perfis
    const extracted = await new ThreeMFProcessor().extractProfileInfo(tempFilePath).catch(() => null)

    // Selecionar perfis aproximados (WIP): buscar por nomes que coincidam
    const printerProfilesService = this.app.service('printer-profiles') as any
    const filamentsService = this.app.service('filaments-profile') as any

    // Carregar listas básicas
    const [allPrinterProfiles, allFilaments] = await Promise.all([
      printerProfilesService.find({ query: {} }),
      filamentsService.find({ query: {} })
    ])

    // Heurísticas simples de matching por agora (será expandido com herança/placeholder):
    const matchByText = (list: any[], needle?: string) => {
      if (!needle) return undefined
      const n = needle.toLowerCase()
      return list.find((p: any) => typeof p.text === 'string' && p.text.toLowerCase().includes(n))
    }

    const processProfile = matchByText(allPrinterProfiles, extracted?.profile || extracted?.technicalName)
      || matchByText(allPrinterProfiles, extracted?.printer)

    const filamentProfile = matchByText(allFilaments, extracted?.technicalName)
      || matchByText(allFilaments, extracted?.profile)

    // Expandir perfis completos via get(id) quando disponível
    let processFull: any = {}
    let filamentFull: any = {}
    try { if (processProfile?.id) processFull = await printerProfilesService.get(processProfile.id) } catch {}
    try { if (filamentProfile?.id) filamentFull = await filamentsService.get(filamentProfile.id) } catch {}

    // Resolver herança com base nos nomes (quando houver "inherits")
    const basePath = resolveProfilesBasePath()
    const processName = (processFull?.name as string) || (processFull?.text as string)
    const filamentName = (filamentFull?.name as string) || (filamentFull?.text as string)

    if (processName) {
      try {
        const resolved = await ProfileInheritanceResolver.resolveProfile(processName, { profileBasePath: basePath, profileType: 'process' })
        processFull = { ...resolved.data }
      } catch {}
    }

    if (filamentName) {
      try {
        const resolved = await ProfileInheritanceResolver.resolveProfile(filamentName, { profileBasePath: basePath, profileType: 'filament' })
        filamentFull = { ...resolved.data }
      } catch {}
    }

    const profiles = { machine: {}, process: processFull, filament: filamentFull }

    // Derivar parâmetros comuns (temperaturas, motion, flavor) a partir dos perfis/arquivo
    const flavor = (opts?.flavor as string) || (processFull as any)?.gcode_flavor || 'Marlin'

    const pickNumber = (...candidates: any[]): number | undefined => {
      for (const c of candidates) {
        if (typeof c === 'number' && !Number.isNaN(c)) return c
        if (typeof c === 'string') {
          const n = parseFloat(c)
          if (!Number.isNaN(n)) return n
        }
      }
      return undefined
    }

    const temperatures = {
      bed: pickNumber((processFull as any)?.bed_temperature, (filamentFull as any)?.bed_temperature, extracted?.printSettings?.bedTemp),
      nozzle: pickNumber((processFull as any)?.nozzle_temperature, (filamentFull as any)?.nozzle_temperature, extracted?.printSettings?.nozzleTemp),
      chamber: pickNumber((processFull as any)?.chamber_temperature, (filamentFull as any)?.chamber_temperature)
    }

    const motion = {
      accel: pickNumber((processFull as any)?.acceleration, (processFull as any)?.default_acceleration),
      travelAccel: pickNumber((processFull as any)?.travel_acceleration),
      jerk: {
        x: pickNumber((processFull as any)?.machine_max_jerk_x),
        y: pickNumber((processFull as any)?.machine_max_jerk_y),
        z: pickNumber((processFull as any)?.machine_max_jerk_z),
        e: pickNumber((processFull as any)?.machine_max_jerk_e)
      },
      speedLimits: {
        print: pickNumber((processFull as any)?.print_speed, extracted?.printSettings?.printSpeed) as any,
        travel: pickNumber((processFull as any)?.travel_speed) as any
      }
    }

    // Placeholder resolution example (for custom gcode fields)
    const machineStart = (processFull as any)?.machine_start_gcode || ''
    const machineEnd = (processFull as any)?.machine_end_gcode || ''
    const ctxPH = { temperatures, layer: { firstLayerHeight: extracted?.printSettings?.layerHeight } }
    const machineStartResolved = typeof machineStart === 'string' ? resolvePlaceholders(machineStart, ctxPH) : ''
    const machineEndResolved = typeof machineEnd === 'string' ? resolvePlaceholders(machineEnd, ctxPH) : ''

    // Attach resolved strings back to process profile (non-destructive)
    ;(profiles.process as any) = { ...(profiles.process as any), machine_start_gcode_resolved: machineStartResolved, machine_end_gcode_resolved: machineEndResolved }

    const coreInput: CoreInput = {
      threeMfPath: tempFilePath,
      profiles,
      flavor,
      units: 'mm',
      temperatures: { ...temperatures, ...(opts?.temperatures || {}) },
      motion: { ...motion, ...(opts?.motion || {}) },
      options: { ...(opts?.options || {}) }
    }

    return coreInput
  }
}

