const path = require('path');
const fs = require('fs');
const binary = path.join(__dirname, '../../..', 'build', 'bindings', 'node', 'orcaslicer_node.node');
const orca = require(binary);

console.log('OrcaSlicer Node addon binary:', binary);

(async () => {
  try {
    // Skip explicit initialize for smoke to avoid environment-specific preset loading; rely on lazy init where needed.
    // const resourcesPath = path.join(__dirname, '../../../OrcaSlicer/resources');
    // orca.initialize({ resourcesPath });

    // Basic sanity check
    const v = orca.version();
    console.log('Version:', v);

    // Optional: try to read a local fixture if present
    const stl = path.join(__dirname, 'fixtures', 'cube.stl');
    if (fs.existsSync(stl)) {
      const info = await orca.getModelInfo(stl);
      console.log('Model info:', info);
    } else {
      console.warn('No STL fixture found; skipping getModelInfo smoke step.');
    }
  } catch (e) {
    console.error('Smoke test failed:', e);
    process.exitCode = 1;
  } finally {
    try { orca.shutdown && orca.shutdown(); } catch (_) {}
  }
})();

