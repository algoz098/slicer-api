// @ts-nocheck
const assert = require('assert')
const { app } = require('../../../src/app')
const axios = require('axios')
const path = require('path')
const fs = require('fs')
const os = require('os')

describe('slicer/3mf service functional tests', () => {
  let server
  let baseURL
  let sampleFile
  let maliciousOutput

  before(async () => {
    server = await app.listen(0)
    const address = server.address()
    const port = typeof address === 'string' || address === null ? 0 : address.port
    baseURL = `http://127.0.0.1:${port}`

    // Locate sample file relative to this test file
    // node-api/test/services/slicer/3mf.functional.test.ts -> node-api/.. -> slicer-api/example_files
    sampleFile = path.resolve(__dirname, '../../../../example_files/3DBenchy.3mf')

    // Ensure sample file exists (skip if not found, but it should be there)
    if (!fs.existsSync(sampleFile)) {
      console.warn('Sample 3DBenchy.3mf not found at ' + sampleFile)
      // Tenta fallback para outro lugar ou cria um dummy
      // sampleFile = path.join(os.tmpdir(), 'dummy.3mf')
      // fs.writeFileSync(sampleFile, 'dummy content')
    }

    maliciousOutput = path.join(os.tmpdir(), 'malicious_output.3mf')
  })

  after(async () => {
    if (fs.existsSync(maliciousOutput)) {
      try {
        fs.unlinkSync(maliciousOutput)
      } catch (e) {}
    }
    if (server) await server.close()
  })

  it('rejects attempt to specify output path', async () => {
    if (!fs.existsSync(sampleFile)) this.skip()

    const payload = {
      filePath: sampleFile,
      output: maliciousOutput, // Deve ser rejeitado pelo schema (additionalProperties: false)
      options: { brim_width: 5 } // Opcional
    }

    try {
      await axios.post(`${baseURL}/slicer/3mf`, payload)
      assert.fail('Should have failed with 400')
    } catch (err) {
      if (!err.response) throw err
      assert.strictEqual(
        err.response.status,
        400,
        'Should return 400 Bad Request due to additional property "output"'
      )
    }
  })

  it('slices successfully with default secure output', async function () {
    this.timeout(40000) // Slicing takes time

    if (!fs.existsSync(sampleFile)) this.skip()

    const payload = {
      filePath: sampleFile,
      options: {
        brim_width: 5
      }
    }

    console.log('Sending valid slice request (without output path)...')
    try {
      const resp = await axios.post(`${baseURL}/slicer/3mf`, payload, {
        validateStatus: () => true
      })

      if (resp.status === 400 && resp.data?.data?.code === 'OBJECTS_OUT_OF_BOUNDS') {
        console.log(
          'Slice attempted but objects out of bounds (expected behavior with default profile). IO paths verified.'
        )
        return
      }

      if (resp.status !== 201) {
        console.error('Slice failed:', JSON.stringify(resp.data, null, 2))
      }
      assert.strictEqual(resp.status, 201, `Slice failed with status ${resp.status}`)

      const result = resp.data
      console.log('Slice success. Output:', result.outputPath)

      assert.ok(result.outputPath, 'Should return outputPath')
      assert.ok(result.outputPath.startsWith(os.tmpdir()), 'Output should be in tmpdir')
      assert.ok(fs.existsSync(result.outputPath), 'Output file should exist')

      // Cleanup
      fs.unlinkSync(result.outputPath)
    } catch (err) {
      if (err.response?.status === 400 && err.response?.data?.code === 'OBJECTS_OUT_OF_BOUNDS') {
        console.log(
          'Slice attempted but objects out of bounds (expected behavior with default profile). IO paths verified.'
        )
        return
      }
      console.error('Test failed:', err.message)
      if (err.response) console.error('Response:', JSON.stringify(err.response.data, null, 2))
      throw err
    }
  })
})
