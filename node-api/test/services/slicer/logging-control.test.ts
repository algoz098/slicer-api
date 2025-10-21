import * as path from 'node:path'

// Minimal sanity test to validate the runtime logging toggle is exposed via N-API
// and callable without throwing. We don't assert actual log volume here because
// native streams bypass Node's stdout, but the method presence + no-throw toggle
// is sufficient to validate the integration contract.

describe('slicer: logging control (native setLoggingSilenced)', function () {
  this.timeout(30_000)

  it('exposes setLoggingSilenced(silent:boolean) and toggles without throwing', async () => {
    const nodeApiRoot = path.resolve(__dirname, '../../../..')
    const repoRoot = path.resolve(nodeApiRoot, '..')

    // Prefer locally built engine from the monorepo for tests
    process.env.ORCACLI_PREFER_LOCAL = '1'
    process.env.ORCACLI_ENGINE_PATH = path.join(
      repoRoot,
      'OrcaSlicerAddon/build/bindings/node/liborcacli_engine.dylib'
    )

    // Load addon directly (same path logic used by services)
    const addonDir = process.env.ORCACLI_ADDON_DIR || path.resolve(nodeApiRoot, '../OrcaSlicerAddon/bindings/node')
    // eslint-disable-next-line @typescript-eslint/no-var-requires
    const orca = require(addonDir)

    if (!(orca && typeof orca.setLoggingSilenced === 'function')) {
      throw new Error('setLoggingSilenced was not found on addon object')
    }

    // Toggle on/off twice to ensure idempotency at runtime
    try { orca.setLoggingSilenced(true) } catch (e) { throw new Error('toggle on failed: ' + (e as Error).message) }
    try { orca.setLoggingSilenced(false) } catch (e) { throw new Error('toggle off failed: ' + (e as Error).message) }
    try { orca.setLoggingSilenced(true) } catch (e) { throw new Error('toggle on(2) failed: ' + (e as Error).message) }
    try { orca.setLoggingSilenced(false) } catch (e) { throw new Error('toggle off(2) failed: ' + (e as Error).message) }
  })
})
