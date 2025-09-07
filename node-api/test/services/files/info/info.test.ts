// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
import * as fs from 'fs'
import * as path from 'path'
import assert from 'assert'

import { app } from '../../../../src/app'

describe('files/info service', function () {
  this.timeout(10000)
  it('registered the service', () => {
    const service = app.service('files/info')

    assert.ok(service, 'Registered the service')
  })

  it('extracts profile from uploaded .3mf file', async () => {
    const service = app.service('files/info')

    // Read test .3mf file
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')
    const fileBuffer = fs.readFileSync(testFilePath)

    // Simulate Koa context with file upload
    const mockCtx = {
      request: {
        files: {
          file: {
            buffer: fileBuffer,
            originalFilename: 'test_3mf.3mf',
            size: fileBuffer.length,
            mimetype: 'application/octet-stream'
          }
        }
      }
    }

    const result = await service.create({}, { request: { ctx: mockCtx } } as any)

    assert.ok(result, 'Should return a result')
    assert.ok(['0.2','0.4','0.6','0.8','1.0'].includes(result.nozzle as string), 'Should have valid nozzle diameter string')
    assert.ok(result.printer, 'Should have printer information')
    assert.strictEqual(typeof result.printer, 'string', 'Printer should be a string')

    assert.ok(result.technicalName, 'Should have technical name information')
    assert.strictEqual(typeof result.technicalName, 'string', 'Technical name should be a string')
    assert.ok(result.technicalName.includes('@BBL') || result.technicalName.includes('@'), 'Technical name should contain @ symbol')

    // Check plate count
    if (result.plateCount !== undefined) {
      assert.strictEqual(typeof result.plateCount, 'number', 'Plate count should be a number')
      assert.ok(result.plateCount >= 0, 'Plate count should be non-negative')
    }
  })

  it('extracts profile from uploaded .3mf file with custom parameters', async function () { this.timeout(10000)
    const service = app.service('files/info')

    // Read test .3mf file
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf_25plates_modified.3mf')
    const fileBuffer = fs.readFileSync(testFilePath)

    // Simulate Koa context with file upload
    const mockCtx = {
      request: {
        files: {
          file: {
            buffer: fileBuffer,
            originalFilename: 'test_3mf_25plates_modified.3mf',
            size: fileBuffer.length,
            mimetype: 'application/octet-stream'
          }
        }
      }
    }

    const result = await service.create({}, { request: { ctx: mockCtx } } as any)

    assert.ok(result, 'Should return a result')
    assert.ok(['0.2','0.4','0.6','0.8','1.0'].includes(result.nozzle as string), 'Should have valid nozzle diameter string')
    assert.ok(result.printer, 'Should have printer information')
    assert.strictEqual(typeof result.printer, 'string', 'Printer should be a string')

    assert.ok(result.technicalName, 'Should have technical name information')
    assert.strictEqual(typeof result.technicalName, 'string', 'Technical name should be a string')
    assert.ok(result.technicalName.includes('@BBL') || result.technicalName.includes('@'), 'Technical name should contain @ symbol')

    // Check for print settings extraction
    if (result.printSettings) {
      assert.strictEqual(typeof result.printSettings, 'object', 'Print settings should be an object')

      // Check for sparse infill percentage if present
      if (result.printSettings.sparseInfillPercentage !== undefined) {
        assert.strictEqual(typeof result.printSettings.sparseInfillPercentage, 'number', 'Sparse infill percentage should be a number')
        assert.ok(result.printSettings.sparseInfillPercentage >= 0 && result.printSettings.sparseInfillPercentage <= 100, 'Sparse infill percentage should be between 0-100')
      }

      // Check for layer height if present
      if (result.printSettings.layerHeight !== undefined) {
        assert.strictEqual(typeof result.printSettings.layerHeight, 'number', 'Layer height should be a number')
        assert.ok(result.printSettings.layerHeight > 0, 'Layer height should be positive')
      }

    }

    // Check plate count
    if (result.plateCount !== undefined) {
      assert.strictEqual(typeof result.plateCount, 'number', 'Plate count should be a number')
      assert.ok(result.plateCount >= 0, 'Plate count should be non-negative')
    }

    // comfirm the differences has sparce infill percentage and its value is 5
    assert.ok(result.differences, 'Should have differences')
    assert.ok(result.differences.length > 0, 'Should have differences')
    assert.ok(result.differences.find((d: any) => d.parameter === 'sparse_infill_percentage'), 'Should have sparse infill percentage difference')
    assert.strictEqual((result.differences.find((d: any) => d.parameter === 'sparse_infill_percentage') as any).fileValue, 5, 'Sparse infill percentage should be 5')

    assert.ok(result.filamentProfile, 'Should have filament profile values')
    // confirm the name of the filamentprofile is SUNLU PLA+
    assert.strictEqual(result.filamentProfile.name, 'SUNLU PLA+ 2.0 @base', 'Filament profile name should be correct')
    // the filamentprofile.diferences should have valeus and have sparce_infill_percentage with value 15 with 4 as value
    assert.ok(result.filamentProfile.differences, 'Should have filament profile differences')
    assert.ok(result.filamentProfile.differences.length > 0, 'Should have filament profile differences')
    assert.ok(result.filamentProfile.differences.find((d: any) => d.parameter === 'sparse_infill_percentage'), 'Should have sparse infill percentage difference')
    assert.strictEqual((result.filamentProfile.differences.find((d: any) => d.parameter === 'sparse_infill_percentage') as any).fileValue, 4, 'Sparse infill percentage should be 4')
    

    // eslint-disable-next-line no-console
    console.log('0:', JSON.stringify(result, null, 2))
    // console.log('0:', Object.keys((result as any).differences))
    // console.log('1:', JSON.stringify(Object.keys((result as any).printerProfileValues), null, 2))
    // console.log('differences:', JSON.stringify((result as any).differences, null, 2))
    // console.log('printerProfileValues:', JSON.stringify(result.printerProfileValues, null, 2))
    // console.log('printerProfileValues:', JSON.stringify(result.differences, null, 2))
    // console.log('printerProfileValues:', (result as any).differences[0].fileValue)
    // console.log('printerProfileValues:', (result as any).printerProfileValues.sparse_infill_density)
  })

  it('rejects requests without file', async () => {
    const service = app.service('files/info')

    try {
      await service.create({}, {} as any)
      assert.fail('Should have thrown BadRequest')
    } catch (error: any) {
      assert.strictEqual(error.name, 'BadRequest', 'Should throw BadRequest')
      assert.strictEqual(error.message, 'No file provided', 'Should have correct message')
    }
  })

  it('rejects unsupported file formats', async () => {
    const service = app.service('files/info')

    // Simulate Koa context with unsupported file
    const testBuffer = Buffer.from('dummy content')
    const mockCtx = {
      request: {
        files: {
          file: {
            buffer: testBuffer,
            originalFilename: 'test.txt',
            size: testBuffer.length,
            mimetype: 'text/plain'
          }
        }
      }
    }

    try {
      await service.create({}, { request: { ctx: mockCtx } } as any)
      assert.fail('Should have thrown BadRequest')
    } catch (error: any) {
      assert.strictEqual(error.name, 'BadRequest', 'Should throw BadRequest')
      assert(error.message.includes('Unsupported file format'), 'Should mention unsupported format')
    }
  })

  it('compares file parameters with printer profile', async () => {
    // For now, just test that the comparison method exists and can be called
    // This is a placeholder test until we fix the upload context issue
    const service = app.service('files/info')
    assert.ok(service)
    assert.ok(typeof (service as any).compareWithProfile === 'function')
  })

  it('normalizes printer profile values correctly', async function () {
    this.timeout(10000)
    const service = app.service('files/info')

    // Read test .3mf file
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')
    const fileBuffer = fs.readFileSync(testFilePath)

    // Simulate Koa context with file upload
    const mockCtx = {
      request: {
        files: {
          file: {
            buffer: fileBuffer,
            originalFilename: 'test_3mf.3mf',
            size: fileBuffer.length,
            mimetype: 'application/octet-stream'
          }
        }
      }
    }

    const result = await service.create({}, {
      request: { ctx: mockCtx }
    } as any)

    assert.ok(result, 'Should return a result')

    // Check if differences object exists and is plain object
    if ((result as any).differences) {
      assert.strictEqual(typeof (result as any).differences, 'object', 'differences should be an object')
    }
  })
})
