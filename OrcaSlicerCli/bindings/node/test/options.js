const fs = require('fs');
const os = require('os');
const path = require('path');
const assert = require('assert');

// Require addon via index.js so it can resolve prebuilds inside Docker base image
const orca = require('..');

function ensureTestSTL() {
  const envPath = process.env.ORCACLI_TEST_STL;
  if (envPath && fs.existsSync(envPath)) return envPath;
  const benchy = path.join(__dirname, '../../..', 'example_files', '3DBenchy.stl');
  if (fs.existsSync(benchy)) return benchy;
  const tmp = path.join(os.tmpdir(), 'orcaslicer_options_triangle.stl');
  const asciiStl = [
    'solid unit_tetra',
    ' facet normal 0 0 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 1 0 0',
    '   vertex 0 1 0',
    '  endloop',
    ' endfacet',
    ' facet normal 1 0 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 1 0 0',
    '   vertex 0 0 1',
    '  endloop',
    ' endfacet',
    ' facet normal 0 1 1',
    '  outer loop',
    '   vertex 0 0 0',
    '   vertex 0 1 0',
    '   vertex 0 0 1',
    '  endloop',
    ' endfacet',
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
  try {
    const localResources = path.resolve(__dirname, '../../../../OrcaSlicer/resources');
    const resourcesPath = process.env.ORCACLI_RESOURCES || (fs.existsSync(localResources) ? localResources : '');
    orca.initialize({ resourcesPath, verbose: false });

    const input = ensureTestSTL();
    const out = path.join(os.tmpdir(), `orca_options_${Date.now()}.gcode`);

    const { output } = await orca.slice({
      input,
      output: out,
      printerProfile: 'Bambu Lab X1 Carbon 0.4 nozzle',
      filamentProfile: 'Bambu PLA Basic @BBL X1C',
      processProfile: '0.20mm Standard @BBL X1C',
      options: { sparse_infill_density: 30, layer_height: 0.24 }
    });

    const gcode = fs.readFileSync(output, 'utf8');
    assert.ok(/sparse_infill_density\s*=\s*30%?\b/.test(gcode), 'sparse_infill_density override missing in G-code');
    assert.ok(/layer_height\s*=\s*0\.24\b/.test(gcode), 'layer_height override missing in G-code');

    console.log('options override test passed');
    try { orca.shutdown && orca.shutdown(); } catch (_) {}
  } catch (e) {
    console.error('options override test failed:', e);
    try { orca.shutdown && orca.shutdown(); } catch (_) {}
    process.exit(1);
  }
})();

