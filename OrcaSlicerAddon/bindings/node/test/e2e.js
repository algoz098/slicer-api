const assert = require('assert');
const path = require('path');
const fs = require('fs');
const os = require('os');

const binary = path.join(__dirname, '../../..', 'build', 'bindings', 'node', 'orcaslicer_node.node');
const orca = require(binary);

function ensureTestSTL() {
  const envPath = process.env.ORCACLI_TEST_STL;
  if (envPath && fs.existsSync(envPath)) return envPath;
  const defaultBenchy = path.join(__dirname, '../../..', 'example_files', '3DBenchy.stl');
  if (fs.existsSync(defaultBenchy)) return defaultBenchy;
  const tmp = path.join(os.tmpdir(), 'orcaslicercli_e2e_triangle.stl');
  const asciiStl = [
    'solid e2e_tetra',
    // base triangle (z=0)
    ' facet normal 0 0 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 1 0 0',
    '   vertex 0 1 0',
    '  endloop',
    ' endfacet',
    // side 1 (x axis)
    ' facet normal 1 0 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 1 0 0',
    '   vertex 0 0 1',
    '  endloop',
    ' endfacet',
    // side 2 (y axis)
    ' facet normal 0 1 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 0 1 0',
    '   vertex 0 0 1',
    '  endloop',
    ' endfacet',
    // side 3 (diagonal)
    ' facet normal 1 1 1',
    '  outer loop',
    '   vertex 1 0 0',
    '   vertex 0 1 0',
    '   vertex 0 0 1',
    '  endloop',
    ' endfacet',
    'endsolid e2e_tetra',
    ''
  ].join('\n');
  fs.writeFileSync(tmp, asciiStl, 'utf8');
  return tmp;
}

function maybeResourcesPath() {
  // Prefer explicit env override
  const envPath = process.env.ORCACLI_RESOURCES;
  if (envPath && fs.existsSync(envPath)) return envPath;
  // Try checked-out OrcaSlicer resources
  const local = path.join(__dirname, '../../../OrcaSlicer/resources');
  if (fs.existsSync(local)) return local;
  // Fall back to empty => engine will rely on its defaults
  return '';
}

(async () => {
  try {
    const resourcesPath = maybeResourcesPath();
    if (resourcesPath) {
      orca.initialize({ resourcesPath });
    } else {
      // Allow engine defaults if resources are not provided in this environment
      orca.initialize({ resourcesPath: '' });
    }

    const stl = ensureTestSTL();
    const info = await orca.getModelInfo(stl);
    assert.strictEqual(info.isValid, true);
    assert.ok(info.triangleCount >= 1);

    const outGcode = path.join(os.tmpdir(), `orcaslicercli_e2e_${Date.now()}.gcode`);
    const res = await orca.slice({ input: stl, output: outGcode, verbose: false, dryRun: false });
    assert.strictEqual(typeof res.output, 'string');
    assert.strictEqual(res.output, outGcode);
    assert.ok(fs.existsSync(outGcode), 'Expected G-code output file to exist');

    // Optional 3MF parity test if provided
    const threeMf = process.env.ORCACLI_TEST_3MF;
    if (threeMf && fs.existsSync(threeMf)) {
      const outGcode2 = path.join(os.tmpdir(), `orcaslicercli_e2e_3mf_${Date.now()}.gcode`);
      const res2 = await orca.slice({ input: threeMf, output: outGcode2, verbose: false, dryRun: false });
      assert.strictEqual(res2.output, outGcode2);
      assert.ok(fs.existsSync(outGcode2), 'Expected 3MF G-code output file to exist');
    } else {
      console.warn('E2E: ORCACLI_TEST_3MF not set or file not found; 3MF slice step skipped.');
    }

    console.log('e2e tests passed');
  } catch (e) {
    console.error('e2e tests failed:', e);
    process.exit(1);
  } finally {
    try { orca.shutdown && orca.shutdown(); } catch (_) {}
  }
})();

