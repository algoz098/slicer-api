/**
 * End-to-end test for multi-color 3MF slicing via HTTP API with auto-realignment.
 *
 * This test validates the complete slicing pipeline:
 * 1. Fetch A1 mini profiles from the ProfilesService via HTTP
 * 2. Upload teste_a1mini.3mf (configured for A1 256x256mm) via HTTP POST to /slicer/3mf
 * 3. Use A1 mini printer profile (180x180mm bed) to force realignment
 * 4. Extract G-code from the output 3MF
 * 5. Validate G-code coordinates are within A1 mini bed limits (180x180mm)
 * 6. Validate multi-color printing (tool changes, AMS commands, colors preserved)
 */

// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { app } = require('../../../../src/app') as { app: any }
// eslint-disable-next-line @typescript-eslint/no-var-requires
const axios = require('axios') as typeof import('axios')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const { execSync } = require('node:child_process') as typeof import('node:child_process')

// A1 mini bed size (180x180mm) - the test uses this to force realignment
// The 3MF file is configured for A1 (256x256mm)
const BED_MAX_X = 180
const BED_MAX_Y = 180
const TOLERANCE = 2 // Allow 2mm tolerance for line width overlap

describe('slicer/3mf: End-to-end multi-color slicing with ProfilesService', function () {
  this.timeout(300_000) // 5 minutes timeout for slicing

  let server: any
  let baseURL: string

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`
  })

  after(async () => {
    await app.teardown()
  })

  it('should fetch A1 mini profiles and slice with realignment', async function () {
    // 1. Fetch profiles from ProfilesService for A1 mini
    console.log('  Fetching A1 mini profiles from ProfilesService...')

    // Get printer profile (A1 mini 0.4 nozzle)
    const printerResp = await axios.get(
      `${baseURL}/profiles/${encodeURIComponent('Bambu Lab A1 mini 0.4 nozzle')}`,
      { params: { type: 'machine' }, validateStatus: () => true }
    )
    assert.strictEqual(printerResp.status, 200, `Failed to fetch printer profile: ${printerResp.status}`)
    const printerProfile = printerResp.data
    console.log(`  Printer profile: ${printerProfile.name}`)
    assert.ok(printerProfile.config, 'Printer profile should have config')
    assert.ok(printerProfile.config.printable_area, 'Printer profile should have printable_area')

    // Get process profile (0.20mm Standard @BBL A1M)
    const processResp = await axios.get(
      `${baseURL}/profiles/${encodeURIComponent('0.20mm Standard @BBL A1M')}`,
      { params: { type: 'process' }, validateStatus: () => true }
    )
    assert.strictEqual(processResp.status, 200, `Failed to fetch process profile: ${processResp.status}`)
    const processProfile = processResp.data
    console.log(`  Process profile: ${processProfile.name}`)
    assert.ok(processProfile.config, 'Process profile should have config')

    // Get filament profile (Bambu PLA Basic @BBL A1M)
    const filamentResp = await axios.get(
      `${baseURL}/profiles/${encodeURIComponent('Bambu PLA Basic @BBL A1M')}`,
      { params: { type: 'filament' }, validateStatus: () => true }
    )
    assert.strictEqual(filamentResp.status, 200, `Failed to fetch filament profile: ${filamentResp.status}`)
    const filamentProfile = filamentResp.data
    console.log(`  Filament profile: ${filamentProfile.name}`)
    assert.ok(filamentProfile.config, 'Filament profile should have config')

    // 2. Setup input/output paths
    const input3mf = path.resolve(__dirname, '../../../fixtures/teste_a1mini.3mf')
    assert.ok(fs.existsSync(input3mf), `Test fixture not found: ${input3mf}`)

    const outDir = path.resolve(__dirname, '../../../../../output_files')
    fs.mkdirSync(outDir, { recursive: true })
    const outTarget = path.join(outDir, 'e2e_a1mini_realign_output.gcode.3mf')

    // 3. Build merged config from profiles
    // Merge printer, process, and filament configs
    const mergedConfig = {
      ...printerProfile.config,
      ...processProfile.config,
      ...filamentProfile.config
    }

    // 4. Build JSON request using merged config (JSON on-the-fly)
    const body = {
      filePath: input3mf,
      output: outTarget,
      plate: 1,
      // Pass merged config to override 3MF settings with A1 mini profiles
      config: mergedConfig
    }

    // 5. POST to slicer/3mf with JSON body
    console.log('  Slicing via HTTP API with A1 mini profiles (180x180mm bed)...')
    const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
      headers: { 'content-type': 'application/json' },
      validateStatus: () => true
    })

    assert.strictEqual(resp.status, 201, `Unexpected status: ${resp.status} - ${JSON.stringify(resp.data)}`)
    const data = resp.data
    assert.ok(data.outputPath && data.outputPath.endsWith('.gcode.3mf'), 'Output should be .gcode.3mf')
    assert.ok(fs.existsSync(data.outputPath), 'Output file should exist on disk')

    // 4. Extract G-code from 3MF
    const gcodeFile = data.outputPath.replace('.gcode.3mf', '-extracted.gcode')
    try {
      execSync(`unzip -p "${data.outputPath}" "Metadata/plate_1.gcode" > "${gcodeFile}"`)
    } catch {
      assert.fail('Failed to extract G-code from output 3MF')
    }
    assert.ok(fs.existsSync(gcodeFile), 'Extracted G-code file should exist')

    const gcode = fs.readFileSync(gcodeFile, 'utf-8')
    const lines = gcode.split('\n')

    // 5. Validate G-code coordinates for ALL moves (G0 and G1)
    // This validates that both the objects AND the printer gcodes use correct coordinates
    console.log('  Validating G-code coordinates (all G0/G1 moves)...')
    const violations: string[] = []
    let maxX = 0
    let maxY = 0
    let minX = Infinity
    let minY = Infinity
    let inPrintingSection = false

    // A1 mini physical limits (including purge/wipe area and maintenance moves)
    // The A1 mini has a purge area at X=-13.5 (vs A1 at X=-48.2)
    // and the bed ends at X=180 (vs A1 at X=256)
    // Maintenance moves (nozzle clog detection, timelapse) can go to X=187
    const PURGE_MIN_X = -15 // A1 mini purge is at X=-13.5
    const PRINTER_MAX_X = 190 // A1 mini bed is 180mm + margin for maintenance moves (X=187 for nozzle clog detect)
    const PRINTER_MIN_Y = -5 // Small margin for bed edge
    const PRINTER_MAX_Y = 190 // A1 mini is 180mm + margin for brush area

    for (let i = 0; i < lines.length; i++) {
      const line = lines[i].trim()

      if (line.includes('LAYER_CHANGE')) {
        inPrintingSection = true
      }

      // Validate ALL G0 and G1 moves after LAYER_CHANGE
      // This catches both printing moves AND travel/positioning moves
      if (inPrintingSection && /^G[01]\s/.test(line)) {
        const xMatch = line.match(/X([-\d.]+)/)
        const yMatch = line.match(/Y([-\d.]+)/)

        if (xMatch) {
          const x = parseFloat(xMatch[1])
          if (x > maxX) maxX = x
          if (x < minX) minX = x
          // Check against A1 mini physical limits
          if (x > PRINTER_MAX_X || x < PURGE_MIN_X) {
            violations.push(
              `Line ${i + 1}: X=${x} out of A1 mini bounds (${PURGE_MIN_X} to ${PRINTER_MAX_X})`
            )
          }
        }
        if (yMatch) {
          const y = parseFloat(yMatch[1])
          if (y > maxY) maxY = y
          if (y < minY) minY = y
          // Check against A1 mini physical limits
          if (y > PRINTER_MAX_Y || y < PRINTER_MIN_Y) {
            violations.push(
              `Line ${i + 1}: Y=${y} out of A1 mini bounds (${PRINTER_MIN_Y} to ${PRINTER_MAX_Y})`
            )
          }
        }
      }
    }

    console.log(
      `  Coordinate range: X=[${minX.toFixed(2)}, ${maxX.toFixed(2)}]mm, Y=[${minY.toFixed(2)}, ${maxY.toFixed(2)}]mm`
    )

    if (violations.length > 0) {
      console.log(`  VIOLATIONS FOUND (${violations.length} total):`)
      violations.slice(0, 10).forEach(v => console.log(`    ${v}`))
      if (violations.length > 10) console.log(`    ... and ${violations.length - 10} more`)
    }

    assert.strictEqual(
      violations.length,
      0,
      `G-code has ${violations.length} coordinate violations for A1 mini`
    )
    assert.ok(maxX > 0 && maxX <= PRINTER_MAX_X, `Max X should be within A1 mini limits: ${maxX}`)
    assert.ok(maxY > 0 && maxY <= PRINTER_MAX_Y, `Max Y should be within A1 mini limits: ${maxY}`)

    // 6. Validate multi-color support
    console.log('  Validating multi-color support...')
    const t0Count = (gcode.match(/^T0$/gm) || []).length
    const t1Count = (gcode.match(/^T1$/gm) || []).length
    assert.ok(t0Count > 0 && t1Count > 0, `Tool changes required: T0=${t0Count}, T1=${t1Count}`)

    const m620Count = (gcode.match(/^M620/gm) || []).length
    const m621Count = (gcode.match(/^M621/gm) || []).length
    assert.ok(m620Count > 0 && m621Count > 0, `AMS commands required: M620=${m620Count}, M621=${m621Count}`)

    // 7. Validate colors preserved (check header comment)
    const colorMatches = gcode.match(/;\s*filament_colour\s*=\s*([^\n]+)/i)
    if (colorMatches) {
      const colors = colorMatches[1]
      console.log(`  Colors in G-code: ${colors}`)
      // Colors may be in different format, just check they exist
      assert.ok(colors.length > 0, 'Colors should be present in G-code header')
    } else {
      // If no color header, that's ok - the tool changes prove multi-color works
      console.log('  No color header found (optional)')
    }

    console.log('  All validations passed!')
  })
})
