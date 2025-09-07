// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
import assert from 'assert'
import * as path from 'path'
import * as fs from 'fs'
import { app } from '../../../../src/app'

describe('files/gcode service', () => {
  it('registered the service', () => {
    const service = app.service('files/gcode')

    assert.ok(service, 'Registered the service')
  })

  // the create method can receive a file in any format supported by orcaslicer minus gcode ones
  it('converts 3mf to gcode', async () => {
    const service = app.service('files/gcode')
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')

    const fileBuffer = fs.readFileSync(testFilePath)
    const fileData = {}

    // Simulate Koa context with file upload
    const mockCtx = {
      request: {
        files: {
          file: {
            buffer: fileBuffer,
            originalFilename: 'test_3mf.3mf',
            name: 'test_3mf.3mf'
          }
        }
      }
    }

    const params = {
      request: {
        ctx: mockCtx
      }
    } as any

    const result = await service.create(fileData, params)
    // retesult should be a file
    assert.ok(result.file, 'Should return file')
    // the result file should be a .gcode
    assert.ok(result.file.originalFilename.endsWith('.gcode'), 'Should return gcode file')
    // the content of the file should follow orcaslicer gcode (may have header comments)
    const gcodeContent = result.file.buffer.toString('utf8')
    assert.ok(gcodeContent.includes('G90'), 'Should contain G90')
    assert.ok(gcodeContent.includes('G21'), 'Should contain G21')
    assert.ok(gcodeContent.includes('M82') || gcodeContent.includes('M83'), 'Should contain M82 or M83')

    // Salvar G-code gerado para comparação na pasta test_files na raiz do code
    const apath = path.join(__dirname, '../../../../../test_files/GENERATED.gcode')
    // save the gocdoe to apath
    fs.writeFileSync(apath, gcodeContent)

    // comparar linha a linha do resultado com o arquivo test_files/test_3mf_plate_1.gcode
    const referencePath = path.join(__dirname, '../../../../../test_files/test_3mf_plate_1.gcode')
    const referenceGcode = fs.readFileSync(referencePath, 'utf8')
    const referenceLines = referenceGcode.split('\n')
    const lines = gcodeContent.split('\n')
    // remove lines from both sides references date and time
    lines.splice(0, 3)
    referenceLines.splice(0, 3)
     
    const diff = lines.filter((line, index) => line !== referenceLines[index])
    assert.ok(diff.length === 0, 'Should have no differences')
    // list which lines are diferent
    diff.forEach((line, index) => {
      console.log('Diferent line:', index, 'Generated:', line, 'Reference:', referenceLines[index])
    })
    assert.ok(lines.length === referenceLines.length, 'Should have same number of lines')
    assert.ok(gcodeContent === referenceGcode, 'Should have same content')
  })


  // the create method can receive a file in any format supported by orcaslicer minus gcode ones
  it('converts 3mf plate 2 to gcode', async () => {
    const service = app.service('files/gcode')
    const testFilePath = path.join(__dirname, '../../../../../test_files/test_3mf.3mf')

    const fileBuffer = fs.readFileSync(testFilePath)
    const fileData = {}

    // Simulate Koa context with file upload
    const mockCtx = {
      request: {
        query: {
          plate: 2
        },
        files: {
          file: {
            buffer: fileBuffer,
            originalFilename: 'test_3mf.3mf',
            name: 'test_3mf.3mf'
          }
        }
      }
    }

    const params = {
      request: {
        ctx: mockCtx
      }
    } as any

    const result = await service.create(fileData, params)
    // retesult should be a file
    assert.ok(result.file, 'Should return file')
    // the result file should be a .gcode
    assert.ok(result.file.originalFilename.endsWith('.gcode'), 'Should return gcode file')
    // the content of the file should follow orcaslicer gcode (may have header comments)
    const gcodeContent = result.file.buffer.toString('utf8')

    // Salvar G-code gerado para comparação na pasta test_files na raiz do code
    const apath = path.join(__dirname, '../../../../../test_files/GENERATED_2.gcode')
    // save the gocdoe to apath
    fs.writeFileSync(apath, gcodeContent)

    const referencePath = path.join(__dirname, '../../../../../test_files/test_3mf_plate_2.gcode')
    const referenceGcode = fs.readFileSync(referencePath, 'utf8')
    const referenceLines = referenceGcode.split('\n')
    const lines = gcodeContent.split('\n')
    // remove lines from both sides references date and time
    lines.splice(0, 3)
    referenceLines.splice(0, 3)
     
    const diff = lines.filter((line, index) => line !== referenceLines[index])
    assert.ok(diff.length === 0, 'Should have no differences')
    // list which lines are diferent
    diff.forEach((line, index) => {
      console.log('Diferent line:', index, 'Generated:', line, 'Reference:', referenceLines[index])
    })
    assert.ok(lines.length === referenceLines.length, 'Should have same number of lines')
    assert.ok(gcodeContent === referenceGcode, 'Should have same content')
  })

})
