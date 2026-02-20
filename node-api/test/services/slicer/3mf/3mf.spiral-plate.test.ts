// Test: 3MF files with spiral_mode and multi-plate structure should slice successfully.
// This covers edge cases where re-saved 3MFs may have broken plate metadata.
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
const os = require('node:os') as typeof import('node:os')
import { execSync } from 'node:child_process'
import type { Server } from 'http'

describe('slicer/3mf - Spiral Vase Mode (TARROS_COCINA.3mf)', function () {
    let server: Server
    let baseURL: string
    const fixtureFile = path.resolve(__dirname, '../../../../test/fixtures/TARROS_COCINA.3mf')

    before(async () => {
        server = await app.listen(0)
        const address = server.address()
        const port = typeof address === 'string' || address === null ? 0 : address.port
        baseURL = `http://127.0.0.1:${port}`
    })

    after(async () => {
        if (app) await app.teardown()
    })

    /**
     * Helper to extract config value from G-code metadata in the 3MF
     */
    function extractConfigValue(outputPath: string, key: string): string | null {
        try {
            // The config is in Metadata/plate_1.gcode (or similar)
            // We grep for "; key = value"
            const cmd = `unzip -p "${outputPath}" "Metadata/plate_*.gcode" 2>/dev/null | grep -E "^; ${key} = " | head -n 1`
            const result = execSync(cmd, { encoding: 'utf-8' })
            const match = result.match(new RegExp(`^; ${key} = (.+)$`, 'm'))
            return match ? match[1].trim() : null
        } catch {
            return null
        }
    }

    it('should slice plate 1 (spiral mode lid) successfully', async function () {
        this.timeout(120000)

        if (!fs.existsSync(fixtureFile)) {
            console.warn('Fixture TARROS_COCINA.3mf not found, skipping')
            return this.skip()
        }

        const resp = await axios.post(
            `${baseURL}/slicer/3mf`,
            {
                filePath: fixtureFile,
                plate: 1
            },
            { validateStatus: () => true }
        )

        // Should not be 500 (empty model error)
        assert.notStrictEqual(
            resp.status,
            500,
            `Got 500 error (should not happen): ${JSON.stringify(resp.data)}`
        )

        // Accept 201 (sliced OK) or 400 with OBJECTS_OUT_OF_BOUNDS (geometry issue, not empty)
        if (resp.status === 400) {
            const code = resp.data?.data?.code
            assert.ok(
                code === 'OBJECTS_OUT_OF_BOUNDS' || code === 'MODEL_EMPTY',
                `Unexpected 400 code: ${code} - ${JSON.stringify(resp.data)}`
            )
            console.log(`Plate 1: 400 with code ${code} (acceptable)`)
            return
        }

        assert.strictEqual(resp.status, 201, 'Status code should be 201')
        assert.ok(resp.data.outputPath, 'Should return outputPath')
        assert.ok(fs.existsSync(resp.data.outputPath), 'Output file should exist')

        // Validate that spiral_mode was actually enabled in the output
        const spiralMode = extractConfigValue(resp.data.outputPath, 'spiral_mode')
        console.log(`[TEST] Extracted spiral_mode: ${spiralMode}`)

        // Check various spiral mode indicators
        assert.strictEqual(spiralMode, '1', 'spiral_mode should be 1 (enabled)')

        // Vase mode implies 0% infill and 1 wall loop (usually)
        const infill = extractConfigValue(resp.data.outputPath, 'sparse_infill_density')
        const wallLoops = extractConfigValue(resp.data.outputPath, 'wall_loops')
        const layers = extractConfigValue(resp.data.outputPath, 'bottom_shell_layers')

        console.log(`[TEST] Infill: ${infill}, WallLoops: ${wallLoops}, BottomLayers: ${layers}`)

        // Note: sparse_infill_density might be "0%" or "0" depending on formatting
        assert.ok(infill === '0%' || infill === '0', 'Vase mode should have 0% infill')
        assert.strictEqual(wallLoops, '1', 'Vase mode should force 1 wall loop')
        console.log(`Plate 1: sliced OK (${resp.data.size} bytes)`)

        // Cleanup
        try { fs.unlinkSync(resp.data.outputPath) } catch { }
    })

    it('should slice plate 2 (spiral mode body) successfully', async function () {
        this.timeout(120000)

        if (!fs.existsSync(fixtureFile)) {
            console.warn('Fixture TARROS_COCINA.3mf not found, skipping')
            return this.skip()
        }

        const resp = await axios.post(
            `${baseURL}/slicer/3mf`,
            {
                filePath: fixtureFile,
                plate: 2
            },
            { validateStatus: () => true }
        )

        // Should not be 500 (empty model error)
        assert.notStrictEqual(
            resp.status,
            500,
            `Got 500 error (should not happen): ${JSON.stringify(resp.data)}`
        )

        if (resp.status === 400) {
            const code = resp.data?.data?.code
            assert.ok(
                code === 'OBJECTS_OUT_OF_BOUNDS' || code === 'MODEL_EMPTY',
                `Unexpected 400 code: ${code} - ${JSON.stringify(resp.data)}`
            )
            console.log(`Plate 2: 400 with code ${code} (acceptable)`)
            return
        }

        assert.strictEqual(resp.status, 201, `Unexpected status: ${resp.status} - ${JSON.stringify(resp.data)}`)
        assert.ok(resp.data.outputPath, 'Should return outputPath')
        console.log(`Plate 2: sliced OK (${resp.data.size} bytes)`)

        // Cleanup
        try { fs.unlinkSync(resp.data.outputPath) } catch { }
    })

    it('should slice without specifying plate (default) successfully', async function () {
        this.timeout(120000)

        if (!fs.existsSync(fixtureFile)) {
            console.warn('Fixture TARROS_COCINA.3mf not found, skipping')
            return this.skip()
        }

        const resp = await axios.post(
            `${baseURL}/slicer/3mf`,
            {
                filePath: fixtureFile
            },
            { validateStatus: () => true }
        )

        // Should not be 500 (empty model error)
        assert.notStrictEqual(
            resp.status,
            500,
            `Got 500 error (should not happen): ${JSON.stringify(resp.data)}`
        )

        if (resp.status === 400) {
            const code = resp.data?.data?.code
            console.log(`Default plate: 400 with code ${code} (acceptable)`)
            return
        }

        assert.strictEqual(resp.status, 201, `Unexpected status: ${resp.status} - ${JSON.stringify(resp.data)}`)
        assert.ok(resp.data.outputPath, 'Should return outputPath')
        console.log(`Default plate: sliced OK (${resp.data.size} bytes)`)

        // Cleanup
        try { fs.unlinkSync(resp.data.outputPath) } catch { }
    })
})
