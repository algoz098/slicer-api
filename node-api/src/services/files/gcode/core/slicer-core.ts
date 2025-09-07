import type { CoreInput, SlicerCore, ToolpathGraph } from '../../../../types/slicer'

// Fachada TS; tenta usar addon nativo. Enquanto não existir, lança ou usa fallback.
export class SlicerCoreFacade implements SlicerCore {
  private addon: any | null = null
  constructor() {
    try {
      // Try direct path first (most reliable in our setup)
      const path = require('path')
      const addonPath = path.join(process.cwd(), 'native/orcaslicer_core/build/Release/orcaslicer_core.node')
      this.addon = require(addonPath)
    } catch (directError) {
      try {
        // Fallback to bindings if available
        this.addon = require('bindings')('orcaslicer_core')
      } catch (bindingsError) {
        // Silent fallback to TS implementation
        this.addon = null
      }
    }
  }

  async slice(input: CoreInput): Promise<ToolpathGraph> {
    // Sem addon, por enquanto sem slicing real
    return { layers: [] }
  }

  async sliceToGcode(input: CoreInput): Promise<string> {
    if (this.addon && typeof this.addon.slice_to_gcode === 'function') {
      // Chamar addon nativo que retorna G-code direto
      const gcode: string = await this.addon.slice_to_gcode(input.threeMfPath, JSON.stringify(input))
      return gcode
    }

    // Fallback: usar Writer TS
    const { WriterFacadeTs } = await import('./writer')
    return new WriterFacadeTs().generate(input)
  }
}

