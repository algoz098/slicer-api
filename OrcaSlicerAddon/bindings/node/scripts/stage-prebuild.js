#!/usr/bin/env node
/*
  Stage a prebuilt addon for the current platform/arch into:
  prebuilds/<platform>-<arch>/orcaslicer_node.node
  And copy the engine runtime library (liborcacli_engine.*) next to it.
*/

const fs = require('fs');
const path = require('path');

function ensureDir(p) {
  fs.mkdirSync(p, { recursive: true });
}

function exists(p) {
  try { return fs.statSync(p).isFile(); } catch (_) { return false; }
}

function findFirst(paths) {
  for (const p of paths) {
    if (exists(p)) return p;
  }
  return null;
}

function main() {
  const here = __dirname; // .../bindings/node/scripts
  const pkgRoot = path.join(here, '..');

  const platform = process.platform; // 'darwin' | 'linux' | 'win32'
  const arch = process.arch; // 'x64' | 'arm64' | ...

  const outDir = path.join(pkgRoot, 'prebuilds', `${platform}-${arch}`);
  ensureDir(outDir);

  // Candidate addon outputs (built locally)
  const addonCandidates = [
    path.join(pkgRoot, 'build', 'Release', 'orcaslicer_node.node'),
    path.join(pkgRoot, 'build', 'bindings', 'node', 'orcaslicer_node.node'),
    path.join(pkgRoot, '../../build/bindings/node/orcaslicer_node.node'),
  ].map(p => path.resolve(p));

  const addonPath = findFirst(addonCandidates);
  if (!addonPath) {
    console.error('ERROR: could not find built addon (.node). Looked at:\n  ' + addonCandidates.join('\n  '));
    process.exit(1);
  }

  // Engine library candidates
  const engineNames = platform === 'darwin'
    ? ['liborcacli_engine.dylib']
    : platform === 'win32'
      ? ['orcacli_engine.dll']
      : ['liborcacli_engine.so'];

  const engineCandidates = [];
  for (const name of engineNames) {
    // Same dir as addon (top-level cmake output case)
    engineCandidates.push(path.join(path.dirname(addonPath), name));
    // Local bindings dir under this package build
    engineCandidates.push(path.join(pkgRoot, 'build', 'bindings', 'node', name));
    // Top-level cmake bindings dir (when building from repo root)
    engineCandidates.push(path.join(pkgRoot, '../../build/bindings/node/', name));
    // Fallback: local build dir sibling
    engineCandidates.push(path.join(pkgRoot, 'build', 'Release', name));
  }
  const enginePath = findFirst(engineCandidates.map(p => path.resolve(p)));
  if (!enginePath) {
    console.error('ERROR: could not find engine runtime library. Looked at:\n  ' + engineCandidates.join('\n  '));
    process.exit(1);
  }

  const destAddon = path.join(outDir, 'orcaslicer_node.node');
  const destEngine = path.join(outDir, path.basename(enginePath));

  fs.copyFileSync(addonPath, destAddon);
  fs.copyFileSync(enginePath, destEngine);

  console.log('Staged prebuild for', `${platform}-${arch}`);
  console.log('  addon :', path.relative(pkgRoot, destAddon));
  console.log('  engine:', path.relative(pkgRoot, destEngine));
}

main();

