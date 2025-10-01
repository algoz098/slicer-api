"use strict";

// Try to load the addon from prebuilt/artifact and common build locations.
const path = require("path");
const fs = require("fs");
const tryRequire = (p) => { try { return require(p); } catch (_) { return null; } };
const log = (...a) => { try { console.error("DEBUG: [addon-js]", ...a); } catch (_) {} };

// Wrap native addon into a Koa-compatible no-op middleware function and attach API
function wrapAsMiddleware(native) {
  const mw = async function orcaMiddleware(ctx, next) { return await next(); };
  // Copy also non-enumerable properties so functions defined via N-API are preserved
  for (const k of Object.getOwnPropertyNames(native)) {
    const d = Object.getOwnPropertyDescriptor(native, k);
    try { Object.defineProperty(mw, k, d); } catch {}
  }
  log("exporting middleware wrapper with API names:", Object.getOwnPropertyNames(native));
  return mw;
}

log("__dirname=", __dirname, "node=", process.versions && process.versions.node);

// 0) Prefer prebuilt artifact bundled in npm: prebuilds/<platform>-<arch>/orcaslicer_node.node
const prebuilt = path.join(__dirname, "prebuilds", `${process.platform}-${process.arch}`, "orcaslicer_node.node");

// 1) cmake-js default output during local dev
const mod1 = path.join(__dirname, "build", "Release", "orcaslicer_node.node");
// 1b) cmake-js with custom LIBRARY_OUTPUT_DIRECTORY (our CMake places under build/bindings/node)
const mod1b = path.join(__dirname, "build", "bindings", "node", "orcaslicer_node.node");
// 2) top-level CMake output copied next to addon by src/CMakeLists during mono-repo builds
const mod2 = path.join(__dirname, "../../build/bindings/node/orcaslicer_node.node");

// Allow dev override to prefer local build ahead of prebuilt
const preferLocal = process.env.ORCACLI_PREFER_LOCAL === "1";
const candidatesLocalFirst = [mod1, mod1b, mod2, prebuilt];
const candidatesDefault = [prebuilt, mod1, mod1b, mod2];
const candidatePaths = preferLocal ? candidatesLocalFirst : candidatesDefault;

for (const p of candidatePaths) {
  if (fs.existsSync(p)) {
    log("trying", p);
    const m = tryRequire(p);
    if (m) { log("loaded", p); module.exports = wrapAsMiddleware(m); return; }
    log("failed to load", p);
  }
}
// Fall back to mod1 error for clearer message in dev
log("falling back to require(mod1)", mod1);
module.exports = wrapAsMiddleware(require(mod1));
