#!/usr/bin/env node
/**
 * Simple, dependency-free text diff (unified format) for this repository.
 *
 * Usage:
 *   node scripts/compare-text.js <fileA> <fileB> [--context N]
 *
 * Notes:
 * - Line-based diff using Myers algorithm (efficient for large files)
 * - Outputs a unified diff with @@ hunks and +/-/ context lines
 */

const fs = require('fs');
const path = require('path');

function printUsageAndExit() {
  console.error('Uso: node scripts/compare-text.js <arquivoA> <arquivoB> [--context N]');
  process.exit(2);
}

// Parse args
const args = process.argv.slice(2);
if (args.length < 2) printUsageAndExit();

let context = 3;
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
  } else {
    files.push(a);
  }
}
if (files.length !== 2) printUsageAndExit();

const [fileA, fileB] = files;

function readLines(p) {
  try {
    const data = fs.readFileSync(p, 'utf8');
    // Normalize line endings and split
    return data.replace(/\r\n/g, '\n').replace(/\r/g, '\n').split('\n');
  } catch (err) {
    console.error(`Erro ao ler arquivo: ${p}`);
    console.error(err.message);
    process.exit(1);
  }
}

const aLines = readLines(fileA);
const bLines = readLines(fileB);

// Quick identical check to avoid false positives and speed up
if (aLines.length === bLines.length) {
  let allEqual = true;
  for (let i = 0; i < aLines.length; i++) {
    if (aLines[i] !== bLines[i]) { allEqual = false; break; }
  }
  if (allEqual) {
    console.log('Não há diferenças.');
    process.exit(0);
  }
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
    // Store snapshot of V for backtracking
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
      // Follow snake
      while (x < N && y < M && a[x] === b[y]) {
        x++;
        y++;
      }
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
      // move down: insertion in a (line added in b)
      const prevY = prevX - prevK;
      edits.push({ type: 'insert', aIndex: x, bIndex: y - 1 });
      x = prevX;
      y = prevY;
    } else {
      prevK = k - 1;
      prevX = (V.get(prevK) ?? 0) + 1;
      const prevY = prevX - prevK;
      // move right: deletion from a
      edits.push({ type: 'delete', aIndex: x - 1, bIndex: y });
      x = prevX - 1;
      y = prevY;
    }
    // follow snake backwards
    while (D > 0 && x > 0 && y > 0 && a[x - 1] === b[y - 1]) {
      edits.push({ type: 'equal', aIndex: x - 1, bIndex: y - 1 });
      x--;
      y--;
    }
  }
  return edits.reverse();
}

// Build unified diff hunks
function buildHunks(edits, contextSize) {
  // Convert edits into sequences grouped by differences with context
  const hunks = [];
  let aStart = null, bStart = null;
  let aCount = 0, bCount = 0;
  let lines = [];

  // Helper to flush current hunk
  function flush() {
    if (aStart === null) return;
    hunks.push({ aStart, aCount, bStart, bCount, lines });
    aStart = bStart = null;
    aCount = bCount = 0;
    lines = [];
  }

  // First, compress equals into runs and differences
  // We'll add context around changed regions
  const equalsIdxs = [];
  for (let i = 0; i < edits.length; i++) {
    if (edits[i].type === 'equal') equalsIdxs.push(i);
  }

  let i = 0;
  while (i < edits.length) {
    // Skip unchanged until we need a hunk
    let eqRunStart = i;
    while (i < edits.length && edits[i].type === 'equal') i++;
    const eqRunEnd = i - 1;

    // If at end (no diff), nothing to flush
    if (i >= edits.length) break;

    // We found a diff at i; include context before
    const preContextStart = Math.max(eqRunStart - contextSize, 0);
    const preContextEnd = eqRunStart - 1;

    // Start new hunk
    aStart = edits[preContextStart].aIndex + 1; // 1-based
    bStart = edits[preContextStart].bIndex + 1;
    lines = [];
    aCount = 0; bCount = 0;

    // Add pre-context
    for (let j = preContextStart; j <= preContextEnd; j++) {
      const idx = edits[j].aIndex;
      lines.push({ sign: ' ', text: aLines[idx] });
      aCount++; bCount++;
    }

    // Add the changed block with following equals context until contextSize
    // Scan until we see contextSize equals after last change
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
      } else { // equal
        lines.push({ sign: ' ', text: aLines[e.aIndex] });
        aCount++; bCount++;
        postEqCount++;
      }
      i++;
    }

    flush();

    // After flushing hunk, continue; i already at position after context
    // Now skip additional equals until a diff resumes
    while (i < edits.length && edits[i].type === 'equal') i++;
  }

  return hunks;
}

const edits = myers(aLines, bLines);

// If no differences
if (edits.every(e => e.type === 'equal')) {
  console.log('Não há diferenças.');
  process.exit(0);
}

const hunks = buildHunks(edits, context);

// Print unified diff
function printUnifiedDiff() {
  const relA = path.relative(process.cwd(), path.resolve(fileA)).replace(/\\/g, '/');
  const relB = path.relative(process.cwd(), path.resolve(fileB)).replace(/\\/g, '/');
  console.log(`--- ${relA}`);
  console.log(`+++ ${relB}`);
  for (const h of hunks) {
    // Calculate counts if zero
    const aCount = h.aCount || 0;
    const bCount = h.bCount || 0;
    console.log(`@@ -${h.aStart},${aCount} +${h.bStart},${bCount} @@`);
    for (const line of h.lines) {
      // Protect control chars
      const text = line.text === undefined ? '' : line.text;
      console.log(`${line.sign}${text}`);
    }
  }
}

printUnifiedDiff();

