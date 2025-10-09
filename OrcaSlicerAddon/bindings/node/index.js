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

// Check if prebuilt exists but for different platform
const prebuildsDir = path.join(__dirname, "prebuilds");
if (fs.existsSync(prebuildsDir)) {
  const available = fs.readdirSync(prebuildsDir).filter(d => {
    const stat = fs.statSync(path.join(prebuildsDir, d));
    return stat.isDirectory();
  });
  if (available.length > 0 && !available.includes(`${process.platform}-${process.arch}`)) {
    const msg = `Prebuilt binaries available for: ${available.join(", ")} but not for current platform: ${process.platform}-${process.arch}`;
    log(msg);
    throw new Error(msg);
  }
}

// Fall back to mod1 error for clearer message in dev
log("falling back to require(mod1)", mod1);
const addon = wrapAsMiddleware(require(mod1));

// Attach Klipper client and high-level API
try {
  addon.KlipperClient = require('./lib/klipper-client');
  addon.SliceAndSend = require('./lib/slice-and-send');
  log("Klipper integration loaded");
} catch (err) {
  log("Klipper integration not available:", err.message);
}

module.exports = addon;
