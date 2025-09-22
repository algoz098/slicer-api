// CLI overrides test: runs orcaslicer-cli slice with --set and validates CONFIG_BLOCK
// eslint-disable-next-line @typescript-eslint/no-var-requires
const assert = require('assert') as typeof import('assert')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const fs = require('node:fs') as typeof import('node:fs')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const path = require('node:path') as typeof import('node:path')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const os = require('node:os') as typeof import('node:os')
// eslint-disable-next-line @typescript-eslint/no-var-requires
const cp = require('node:child_process') as typeof import('node:child_process')

function findCliBinary() {
  // Prefer path relative to node-api working directory
  const fromCwd = path.resolve(process.cwd(), '../OrcaSlicerCli/build-ninja/bin/orcaslicer-cli')
  if (fs.existsSync(fromCwd)) return fromCwd
  // Fallback: compute relative to this test file
  const fromHere = path.resolve(__dirname, '../../..', 'OrcaSlicerCli/build-ninja/bin/orcaslicer-cli')
  if (fs.existsSync(fromHere)) return fromHere
  throw new Error(`CLI binario nao encontrado em ${fromCwd}`)
}

describe('CLI slice --set overrides', () => {
  it('aplica sparse_infill_density e layer_height', function () {
    this.timeout(180000)

    const cli = findCliBinary()
    console.log('CLI binario:', cli)

    const inputSTL = path.resolve(__dirname, '../../..', 'example_files/3DBenchy.stl')
    assert.ok(fs.existsSync(inputSTL), 'Arquivo de exemplo 3DBenchy.stl nao encontrado')

    const outGcode = path.join(os.tmpdir(), `cli_overrides_${Date.now()}.gcode`)

    const args = [
      'slice',
      '-q',
      '--input', inputSTL,
      '--output', outGcode,
      '--printer', 'Bambu Lab X1 Carbon 0.4 nozzle',
      '--filament', 'Bambu PLA Basic @BBL X1C',
      '--process', '0.20mm Standard @BBL X1C',
      '--set', 'sparse_infill_density=30,layer_height=0.24'
    ]

    const cwd = path.resolve(process.cwd(), '..')
    const { status, stdout, stderr, signal, error } = cp.spawnSync(cli, args, { encoding: 'utf8', cwd, maxBuffer: 64 * 1024 * 1024 })
    if (status !== 0) {
      console.error('CLI stdout:\n', stdout)
      console.error('CLI stderr:\n', stderr)
      if (error) console.error('spawnSync error:', error)
      throw new Error(`CLI retornou status ${status} signal=${signal ?? 'none'} (cwd=${cwd}).`)
    }

    assert.ok(fs.existsSync(outGcode), 'G-code nao foi gerado')
    const gcode = fs.readFileSync(outGcode, 'utf8')

    assert.ok(/sparse_infill_density\s*=\s*30%?\b/.test(gcode), 'sparse_infill_density override ausente no G-code')
    assert.ok(/layer_height\s*=\s*0\.24\b/.test(gcode), 'layer_height override ausente no G-code')
  })
})

