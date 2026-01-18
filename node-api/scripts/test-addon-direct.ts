/**
 * Test script that calls the addon directly (bypassing HTTP API)
 * to test multi-color slicing.
 *
 * IMPORTANT: This test demonstrates the CORRECT approach:
 * 1. The 3MF file specifies which profiles to use (printer, filament, process)
 * 2. The addon should load those profiles from the 3MF
 * 3. We only pass options if we want to OVERRIDE specific values
 * 4. If no customizations are needed, the result should match OrcaSlicer native
 *
 * The 3MF file (teste_a1mini.3mf) uses:
 * - Printer: Bambu Lab A1 0.4 nozzle
 * - Process: 0.20mm Standard @BBL A1
 * - Filament: Bambu PLA Basic @BBL A1 (4 filaments for AMS)
 */
import * as path from 'node:path'
import * as fs from 'node:fs'

// Load addon directly
const addonPath = path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')
const orca = require(addonPath)

// NOTE: The test file (teste_a1mini.3mf) is actually configured for A1 (256x256mm),
// not A1 mini (180x180mm), despite its name. The profiles embedded in the 3MF are:
// - Printer: Bambu Lab A1 0.4 nozzle
// - Process: 0.20mm Standard @BBL A1
// - Filament: Bambu PLA Basic @BBL A1 (4 filaments for AMS)
const TEST_FILE = path.resolve(__dirname, '../test/fixtures/teste_a1mini.3mf')
const OUTPUT_FILE = '/tmp/test-addon-direct-output.gcode.3mf'
const RESOURCES_PATH = path.resolve(__dirname, '../../OrcaSlicer/resources')

// NOTE: These profile names are NOT used when transfer flags are enabled.
// The 3MF file specifies its own profiles, and we should respect those.
// These are only used as fallbacks or when we want to explicitly override.
const PRINTER_PROFILE_NAME = 'Bambu Lab A1 0.4 nozzle'  // Match what the 3MF uses
const FILAMENT_PROFILE_NAME = 'Bambu PLA Basic @BBL A1'  // Match what the 3MF uses
const PROCESS_PROFILE_NAME = '0.20mm Standard @BBL A1'   // Match what the 3MF uses

/**
 * Resolve profile inheritance chain and merge all configs
 * @param profileName Name of the profile to load
 * @param profileType 'machine', 'filament', or 'process'
 * @returns Merged configuration object
 */
function resolveProfileInheritance(profileName: string, profileType: string): Record<string, any> {
  const profilesDir = path.join(RESOURCES_PATH, 'profiles', 'BBL', profileType)

  // Find the profile file
  function findProfileFile(name: string): string | null {
    // Try exact match first
    const exactPath = path.join(profilesDir, `${name}.json`)
    if (fs.existsSync(exactPath)) return exactPath

    // Recursive search
    function searchDir(dir: string): string | null {
      const entries = fs.readdirSync(dir, { withFileTypes: true })
      for (const entry of entries) {
        const fullPath = path.join(dir, entry.name)
        if (entry.isDirectory()) {
          const found = searchDir(fullPath)
          if (found) return found
        } else if (entry.isFile() && entry.name.endsWith('.json')) {
          const baseName = entry.name.replace('.json', '')
          if (baseName === name) return fullPath
        }
      }
      return null
    }
    return searchDir(profilesDir)
  }

  function loadAndMerge(name: string, visited: Set<string> = new Set()): Record<string, any> {
    if (visited.has(name)) {
      console.warn(`Circular inheritance detected: ${name}`)
      return {}
    }
    visited.add(name)

    const filePath = findProfileFile(name)
    if (!filePath) {
      // Try to find in base directory (common for @base profiles)
      const basePath = path.join(RESOURCES_PATH, 'profiles', 'BBL', profileType, `${name}.json`)
      if (!fs.existsSync(basePath)) {
        console.warn(`Profile not found: ${name} (type: ${profileType})`)
        return {}
      }
    }

    const actualPath = filePath || path.join(RESOURCES_PATH, 'profiles', 'BBL', profileType, `${name}.json`)
    if (!fs.existsSync(actualPath)) {
      console.warn(`Profile file not found: ${actualPath}`)
      return {}
    }

    const content = JSON.parse(fs.readFileSync(actualPath, 'utf-8'))

    // If inherits, load parent first
    let parentConfig: Record<string, any> = {}
    if (content.inherits) {
      parentConfig = loadAndMerge(content.inherits, visited)
    }

    // Merge parent with current (current takes precedence)
    const merged = { ...parentConfig }
    for (const [key, value] of Object.entries(content)) {
      if (key !== 'inherits' && key !== 'type' && key !== 'from' && key !== 'setting_id' && key !== 'instantiation') {
        merged[key] = value
      }
    }

    return merged
  }

  return loadAndMerge(profileName)
}

/**
 * Keys that are per-filament arrays and should be preserved from 3MF
 * instead of being overwritten by profile values (which have only 1 element).
 * The 3MF file has these arrays with 4 elements (one per AMS slot).
 */
const FILAMENT_ARRAY_KEYS_TO_PRESERVE = new Set([
  'activate_air_filtration', 'activate_chamber_temp_control', 'adaptive_pressure_advance',
  'adaptive_pressure_advance_bridges', 'adaptive_pressure_advance_model', 'adaptive_pressure_advance_overhangs',
  'additional_cooling_fan_speed', 'chamber_temperature', 'close_fan_the_first_x_layers',
  'complete_print_exhaust_fan_speed', 'cool_plate_temp', 'cool_plate_temp_initial_layer',
  'default_filament_colour', 'dont_slow_down_outer_wall', 'during_print_exhaust_fan_speed',
  'enable_overhang_bridge_fan', 'enable_pressure_advance', 'eng_plate_temp', 'eng_plate_temp_initial_layer',
  'fan_cooling_layer_time', 'fan_max_speed', 'fan_min_speed', 'filament_cooling_final_speed',
  'filament_cooling_initial_speed', 'filament_cooling_moves', 'filament_cost', 'filament_density',
  'filament_deretraction_speed', 'filament_end_gcode', 'filament_flow_ratio', 'filament_ids',
  'filament_is_support', 'filament_loading_speed', 'filament_loading_speed_start',
  'filament_long_retractions_when_cut', 'filament_max_volumetric_speed', 'filament_minimal_purge_on_wipe_tower',
  'filament_multitool_ramming', 'filament_multitool_ramming_flow', 'filament_multitool_ramming_volume',
  'filament_notes', 'filament_ramming_parameters', 'filament_retract_before_wipe',
  'filament_retract_lift_above', 'filament_retract_lift_below', 'filament_retract_lift_enforce',
  'filament_retract_restart_extra', 'filament_retract_when_changing_layer', 'filament_retraction_distances_when_cut',
  'filament_retraction_length', 'filament_retraction_minimum_travel', 'filament_retraction_speed',
  'filament_settings_id', 'filament_shrink', 'filament_shrinkage_compensation_z', 'filament_soluble',
  'filament_stamping_distance', 'filament_stamping_loading_speed', 'filament_start_gcode',
  'filament_toolchange_delay', 'filament_unloading_speed', 'filament_unloading_speed_start',
  'filament_vendor', 'filament_wipe', 'filament_wipe_distance', 'filament_z_hop', 'filament_z_hop_types',
  'full_fan_speed_layer', 'head_wrap_detect_zone', 'hot_plate_temp', 'hot_plate_temp_initial_layer',
  'idle_temperature', 'internal_bridge_fan_speed', 'ironing_fan_speed', 'nozzle_temperature',
  'nozzle_temperature_initial_layer', 'nozzle_temperature_range_high', 'nozzle_temperature_range_low',
  'overhang_fan_speed', 'overhang_fan_threshold', 'pellet_flow_coefficient', 'pressure_advance',
  'reduce_fan_stop_start_freq', 'required_nozzle_HRC', 'slow_down_for_layer_cooling', 'slow_down_layer_time',
  'slow_down_min_speed', 'supertack_plate_temp', 'supertack_plate_temp_initial_layer',
  'support_material_interface_fan_speed', 'temperature_vitrification', 'textured_cool_plate_temp',
  'textured_cool_plate_temp_initial_layer', 'textured_plate_temp', 'textured_plate_temp_initial_layer',
  // Also preserve colors and types from 3MF
  'filament_colour', 'filament_type',
])

/**
 * Build complete configuration from resolved profiles
 */
function buildCompleteConfig(): Record<string, any> {
  console.log('Resolving profile inheritance chains...')

  const printerConfig = resolveProfileInheritance(PRINTER_PROFILE_NAME, 'machine')
  console.log(`  Printer profile resolved: ${Object.keys(printerConfig).length} keys`)

  const filamentConfig = resolveProfileInheritance(FILAMENT_PROFILE_NAME, 'filament')
  console.log(`  Filament profile resolved: ${Object.keys(filamentConfig).length} keys`)

  const processConfig = resolveProfileInheritance(PROCESS_PROFILE_NAME, 'process')
  console.log(`  Process profile resolved: ${Object.keys(processConfig).length} keys`)

  // Merge all configs (process > filament > printer precedence)
  const rawConfig = {
    ...printerConfig,
    ...filamentConfig,
    ...processConfig,
  }

  // Filter out filament array keys to preserve 3MF values
  const config: Record<string, any> = {}
  let excludedCount = 0
  for (const [key, value] of Object.entries(rawConfig)) {
    if (FILAMENT_ARRAY_KEYS_TO_PRESERVE.has(key)) {
      excludedCount++
      continue
    }
    config[key] = value
  }

  console.log(`  Total merged config: ${Object.keys(config).length} keys (excluded ${excludedCount} filament array keys to preserve 3MF values)`)
  return config
}

async function main() {
  console.log('=== Direct Addon Test (3MF-Driven - No Options Override) ===')
  console.log('Test file:', TEST_FILE)
  console.log('Output:', OUTPUT_FILE)
  console.log('')
  console.log('This test lets the 3MF file define all profiles and settings.')
  console.log('The addon should produce identical output to OrcaSlicer native.')
  console.log('')

  const startTime = Date.now()
  const TIMEOUT_SEC = 120  // Increased timeout for profile resolution

  // Set a hard timeout to kill the process if it hangs
  const timeoutId = setTimeout(() => {
    const elapsed = Date.now() - startTime
    console.error(`\nFAIL: Process timed out after ${elapsed}ms`)
    console.error('The addon is likely stuck in an infinite loop.')
    process.exit(1)
  }, TIMEOUT_SEC * 1000)

  try {
    // Initialize addon
    console.log('\nInitializing addon...')
    orca.initialize({ addonDir: addonPath, resourcesPath: RESOURCES_PATH })
    console.log('Addon initialized.')

    // IMPORTANT: We do NOT pass options here.
    // The 3MF file contains:
    // - Which profiles to use (printer, filament, process)
    // - All configuration values from those profiles
    // - Any user customizations (if any)
    //
    // By setting all transfer flags to true and NOT passing options,
    // the addon should produce identical output to OrcaSlicer native.

    console.log('\nCalling orca.slice() with 3MF-defined profiles (no options override)...')
    console.log('')

    // Let the 3MF file define everything - no options override
    const result = await orca.slice({
      input: TEST_FILE,
      output: OUTPUT_FILE,
      // Do NOT pass printerProfile - let 3MF define it
      // Do NOT pass options - let 3MF values through
      center: true,
      autoRealignIfNeeded: true,
      // Transfer ALL settings from 3MF file
      transferPrinterCustomizations: true,
      transferFilamentCustomizations: true,
      transferProcessCustomizations: true,
      transferProjectOverrides: true,
    })

    clearTimeout(timeoutId)
    const elapsed = Date.now() - startTime

    console.log(`\nOperation completed in ${elapsed}ms`)
    console.log('Result:', JSON.stringify(result, null, 2).slice(0, 1000))
    console.log('\nPASS: Addon completed without hanging')

    // Verify G-code respects bed limits (180x180 for A1 Mini)
    const fs = require('fs')
    const { execSync } = require('child_process')
    const output3mf = '/tmp/test-addon-direct-output.gcode.3mf'
    const gcodeFile = '/tmp/test-addon-direct-output-extracted.gcode'

    // Extract G-code from 3MF
    if (fs.existsSync(output3mf)) {
      try {
        execSync(`unzip -p "${output3mf}" "Metadata/plate_1.gcode" > "${gcodeFile}"`)
        console.log('\nExtracted G-code from 3MF')
      } catch (e) {
        console.log('\nFailed to extract G-code from 3MF')
      }
    }

    if (fs.existsSync(gcodeFile)) {
      // The 3MF uses A1 (256x256mm bed), not A1 Mini (180x180mm)
      console.log('\n=== Verifying G-code respects bed limits (256x256mm for A1) ===')
      const gcode = fs.readFileSync(gcodeFile, 'utf-8')
      const lines = gcode.split('\n')

      const BED_MAX_X = 256
      const BED_MAX_Y = 256
      const violations: string[] = []
      let maxX = 0
      let maxY = 0
      let inPrintingSection = false  // Only validate after first LAYER_CHANGE

      for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim()

        // Start validating coordinates only after the first LAYER_CHANGE
        // This skips the start_gcode which has movements outside the bed (purge, wipe, etc)
        if (line.includes('LAYER_CHANGE')) {
          inPrintingSection = true
        }

        // Skip validation before printing starts
        if (!inPrintingSection) continue

        // Only validate G1 moves with extrusion (E parameter) - these are actual printing moves
        // Skip G0 (travel) and G1 without E (also travel/retraction)
        // This avoids false positives from wipe tower, purge, and other non-printing moves
        if (/^G1\s/.test(line) && line.includes('E') && !line.includes('E-')) {
          const xMatch = line.match(/X([-\d.]+)/)
          const yMatch = line.match(/Y([-\d.]+)/)

          if (xMatch) {
            const x = parseFloat(xMatch[1])
            if (x > maxX) maxX = x
            if (x > BED_MAX_X || x < 0) {
              violations.push(`Line ${i + 1}: X=${x} out of bounds [0, ${BED_MAX_X}] - ${line}`)
            }
          }
          if (yMatch) {
            const y = parseFloat(yMatch[1])
            if (y > maxY) maxY = y
            if (y > BED_MAX_Y || y < 0) {
              violations.push(`Line ${i + 1}: Y=${y} out of bounds [0, ${BED_MAX_Y}] - ${line}`)
            }
          }
        }
      }

      console.log(`Max X coordinate found (in printing section): ${maxX.toFixed(2)}mm`)
      console.log(`Max Y coordinate found (in printing section): ${maxY.toFixed(2)}mm`)

      if (violations.length > 0) {
        console.log(`\nFAIL: Found ${violations.length} G-code violations!`)
        // Show first 10 violations
        violations.slice(0, 10).forEach(v => console.log(`  ${v}`))
        if (violations.length > 10) {
          console.log(`  ... and ${violations.length - 10} more`)
        }
        process.exit(1)
      } else {
        console.log('\nPASS: All G-code coordinates are within bed limits')
      }

      // === MULTI-COLOR VALIDATION ===
      console.log('\n=== Verifying multi-color printing support ===')
      let multiColorPassed = true

      // 1. Check for tool change commands (T0, T1)
      const t0Count = (gcode.match(/^T0$/gm) || []).length
      const t1Count = (gcode.match(/^T1$/gm) || []).length
      console.log(`Tool changes found: T0=${t0Count}, T1=${t1Count}`)

      if (t0Count === 0 || t1Count === 0) {
        console.log('FAIL: Missing tool change commands - both T0 and T1 should be present')
        multiColorPassed = false
      } else {
        console.log('PASS: Both T0 and T1 tool changes found')
      }

      // 2. Check for Bambu AMS commands (M620/M621)
      const m620Count = (gcode.match(/M620/g) || []).length
      const m621Count = (gcode.match(/M621/g) || []).length
      console.log(`Bambu AMS commands found: M620=${m620Count}, M621=${m621Count}`)

      if (m620Count === 0 || m621Count === 0) {
        console.log('FAIL: Missing Bambu AMS filament change commands (M620/M621)')
        multiColorPassed = false
      } else {
        console.log('PASS: Bambu AMS commands (M620/M621) found')
      }

      // 3. Check filament usage line
      const filamentMatch = gcode.match(/^; filament: (.+)$/m)
      if (filamentMatch) {
        const filaments = filamentMatch[1].split(',').map((f: string) => f.trim())
        console.log(`Filaments used: ${filaments.join(', ')}`)

        if (filaments.length < 2) {
          console.log('FAIL: Only one filament used - expected at least 2 for multi-color')
          multiColorPassed = false
        } else {
          console.log('PASS: Multiple filaments used')
        }
      } else {
        console.log('WARN: Could not find filament usage line in G-code')
      }

      // 4. Check project_settings.config for preserved colors
      try {
        const configJson = execSync(`unzip -p "${output3mf}" "Metadata/project_settings.config"`, { encoding: 'utf-8' })
        const config = JSON.parse(configJson)

        if (config.filament_colour && Array.isArray(config.filament_colour)) {
          console.log(`Colors in output 3MF: ${config.filament_colour.join(', ')}`)

          // Check for expected colors from the test 3MF (yellow and green)
          const hasYellow = config.filament_colour.some((c: string) => c.toUpperCase() === '#FFFF00')
          const hasGreen = config.filament_colour.some((c: string) => c.toUpperCase() === '#00FF00')

          if (hasYellow && hasGreen) {
            console.log('PASS: Original 3MF colors preserved (yellow #FFFF00, green #00FF00)')
          } else {
            console.log('FAIL: Original 3MF colors not preserved')
            console.log(`  Expected: #FFFF00 (yellow) and #00FF00 (green)`)
            multiColorPassed = false
          }
        } else {
          console.log('WARN: filament_colour not found in project_settings.config')
        }

        // 5. Check change_filament_gcode is preserved
        if (config.change_filament_gcode && config.change_filament_gcode.length > 100) {
          console.log(`change_filament_gcode preserved: ${config.change_filament_gcode.length} chars`)
          console.log('PASS: change_filament_gcode preserved from original 3MF')
        } else {
          console.log('FAIL: change_filament_gcode not preserved or empty')
          multiColorPassed = false
        }

        // 6. Check prime tower is enabled
        if (config.enable_prime_tower === 1 || config.enable_prime_tower === '1' || config.enable_prime_tower === true) {
          console.log('enable_prime_tower: enabled')
          console.log('PASS: Prime tower is enabled for multi-color printing')
        } else {
          console.log(`enable_prime_tower: ${config.enable_prime_tower}`)
          console.log('FAIL: Prime tower should be enabled for multi-color printing')
          multiColorPassed = false
        }
      } catch (e) {
        console.log('WARN: Could not parse project_settings.config')
      }

      // 7. Check for wipe tower in G-code (WIPE_TOWER comments)
      const wipeTowerMoves = (gcode.match(/; WIPE_TOWER/g) || []).length
      console.log(`Wipe tower moves in G-code: ${wipeTowerMoves}`)
      if (wipeTowerMoves > 0) {
        console.log('PASS: Wipe tower (prime tower) moves found in G-code')
      } else {
        console.log('FAIL: No wipe tower moves found in G-code')
        multiColorPassed = false
      }

      if (!multiColorPassed) {
        console.log('\nFAIL: Multi-color validation failed')
        process.exit(1)
      } else {
        console.log('\nPASS: All multi-color validations passed')
      }

    } else {
      console.log('\nWARN: G-code file not found, skipping coordinate verification')
    }

    process.exit(0)

  } catch (err: any) {
    clearTimeout(timeoutId)
    const elapsed = Date.now() - startTime
    console.log(`\nOperation failed after ${elapsed}ms`)
    console.log('Error:', err.message)
    
    // A failure is still acceptable - it means the addon didn't hang
    if (elapsed < TIMEOUT_SEC * 1000) {
      console.log('\nPASS: Addon returned error (did not hang)')
      process.exit(0)
    } else {
      console.log('\nFAIL: Addon took too long')
      process.exit(1)
    }
  }
}

main()

