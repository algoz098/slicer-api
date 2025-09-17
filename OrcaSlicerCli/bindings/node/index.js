"use strict";

// Try to load the addon from prebuilt/artifact and common build locations.
const path = require("path");
const fs = require("fs");
const tryRequire = (p) => { try { return require(p); } catch (_) { return null; } };

// 0) Prefer prebuilt artifact bundled in npm: prebuilds/<platform>-<arch>/orcaslicer_node.node
const prebuilt = path.join(__dirname, "prebuilds", `${process.platform}-${process.arch}`, "orcaslicer_node.node");

// 1) cmake-js default output during local dev
const mod1 = path.join(__dirname, "build", "Release", "orcaslicer_node.node");
// 2) top-level CMake output copied next to addon by src/CMakeLists during mono-repo builds
const mod2 = path.join(__dirname, "../../build/bindings/node/orcaslicer_node.node");

const candidatePaths = [prebuilt, mod1, mod2];
for (const p of candidatePaths) {
  if (fs.existsSync(p)) {
    const m = tryRequire(p);
    if (m) { module.exports = m; return; }
  }
}
// Fall back to mod1 error for clearer message in dev
module.exports = require(mod1);
