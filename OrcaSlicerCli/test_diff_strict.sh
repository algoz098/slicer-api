#!/bin/bash
# Strict diff test for plate 2: lists every differing line between reference and generated G-code
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI_BUILD_DIR="$SCRIPT_DIR/../OrcaSlicerCli/build"
CLI_BIN="$CLI_BUILD_DIR/bin/orcaslicer-cli"
INPUT_3MF="$SCRIPT_DIR/../example_files/3DBenchy.3mf"
OUT_DIR="$SCRIPT_DIR/../output_files"
OUTPUT_3MF2="$OUT_DIR/output_3DBenchy_plate_2.gcode"
REFERENCE_3MF2="$SCRIPT_DIR/../comparable_files/3DBenchy_plate_2.gcode"
TOOLS_DIR="$SCRIPT_DIR/tools"
COMPARE_PY="$TOOLS_DIR/compare_gcode_strict.py"

mkdir -p "$OUT_DIR"

if [ ! -f "$INPUT_3MF" ]; then
  echo "ERRO: Arquivo de entrada não encontrado: $INPUT_3MF" >&2
  exit 1
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "Construindo CLI em $CLI_BUILD_DIR ..." >&2
  mkdir -p "$CLI_BUILD_DIR"
  ( cd "$CLI_BUILD_DIR" && /Applications/CMake.app/Contents/bin/cmake -DCMAKE_BUILD_TYPE=Release .. && /Applications/CMake.app/Contents/bin/cmake --build . -j4 )
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "ERRO: Executável não encontrado em $CLI_BIN" >&2
  exit 1
fi

rm -f "$OUTPUT_3MF2" "${OUTPUT_3MF2}.tmp"
echo "Executando slice 3MF (plate 2)..."
"$CLI_BIN" slice --input "$INPUT_3MF" --output "$OUTPUT_3MF2" --plate 2 2>&1 || true

if [ -f "${OUTPUT_3MF2}.tmp" ]; then
  mv "${OUTPUT_3MF2}.tmp" "$OUTPUT_3MF2"
fi

if [ ! -f "$REFERENCE_3MF2" ]; then
  echo "ERRO: Arquivo de referência não encontrado: $REFERENCE_3MF2" >&2
  exit 1
fi

# Run strict comparisons
FULL_DIFF_TXT="$OUT_DIR/diff_plate_2_full.txt"
BODY_DIFF_TXT="$OUT_DIR/diff_plate_2_body.txt"

chmod +x "$COMPARE_PY" || true

# Full-file comparison
echo "Comparando (arquivo inteiro)..."
python3 "$COMPARE_PY" "$REFERENCE_3MF2" "$OUTPUT_3MF2" --out "$FULL_DIFF_TXT" 2> "$OUT_DIR/diff_plate_2_full_summary.txt" || true

# Body-only (after CONFIG_BLOCK_END)
echo "Comparando (apenas G-code após CONFIG_BLOCK_END)..."
python3 "$COMPARE_PY" "$REFERENCE_3MF2" "$OUTPUT_3MF2" --after-config --out "$BODY_DIFF_TXT" 2> "$OUT_DIR/diff_plate_2_body_summary.txt" || true

# Summaries
echo ""
echo "Resultados gravados em:"
echo "  - $FULL_DIFF_TXT (todas as diferenças, arquivo inteiro)"
echo "  - $BODY_DIFF_TXT (todas as diferenças, apenas corpo do G-code)"
echo "Resumo:"
cat "$OUT_DIR/diff_plate_2_full_summary.txt" || true
cat "$OUT_DIR/diff_plate_2_body_summary.txt" || true

# Show first few differences for quick inspection
echo ""
echo "Primeiras 10 diferenças (arquivo inteiro):"
head -n 20 "$FULL_DIFF_TXT" || true

echo ""
echo "Primeiras 10 diferenças (após CONFIG_BLOCK_END):"
head -n 20 "$BODY_DIFF_TXT" || true

