/**
 * Sanitizes BBL (Bambu Lab) proprietary G-code template variables
 * that don't exist in the OrcaSlicer fork used by this addon.
 *
 * BBL profiles contain G-code templates referencing proprietary variables
 * like `flush_volumetric_speeds` and `flush_temperatures`.  These cause
 * PlaceholderParser errors during export_gcode.  Additionally,
 * `previous_extruder` starts at -1 for single-extruder setups, causing
 * "negative index" errors when used as a vector index.
 *
 * This module patches the template strings *before* passing them to the
 * C++ addon so the PlaceholderParser never sees unknown identifiers.
 */

/** G-code template config keys that may contain BBL placeholders */
const GCODE_TEMPLATE_KEYS = [
  'machine_start_gcode',
  'machine_end_gcode',
  'change_filament_gcode',
  'filament_start_gcode',
  'filament_end_gcode',
  'layer_change_gcode',
  'time_lapse_gcode',
  'printing_by_object_gcode',
  'before_layer_change_gcode'
] as const

/**
 * BBL-proprietary variable names that are NOT defined in this OrcaSlicer
 * fork.  When they appear in a template we replace them with safe literal
 * fallbacks so the PlaceholderParser doesn't throw "Not a variable name".
 *
 * Format: variable name → default literal replacement value
 */
const BBL_PROPRIETARY_VARS: Record<string, string> = {
  flush_volumetric_speeds: '0',
  flush_temperatures: '220'
}

/**
 * Sanitize a single G-code template string.
 *
 * Handles two cases:
 * 1. BBL proprietary variables used as `{var[idx]}` or in arithmetic
 *    expressions — replaced with safe literal values.
 * 2. Lines that index vectors with `previous_extruder` — wrapped in
 *    a guard that checks `previous_extruder >= 0`.
 */
function sanitizeTemplate(template: string, key: string): string {
  if (!template || typeof template !== 'string') return template

  let result = template

  // --- 1. Replace BBL-proprietary variable references ---
  // Three syntax forms to handle:
  //   a) Legacy square brackets:  [flush_temperatures[idx]]  (outermost [] is legacy expansion)
  //   b) Curly brace expression:  {flush_volumetric_speeds[idx]/2.4053*60}
  //   c) Bare indexed reference:  flush_temperatures[idx]
  //
  // IMPORTANT: Legacy square bracket form must be replaced FIRST.
  // Otherwise replacing the inner var[idx] → literal leaves [literal]
  // which OrcaSlicer interprets as a legacy variable lookup and fails.
  for (const [varName, defaultVal] of Object.entries(BBL_PROPRIETARY_VARS)) {
    const escapedName = varName.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')

    // (a) Legacy square-bracket expansion: [var[idx]]
    // The outer [] is the legacy syntax, inner [idx] is the vector index.
    // Replace the ENTIRE match (including outer brackets) with the literal.
    const legacyPattern = new RegExp('\\[' + escapedName + '\\[[^\\]]*\\]\\]', 'g')
    result = result.replace(legacyPattern, defaultVal)

    // (b) Indexed reference inside curly braces or standalone: var[idx]
    const indexedPattern = new RegExp(escapedName + '\\[[^\\]]*\\]', 'g')
    result = result.replace(indexedPattern, defaultVal)

    // (c) Bare variable references (no indexing)
    const barePattern = new RegExp('(?<![a-zA-Z_])' + escapedName + '(?![a-zA-Z_\\[])', 'g')
    result = result.replace(barePattern, defaultVal)
  }

  // --- 2. Guard previous_extruder vector accesses ---
  // In change_filament_gcode, lines like:
  //   {if nozzle_temperature[previous_extruder] > 142 && next_extruder < 255}
  //   {if long_retractions_when_cut[previous_extruder]}
  // fail because previous_extruder = -1 on first call.
  //
  // Strategy: wrap `{if EXPR[previous_extruder]...}` blocks so the
  // condition is only evaluated when previous_extruder >= 0.
  //
  // We look for `{if ...` lines containing `[previous_extruder]` and
  // prepend a guard: `{if previous_extruder >= 0 && (original_condition)}`
  if (result.includes('previous_extruder')) {
    // Match {if ...condition containing [previous_extruder]...}
    result = result.replace(
      /\{if\s+((?:(?!\{(?:if|elsif|else|endif)).)*?\[previous_extruder\].*?)\}/g,
      (match, condition) => {
        // If already guarded, skip
        if (condition.includes('previous_extruder >= 0') || condition.includes('previous_extruder>= 0')) {
          return match
        }
        return `{if previous_extruder >= 0 && (${condition.trim()})}`
      }
    )

    // Same for {elsif ...}
    result = result.replace(
      /\{elsif\s+((?:(?!\{(?:if|elsif|else|endif)).)*?\[previous_extruder\].*?)\}/g,
      (match, condition) => {
        if (condition.includes('previous_extruder >= 0') || condition.includes('previous_extruder>= 0')) {
          return match
        }
        return `{elsif previous_extruder >= 0 && (${condition.trim()})}`
      }
    )
  }

  return result
}

/**
 * Sanitize all G-code template fields in a slicer options/config object.
 * Mutates the object in place and returns it for convenience.
 *
 * Safe to call even when no BBL templates are present — non-template
 * keys are ignored and template strings without BBL variables are
 * returned unchanged.
 */
export function sanitizeBblGcodeTemplates<T extends Record<string, any>>(opts: T): T {
  for (const key of GCODE_TEMPLATE_KEYS) {
    const val = opts[key]
    if (typeof val === 'string') {
      ;(opts as any)[key] = sanitizeTemplate(val, key)
    } else if (Array.isArray(val)) {
      // Some G-code template fields can be arrays (per-extruder)
      ;(opts as any)[key] = val.map((v: any) => (typeof v === 'string' ? sanitizeTemplate(v, key) : v))
    }
  }
  return opts
}
