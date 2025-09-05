// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
import assert from 'assert'
import * as fs from 'fs'
import * as path from 'path'
import { app } from '../../../../src/app'

describe('files/info service', () => {
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
    assert.strictEqual(result.nozzle, '0.4', 'Should have nozzle field with diameter')
    assert.ok(result.printer, 'Should have printer information')
    assert.strictEqual(typeof result.printer, 'string', 'Printer should be a string')

    assert.ok(result.technicalName, 'Should have technical name information')
    assert.strictEqual(typeof result.technicalName, 'string', 'Technical name should be a string')
    assert.ok(result.technicalName.includes('@BBL') || result.technicalName.includes('@'), 'Technical name should contain @ symbol')

    console.log('Full result:', JSON.stringify(result, null, 2))
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
})
