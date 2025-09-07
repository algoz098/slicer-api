#!/usr/bin/env node
const { spawnSync } = require('child_process')

function run(cmd, args, opts={}) {
  const r = spawnSync(cmd, args, { stdio: 'inherit', ...opts })
  if (r.status !== 0) process.exit(r.status)
}

// Ensure cmake-js is available (dev dependency is recommended)
try { require.resolve('cmake-js') } catch (e) {
  console.error('cmake-js not found. Install with: npm install --save-dev cmake-js node-addon-api')
  process.exit(1)
}

run(process.execPath, [require.resolve('cmake-js/bin/cmake-js'), 'compile'], { cwd: __dirname + '/../native/orcaslicer_core' })

