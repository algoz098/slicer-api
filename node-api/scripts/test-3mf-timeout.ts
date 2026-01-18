/**
 * Test script to verify the timeout fix for the arrange operation.
 * Uses the teste_a1mini.3mf file with Bambu Lab A1 Mini config on-the-fly.
 *
 * This test simulates the real flow: frontend -> backend -> slicer-api
 * where the file is sent with A1 Mini (180x180) configuration but the file
 * was created for A1 (256x256).
 */
import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import { app } from '../src/app'
import FormData from 'form-data'

const TEST_FILE = path.resolve(__dirname, '../test/fixtures/teste_a1mini.3mf')

// Bambu Lab A1 Mini configuration on-the-fly (180x180x180mm bed)
// The 3MF file was created for 256x256 bed (A1), so using A1 Mini (180x180)
// should cause objects at position 128,128 to be near the edge or trigger realign
const A1_MINI_CONFIG = {
  // Printer settings - A1 Mini has 180x180 bed
  printer_model: 'Bambu Lab A1 mini',
  printer_variant: '0.4',
  nozzle_diameter: ['0.4'],
  printable_area: ['0x0', '180x0', '180x180', '0x180'],
  printable_height: '180',
  printer_structure: 'i3',

  // Filament settings (PLA) - matching the 4 extruders in the 3MF config
  filament_type: ['PLA', 'PLA', 'PLA', 'PLA'],
  filament_colour: ['#FFFFFF', '#000000', '#FF0000', '#00FF00'],
  nozzle_temperature: ['220', '220', '220', '220'],
  nozzle_temperature_initial_layer: ['220', '220', '220', '220'],
  bed_temperature: ['55', '55', '55', '55'],
  bed_temperature_initial_layer: ['55', '55', '55', '55'],

  // Process settings (0.20mm Standard)
  layer_height: '0.2',
  initial_layer_height: '0.2',
  wall_loops: '2',
  top_shell_layers: '4',
  bottom_shell_layers: '3',
  sparse_infill_density: '15%',
  sparse_infill_pattern: 'grid',
  initial_layer_speed: '50',
  outer_wall_speed: '200',
  inner_wall_speed: '300',
  sparse_infill_speed: '270',
  travel_speed: '500'
}

async function main() {
  const server = await app.listen(0)
  const startTime = Date.now()
  const TIMEOUT_LIMIT = 120000 // 120 seconds max for this test

  try {
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    const baseURL = `http://127.0.0.1:${port}`

    console.log(`Server running at ${baseURL}`)
    console.log(`Test file: ${TEST_FILE}`)
    console.log('')
    console.log('Using on-the-fly config for Bambu Lab A1 Mini (180x180x180mm)')
    console.log('')

    if (!fs.existsSync(TEST_FILE)) {
      console.error('Test file not found:', TEST_FILE)
      process.exit(2)
    }

    console.log('Uploading 3MF file for slicing...')
    console.log('This test verifies the timeout fix for the arrange operation.')
    console.log('Expected: The operation should complete (success or fail) within 120 seconds.')
    console.log('')

    const form = new FormData()
    form.append('file', fs.createReadStream(TEST_FILE), {
      filename: 'teste_a1mini.3mf',
      contentType: 'application/vnd.ms-package.3dmanufacturing-3dmodel+xml'
    })

    // Use on-the-fly config instead of profile names
    form.append('config', JSON.stringify(A1_MINI_CONFIG))

    // Set timeout for axios to detect infinite loops
    const resp = await axios.post(`${baseURL}/slicer/3mf`, form, {
      headers: form.getHeaders(),
      maxBodyLength: Infinity,
      maxContentLength: Infinity,
      timeout: TIMEOUT_LIMIT,
      validateStatus: () => true
    })

    const elapsed = Date.now() - startTime
    console.log(`\nOperation completed in ${elapsed}ms`)

    if (resp.status === 201) {
      console.log('SUCCESS: Slicing completed successfully')
      console.log('Output path:', resp.data?.outputPath)
    } else if (resp.status === 422) {
      console.log('EXPECTED: Slicing failed with validation error (item too large for bed)')
      console.log('Error:', resp.data?.message || resp.data)
    } else {
      console.log(`Operation returned status ${resp.status}`)
      console.log('Response:', JSON.stringify(resp.data, null, 2).slice(0, 500))
    }

    if (elapsed > TIMEOUT_LIMIT) {
      console.error('FAIL: Operation took too long - timeout fix may not be working')
      process.exit(1)
    } else {
      console.log('\nPASS: Operation completed within acceptable time limit')
      process.exit(0)
    }

  } catch (err: any) {
    const elapsed = Date.now() - startTime
    if (err.code === 'ECONNABORTED' || err.code === 'ETIMEDOUT') {
      console.error(`\nFAIL: Request timed out after ${elapsed}ms`)
      console.error('The arrange operation is likely stuck in an infinite loop.')
      process.exit(1)
    }
    console.error('Error:', err.message)
    process.exit(1)
  } finally {
    await app.teardown()
  }
}

main()

