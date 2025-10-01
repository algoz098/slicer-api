const assert = require('assert');
const path = require('path');
const fs = require('fs');
const os = require('os');
const binary = path.join(__dirname, '../../..', 'build', 'bindings', 'node', 'orcaslicer_node.node');
const orca = require(binary);

function ensureTestSTL() {
  const envPath = process.env.ORCACLI_TEST_STL;
  if (envPath && fs.existsSync(envPath)) return envPath;
  const benchy = path.join(__dirname, '../../..', 'example_files', '3DBenchy.stl');
  if (fs.existsSync(benchy)) return benchy;
  // Generate a tiny valid ASCII STL (single triangle)
  const tmp = path.join(os.tmpdir(), 'orcaslicercli_unit_triangle.stl');
  const asciiStl = [
    'solid unit_tetra',
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
    'endsolid unit_tetra',
    ''
  ].join('\n');
  fs.writeFileSync(tmp, asciiStl, 'utf8');
  return tmp;
}

(async () => {
  // Explicit init for unit tests. Use empty resourcesPath to let engine resolve defaults.
  orca.initialize({ resourcesPath: '' });

  // version returns string
  const v = orca.version();
  assert.strictEqual(typeof v, 'string');
  assert.ok(v.length > 0);

  // getModelInfo returns required fields
  const stl = ensureTestSTL();
  const info = await orca.getModelInfo(stl);
  assert.strictEqual(typeof info.filename, 'string');
  assert.ok(info.objectCount >= 1);
  assert.ok(info.triangleCount >= 1);

  // slice requires input param
  let threw = false;
  try {
    await orca.slice({});
  } catch (e) {
    threw = true;
  }
  assert.ok(threw, 'slice without params.input should throw');

  console.log('unit tests passed');
  try { orca.shutdown && orca.shutdown(); } catch (_) {}
})().catch((e) => { console.error(e); try { orca.shutdown && orca.shutdown(); } catch (_) {} process.exit(1); });
