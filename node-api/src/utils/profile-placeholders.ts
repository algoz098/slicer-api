/**
 * Placeholder resolver for Orca-style profile strings (e.g., machine_start_gcode with [first_layer_temperature]).
 * This is a minimal, extensible approach; expand mapping conforme necessário.
 */
export type PlaceholderContext = {
  temperatures?: { bed?: number; nozzle?: number; chamber?: number }
  layer?: { firstLayerHeight?: number }
  motion?: { accel?: number }
  [k: string]: any
}

const DEFAULTS: Required<PlaceholderContext> = {
  temperatures: { bed: 0, nozzle: 0, chamber: 0 },
  layer: { firstLayerHeight: 0.2 },
  motion: { accel: 0 }
}

export function resolvePlaceholders(input: string, ctx: PlaceholderContext): string {
  const c = { ...DEFAULTS, ...ctx, temperatures: { ...DEFAULTS.temperatures, ...(ctx?.temperatures || {}) } }
  const table: Record<string, string | number> = {
    '[first_layer_temperature]': c.temperatures?.nozzle ?? 0,
    '[first_layer_bed_temperature]': c.temperatures?.bed ?? 0,
    '[chamber_temperature]': c.temperatures?.chamber ?? 0,
    '[first_layer_height]': c.layer?.firstLayerHeight ?? 0.2
  }
  return input.replace(/\[(first_layer_temperature|first_layer_bed_temperature|chamber_temperature|first_layer_height)\]/g, (m) => String(table[m] ?? m))
}

