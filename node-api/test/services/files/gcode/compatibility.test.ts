import assert from 'assert'
import * as fs from 'fs'
import * as path from 'path'
import { SlicerCoreFacade } from '../../../../src/services/files/gcode/core/slicer-core'
import { CoreInputBuilder } from '../../../../src/services/files/gcode/core/core-input-builder'
import { app } from '../../../../src/app'

describe('G-code Compatibility with OrcaSlicer', () => {
  it('generates G-code with OrcaSlicer-compatible structure', async () => {
    const slicer = new SlicerCoreFacade()
    const builder = new CoreInputBuilder(app)

    // Load the same 3MF file used to generate the reference
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')
    const fileBuffer = fs.readFileSync(testFilePath)
    
    const mockFile = {
      buffer: fileBuffer,
      originalFilename: 'test_3mf.3mf'
    }

    const coreInput = await builder.fromUploadedFile(mockFile)
    const generatedGcode = await slicer.sliceToGcode(coreInput)

    // Load reference G-code
    const referencePath = path.join(__dirname, '../../../../../test_files/test_3mf_plate_1.gcode')
    const referenceGcode = fs.readFileSync(referencePath, 'utf8')

    // Save generated G-code for manual inspection
    const outputPath = path.join(__dirname, 'COMPATIBILITY_TEST.gcode')
    fs.writeFileSync(outputPath, generatedGcode)

    console.log('Generated G-code saved to:', outputPath)
    console.log('Reference G-code length:', referenceGcode.length, 'lines')
    console.log('Generated G-code length:', generatedGcode.split('\n').length, 'lines')

    // Structural compatibility checks
    const generatedLines = generatedGcode.split('\n')
    const referenceLines = referenceGcode.split('\n')

    // Check for essential G-code commands that should be present
    const essentialCommands = [
      'G90', // Absolute positioning
      'G21', // Millimeters
      'M82', // Absolute extrusion (or M83 for relative)
      'G92 E0', // Reset extruder
      'M104', // Set nozzle temp (or M109)
      'M140', // Set bed temp (or M190)
      'M204', // Set acceleration
      'G1', // Linear move
      'M84' // Disable motors (end sequence)
    ]

    for (const cmd of essentialCommands) {
      const hasCommand = generatedGcode.includes(cmd)
      assert.ok(hasCommand, `Generated G-code should contain ${cmd}`)
    }

    // Check for OrcaSlicer-style features
    const orcaFeatures = [
      'HEADER_BLOCK_START',
      'CONFIG_BLOCK_START',
      'CHANGE_LAYER',
      'FEATURE:',
      'layer num/total_layer_count'
    ]

    for (const feature of orcaFeatures) {
      const hasFeature = generatedGcode.includes(feature)
      assert.ok(hasFeature, `Generated G-code should contain OrcaSlicer feature: ${feature}`)
    }

    // Check that generated G-code can be parsed (basic syntax validation)
    let validGcodeLines = 0
    let commentLines = 0
    
    for (const line of generatedLines) {
      const trimmed = line.trim()
      if (trimmed.length === 0) continue
      
      if (trimmed.startsWith(';')) {
        commentLines++
      } else if (/^[GM]\d+/.test(trimmed)) {
        validGcodeLines++
      }
    }

    assert.ok(validGcodeLines > 0, 'Should contain valid G-code commands')
    assert.ok(commentLines > 0, 'Should contain comments for readability')

    console.log(`Validation passed: ${validGcodeLines} G-code commands, ${commentLines} comments`)
  })

  it('generates G-code that can be loaded in OrcaSlicer preview', async () => {
    // This test validates that our G-code has the minimum structure
    // needed for OrcaSlicer to recognize and preview it
    
    const slicer = new SlicerCoreFacade()
    const builder = new CoreInputBuilder(app)

    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')
    const fileBuffer = fs.readFileSync(testFilePath)
    
    const mockFile = {
      buffer: fileBuffer,
      originalFilename: 'test_3mf.3mf'
    }

    const coreInput = await builder.fromUploadedFile(mockFile)
    const gcode = await slicer.sliceToGcode(coreInput)

    // Check for minimum required structure for OrcaSlicer preview
    assert.ok(gcode.includes('G90'), 'Must have absolute positioning')
    assert.ok(gcode.includes('G21'), 'Must specify millimeters')
    assert.ok(gcode.includes('M82') || gcode.includes('M83'), 'Must specify extrusion mode')
    assert.ok(gcode.includes('G1') && gcode.includes('E'), 'Must have extrusion moves')
    assert.ok(gcode.includes('Z'), 'Must have Z movements')
    assert.ok(gcode.includes('F'), 'Must have feedrate specifications')

    // Check for layer information (required for preview)
    assert.ok(gcode.includes('LAYER') || gcode.includes('layer'), 'Must have layer information')
    assert.ok(gcode.includes('Z_HEIGHT') || gcode.includes('Z'), 'Must have Z height information')

    // Check for proper ending sequence
    assert.ok(gcode.includes('M104 S0') || gcode.includes('M109 S0'), 'Must turn off nozzle')
    assert.ok(gcode.includes('M140 S0') || gcode.includes('M190 S0'), 'Must turn off bed')

    console.log('G-code structure validation for OrcaSlicer preview: PASSED')
  })
})
