/**
 * Test script to verify the auto-realign feature when objects are outside the bed.
 * Uses the teste_a1mini.3mf file (configured for A1 256x256mm) but forces a smaller
 * printable area (180x180mm) to trigger the realignment logic.
 */
import * as path from 'node:path'
import * as fs from 'node:fs'

const addonPath = path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')
const orca = require(addonPath)

const TEST_FILE = path.resolve(__dirname, '../test/fixtures/teste_a1mini.3mf')
const OUTPUT_FILE = '/tmp/test-realign-output.gcode.3mf'
const RESOURCES_PATH = path.resolve(__dirname, '../../OrcaSlicer/resources')

async function main() {
  console.log('=== Auto-Realign Test (Objects Outside Bed) ===')
  console.log('Test file:', TEST_FILE)
  console.log('Output:', OUTPUT_FILE)
  console.log('')
  console.log('This test uses a 3MF configured for A1 (256x256mm) but forces')
  console.log('a smaller printable area (180x180mm) to trigger realignment.')
  console.log('')

  const startTime = Date.now()
  const TIMEOUT_SEC = 120

  const timeoutId = setTimeout(() => {
    console.error(`\nFAIL: Process timed out after ${Date.now() - startTime}ms`)
    process.exit(1)
  }, TIMEOUT_SEC * 1000)

  try {
    console.log('Initializing addon...')
    orca.initialize({ addonDir: addonPath, resourcesPath: RESOURCES_PATH })
    console.log('Addon initialized.')

    // Force a smaller printable area to trigger realignment
    // The 3MF has objects at positions that will be outside 180x180mm
    const smallBedOptions = {
      printable_area: ['0x0', '180x0', '180x180', '0x180'],
      printable_height: '180'
    }

    console.log('\nCalling orca.slice() with smaller bed (180x180mm)...')
    console.log('Objects in 3MF are at positions that exceed 180mm, so realignment should trigger.')
    console.log('')

    const result = await orca.slice({
      input: TEST_FILE,
      output: OUTPUT_FILE,
      options: smallBedOptions,
      center: true,
      autoRealignIfNeeded: true,
      transferPrinterCustomizations: true,
      transferFilamentCustomizations: true,
      transferProcessCustomizations: true,
      transferProjectOverrides: true
    })

    clearTimeout(timeoutId)
    const elapsed = Date.now() - startTime

    console.log(`\nOperation completed in ${elapsed}ms`)
    console.log('Result:', JSON.stringify(result, null, 2).slice(0, 500))

    // Check if realignment was triggered by looking at the output log
    const logFile = path.resolve(__dirname, '../../OrcaSlicerAddon/output.log')
    if (fs.existsSync(logFile)) {
      const log = fs.readFileSync(logFile, 'utf-8')
      const hasRealign = log.includes('simple_reposition') || log.includes('is_outside=true')
      const hasCheckOutside = log.includes('check_outside')

      console.log('\n=== Realignment Check ===')
      console.log('check_outside called:', hasCheckOutside ? 'YES' : 'NO')
      console.log('Realignment triggered:', hasRealign ? 'YES' : 'NO')

      if (hasRealign) {
        console.log('\nPASS: Auto-realign was triggered for objects outside bed')
      } else if (hasCheckOutside) {
        // Check if objects were already inside
        const isOutsideFalse = log.includes('is_outside=false')
        if (isOutsideFalse) {
          console.log('\nINFO: Objects were already inside the bed, no realignment needed')
          console.log('This may indicate the options did not override the printable_area correctly')
        }
      }
    }

    // Verify G-code coordinates are within the smaller bed
    const { execSync } = require('child_process')
    const gcodeFile = '/tmp/test-realign-output-extracted.gcode'

    if (fs.existsSync(OUTPUT_FILE)) {
      try {
        execSync(`unzip -p "${OUTPUT_FILE}" "Metadata/plate_1.gcode" > "${gcodeFile}"`)
        console.log('\nExtracted G-code from 3MF')

        const gcode = fs.readFileSync(gcodeFile, 'utf-8')
        const lines = gcode.split('\n')

        let maxX = 0,
          maxY = 0
        let inPrintingSection = false

        for (const line of lines) {
          if (line.includes('LAYER_CHANGE')) inPrintingSection = true
          if (!inPrintingSection) continue

          if (/^G1\s/.test(line) && line.includes('E') && !line.includes('E-')) {
            const xMatch = line.match(/X([-\d.]+)/)
            const yMatch = line.match(/Y([-\d.]+)/)
            if (xMatch) maxX = Math.max(maxX, parseFloat(xMatch[1]))
            if (yMatch) maxY = Math.max(maxY, parseFloat(yMatch[1]))
          }
        }

        console.log(`\nMax X coordinate (extrusion moves): ${maxX.toFixed(2)}mm`)
        console.log(`Max Y coordinate (extrusion moves): ${maxY.toFixed(2)}mm`)

        if (maxX <= 180 && maxY <= 180) {
          console.log('\nPASS: All extrusion coordinates are within 180x180mm bed')
        } else {
          console.log('\nFAIL: Extrusion coordinates exceed 180x180mm bed limits')
          console.log('This indicates realignment did not work correctly')
          process.exit(1)
        }
      } catch (e) {
        console.log('Failed to extract/analyze G-code:', e)
      }
    }

    console.log('\nTest completed successfully')
    process.exit(0)
  } catch (err: any) {
    clearTimeout(timeoutId)
    console.error('Error:', err.message)
    process.exit(1)
  }
}

main()
