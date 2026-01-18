import * as path from 'node:path'

// Ensure the native engine library is discoverable before any test imports src/app.ts
if (!process.env.ORCACLI_PREFER_LOCAL) process.env.ORCACLI_PREFER_LOCAL = '1'

// Resolve paths relative to the node-api folder (stable even if cwd shifts)
const nodeApiRoot = path.resolve(__dirname, '..')
const repoRoot = path.resolve(nodeApiRoot, '..')

if (!process.env.ORCACLI_ENGINE_PATH) {
  const engine = path.resolve(repoRoot, 'OrcaSlicerAddon/build/bindings/node/liborcacli_engine.dylib')
  process.env.ORCACLI_ENGINE_PATH = engine
}

if (!process.env.ORCACLI_RESOURCES) {
  const resources = path.resolve(repoRoot, 'OrcaSlicer/resources')
  process.env.ORCACLI_RESOURCES = resources
}

// Optional: allow overriding addon dir if needed in CI or different layout
if (!process.env.ORCACLI_ADDON_DIR) {
  process.env.ORCACLI_ADDON_DIR = path.resolve(repoRoot, 'OrcaSlicerAddon/bindings/node')
}

// Small debug to confirm early setup
// eslint-disable-next-line no-console
console.log('[test-setup] ORCACLI_ENGINE_PATH=', process.env.ORCACLI_ENGINE_PATH)
