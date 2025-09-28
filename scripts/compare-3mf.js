#!/usr/bin/env node
/**
 * Compare a raw G-code file with the G-code embedded inside a 3MF.
 *
 * Usage:
 *   node scripts/compare-3mf.js <original.gcode> <file.3mf> [--context N] [--entry <gcode path>]
 *
 * Notes:
 * - Line-based diff using Myers algorithm (fast, dependency-free diff logic)
 * - Reads the 3MF as a ZIP; finds the first .gcode entry (or the one passed via --entry)
 * - Prints unified diff (git style): ---/+++, @@ hunks, and +/-/ context lines
 *
 * Dependency required to read .3mf (ZIP):
 *   npm install yauzl
 */

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');
const readline = require('node:readline');
const os = require('node:os');
const { spawnSync } = require('node:child_process');
let yauzl;

function printUsageAndExit() {
  console.error('Uso: node scripts/compare-3mf.js <original.gcode> <arquivo.3mf> [--context N] [--entry <caminhoNoZip>] [--full]');
  process.exit(2);
}

// Parse args
const args = process.argv.slice(2);
if (args.length < 2) printUsageAndExit();

let context = 3;
let preferredEntry = null;
let segmentLines = 10000; // número de linhas por segmento antes de comparar (evita OOM)
let full = false; // modo de diff completo
const files = [];
for (let i = 0; i < args.length; i++) {
  const a = args[i];
  if (a === '--context' || a === '-c') {
    const n = parseInt(args[++i], 10);
    if (!Number.isFinite(n) || n < 0) {
      console.error('Valor inválido para --context (use inteiro >= 0)');
      process.exit(2);
    }
    context = n;
  } else if (a === '--segment' || a === '-s') {
    const n = parseInt(args[++i], 10);
    if (!Number.isFinite(n) || n <= 0) {
      console.error('Valor inválido para --segment (use inteiro > 0)');
      process.exit(2);
    }
    segmentLines = n;
  } else if (a === '--entry' || a === '-e') {
    preferredEntry = args[++i];
    if (!preferredEntry) printUsageAndExit();
  } else if (a === '--full') {
    full = true;
  } else {
    files.push(a);
  }
}
if (files.length !== 2) printUsageAndExit();

const [fileGcode, file3mf] = files;
if (!/\.3mf$/i.test(file3mf)) {
  console.error('O segundo argumento deve ser um arquivo .3mf');
  process.exit(2);
}

function splitLinesUtf8(data) {
  return String(data).replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
}

async function readGcodeFileLines(p) {
  try {
    const data = await fsp.readFile(p, 'utf8');
    return splitLinesUtf8(data);
  } catch (err) {
    console.error(`Erro ao ler G-code: ${p}`);
    console.error(err.message);
    process.exit(1);
  }
}

function ensureYauzl() {
  try {
    // Lazy require so the script can show a useful message if missing
    // Install with: npm install yauzl
    // eslint-disable-next-line import/no-extraneous-dependencies
    yauzl = require('yauzl');
    return yauzl;
  } catch (e) {
    console.error('Dependência ausente: "yauzl" é necessária para ler .3mf (ZIP).');
    console.error('Instale com: npm install yauzl');
    process.exit(1);
  }
}

function readGcodeFrom3mf(zipPath, preferred) {
  ensureYauzl();
  return new Promise((resolve, reject) => {
    yauzl.open(zipPath, { lazyEntries: true }, (err, zip) => {
      if (err) return reject(err);
      let resolved = false;
      let chosen = null;

      function openAndRead(entry) {
        zip.openReadStream(entry, (err2, stream) => {
          if (err2) return reject(err2);
          const chunks = [];
          stream.on('data', (c) => chunks.push(c));
          stream.on('error', reject);
          stream.on('end', () => {
            resolved = true;
            const buf = Buffer.concat(chunks);
            zip.close();
            resolve(splitLinesUtf8(buf));
          });
        });
      }

      zip.on('entry', (entry) => {
        const name = entry.fileName.replace(/\\/g, '/');
        const isGcode = name.toLowerCase().endsWith('.gcode');
        if (preferred && name === preferred) {
          chosen = entry; // exact match wins
          openAndRead(entry);
          return;
        }
        if (!preferred && isGcode && !chosen) {
          // Pick the first .gcode if user didn't specify
          chosen = entry;
          openAndRead(entry);
          return;
        }
        zip.readEntry();
      });

      zip.on('end', () => {
        if (!resolved) {
          if (preferred) {
            reject(new Error(`Entrada não encontrada no .3mf: ${preferred}`));
          } else {
            reject(new Error('Nenhum arquivo .gcode encontrado dentro do .3mf'));
          }
        }
      });

      zip.on('error', reject);
      zip.readEntry();
    });
  });
}

// Open a readable stream to the first (or specified) .gcode entry inside the 3MF (ZIP)
function openGcodeStreamFrom3mf(zipPath, preferred) {
  ensureYauzl();
  return new Promise((resolve, reject) => {
    yauzl.open(zipPath, { lazyEntries: true }, (err, zip) => {
      if (err) return reject(err);
      let resolved = false;

      function openAndPipe(entry) {
        zip.openReadStream(entry, (err2, stream) => {
          if (err2) return reject(err2);
          // Ensure UTF-8 decoding for readline
          stream.setEncoding('utf8');
          resolved = true;
          stream.on('end', () => zip.close());
          resolve(stream);
        });
      }

      zip.on('entry', (entry) => {
        const name = entry.fileName.replace(/\\/g, '/');
        const isGcode = name.toLowerCase().endsWith('.gcode');
        if (preferred && name === preferred) {
          openAndPipe(entry);
          return;
        }
        if (!preferred && isGcode) {
          openAndPipe(entry);
          return;
        }
        zip.readEntry();
      });

      zip.on('end', () => {
        if (!resolved) {
          if (preferred) {
            reject(new Error(`Entrada não encontrada no .3mf: ${preferred}`));
          } else {
            reject(new Error('Nenhum arquivo .gcode encontrado dentro do .3mf'));
          }
        }
      });

      zip.on('error', reject);
      zip.readEntry();
    });
  });
}

// Extract G-code stream from 3MF into a temporary file to allow external diff tools
async function extractGcodeToTempFile(zipPath, preferred, baseName) {
  const dir = await fsp.mkdtemp(path.join(os.tmpdir(), 'slicer-'));
  const file = path.join(dir, (baseName || path.basename(zipPath, '.3mf')) + '.gcode');
  const inStream = await openGcodeStreamFrom3mf(zipPath, preferred);
  await new Promise((resolve, reject) => {
    const out = fs.createWriteStream(file, { encoding: 'utf8' });
    inStream.pipe(out);
    out.on('finish', resolve);
    out.on('error', reject);
    inStream.on('error', reject);
  });
  return { dir, file };
}

// Myers diff (line-based)
function myers(a, b) {
  const N = a.length;
  const M = b.length;
  const max = N + M;
  const V = new Map();
  V.set(1, 0);
  const trace = [];

  for (let D = 0; D <= max; D++) {
    const snapshot = new Map(V);
    trace.push(snapshot);
    for (let k = -D; k <= D; k += 2) {
      let x;
      if (k === -D) {
        x = V.get(k + 1) ?? 0; // down (insert in a)
      } else if (k !== D && (V.get(k - 1) ?? 0) < (V.get(k + 1) ?? 0)) {
        x = V.get(k + 1) ?? 0; // down
      } else {
        x = (V.get(k - 1) ?? 0) + 1; // right (delete from a)
      }
      let y = x - k;
      while (x < N && y < M && a[x] === b[y]) { x++; y++; }
      V.set(k, x);
      if (x >= N && y >= M) {
        return backtrack(trace, a, b, N, M);
      }
    }
  }
  return [];
}

function backtrack(trace, a, b, N, M) {
  const edits = [];
  let x = N;
  let y = M;
  for (let D = trace.length - 1; D >= 0; D--) {
    const V = trace[D];
    const k = x - y;
    let prevK;
    let prevX;
    if (k === -D || (k !== D && (V.get(k - 1) ?? 0) < (V.get(k + 1) ?? 0))) {
      prevK = k + 1;
      prevX = V.get(prevK) ?? 0;
      const prevY = prevX - prevK;
      edits.push({ type: 'insert', aIndex: x, bIndex: y - 1 });
      x = prevX;
      y = prevY;
    } else {
      prevK = k - 1;
      prevX = (V.get(prevK) ?? 0) + 1;
      const prevY = prevX - prevK;
      edits.push({ type: 'delete', aIndex: x - 1, bIndex: y });
      x = prevX - 1;
      y = prevY;
    }
    while (D > 0 && x > 0 && y > 0 && a[x - 1] === b[y - 1]) {
      edits.push({ type: 'equal', aIndex: x - 1, bIndex: y - 1 });
      x--; y--;
    }
  }
  return edits.reverse();
}

// Streaming comparison: walk line-by-line and stop at first difference.
// Prints a single unified-diff hunk around the first mismatch.
async function compareSegmented({ fileGcode, file3mf, preferredEntry, context, segmentLines, relA, relB }) {
  const streamA = fs.createReadStream(fileGcode, { encoding: 'utf8' });
  const rlA = readline.createInterface({ input: streamA, crlfDelay: Infinity });
  const streamB = await openGcodeStreamFrom3mf(file3mf, preferredEntry);
  const rlB = readline.createInterface({ input: streamB, crlfDelay: Infinity });

  function makeNext(rl) {
    const q = [];
    let done = false;
    const waiters = [];
    rl.on('line', (line) => {
      if (waiters.length) waiters.shift()({ value: line, done: false });
      else q.push(line);
    });
    rl.on('close', () => {
      done = true;
      while (waiters.length) waiters.shift()({ value: null, done: true });
    });
    return function next() {
      if (q.length) return Promise.resolve({ value: q.shift(), done: false });
      if (done) return Promise.resolve({ value: null, done: true });
      return new Promise((res) => waiters.push(res));
    };
  }

  const nextA = makeNext(rlA);
  const nextB = makeNext(rlB);

  const pre = [];
  let iA = 0;
  let iB = 0;

  while (true) {
    const [ra, rb] = await Promise.all([nextA(), nextB()]);
    const la = ra.value; const da = ra.done;
    const lb = rb.value; const db = rb.done;

    // Both ended -> identical
    if (da && db) {
      console.log('Não há diferenças.');
      rlA.close(); rlB.close();
      return false;
    }

    // If both have a line and they are equal, advance and keep rolling context
    if (!da && !db && la === lb) {
      iA++; iB++;
      pre.push(la);
      if (pre.length > context) pre.shift();
      continue;
    }

    // Found a difference (or one ended earlier)
    const preLen = pre.length;
    const aStart = iA - preLen + 1; // 1-based
    const bStart = iB - preLen + 1;
    const aCount = preLen + (da ? 0 : 1);
    const bCount = preLen + (db ? 0 : 1);

    console.log(`--- ${relA}`);
    console.log(`+++ ${relB} (gcode dentro do .3mf)`);
    console.log(`@@ -${aStart},${aCount} +${bStart},${bCount} @@`);
    for (const ctxLine of pre) console.log(` ${ctxLine}`);
    if (!da) console.log(`-${la}`);
    if (!db) console.log(`+${lb}`);

    rlA.close(); rlB.close();
    return true;
  }
}


function buildHunks(edits, contextSize, aLines, bLines) {
  const hunks = [];
  let aStart = null, bStart = null;
  let aCount = 0, bCount = 0;
  let lines = [];

  function flush() {
    if (aStart === null) return;
    hunks.push({ aStart, aCount, bStart, bCount, lines });
    aStart = bStart = null;
    aCount = bCount = 0;
    lines = [];
  }

  let i = 0;
  while (i < edits.length) {
    let eqRunStart = i;
    while (i < edits.length && edits[i].type === 'equal') i++;

    if (i >= edits.length) break;

    const preContextStart = Math.max(eqRunStart - contextSize, 0);
    const preContextEnd = eqRunStart - 1;

    aStart = edits[preContextStart].aIndex + 1;
    bStart = edits[preContextStart].bIndex + 1;
    lines = [];
    aCount = 0; bCount = 0;

    for (let j = preContextStart; j <= preContextEnd; j++) {
      const idx = edits[j].aIndex;
      lines.push({ sign: ' ', text: aLines[idx] });
      aCount++; bCount++;
    }

    let postEqCount = 0;
    while (i < edits.length && postEqCount < contextSize) {
      const e = edits[i];
      if (e.type === 'delete') {
        lines.push({ sign: '-', text: aLines[e.aIndex] });
        aCount++;
        postEqCount = 0;
      } else if (e.type === 'insert') {
        lines.push({ sign: '+', text: bLines[e.bIndex] });
        bCount++;
        postEqCount = 0;
      } else {
        lines.push({ sign: ' ', text: aLines[e.aIndex] });
        aCount++; bCount++;
        postEqCount++;
      }
      i++;
    }

    flush();
    while (i < edits.length && edits[i].type === 'equal') i++;
  }

  return hunks;
}

async function compareFull({ fileGcode, file3mf, preferredEntry, context }) {
  // Extract B to a temp file and invoke system diff to avoid Node memory pressure
  const { dir, file: tempB } = await extractGcodeToTempFile(file3mf, preferredEntry, path.basename(file3mf, '.3mf'));
  try {
    // Prefer git diff --no-index for unified output and context control
    let result = spawnSync('git', ['diff', '--no-index', `-U${context}`, '--', fileGcode, tempB], { stdio: 'inherit' });
    if (result.error || result.status == null) {
      // Fallback to BSD/GNU diff -U
      result = spawnSync('diff', ['-U', String(context), fileGcode, tempB], { stdio: 'inherit' });
    }
    // result.status: 0 (no diff), 1 (diffs), >1 (errors). We'll just print output.
    return result && result.status === 1;
  } finally {
    // Best-effort cleanup
    try { await fsp.unlink(tempB); } catch {}
    try { await fsp.rmdir(dir); } catch {}
  }
}

async function main() {
  const relA = path.relative(process.cwd(), path.resolve(fileGcode)).replace(/\\\\/g, '/');
  const relB = path.relative(process.cwd(), path.resolve(file3mf)).replace(/\\\\/g, '/');
  try {
    if (full) {
      await compareFull({ fileGcode, file3mf, preferredEntry, context, relA, relB });
    } else {
      await compareSegmented({ fileGcode, file3mf, preferredEntry, context, segmentLines, relA, relB });
    }
  } catch (e) {
    console.error(`Erro ao comparar arquivos: ${fileGcode} vs ${file3mf}`);
    console.error(e?.message || String(e));
    process.exit(1);
  }
}

main().catch((err) => {
  console.error(err?.stack || err?.message || String(err));
  process.exit(1);
});

