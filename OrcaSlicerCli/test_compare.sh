#!/bin/bash

# Script para testar e comparar G-code gerado com arquivo de referência
# Uso: ./test_compare.sh [PRINTER] [FILAMENT] [PROCESS]

set -e

# Perfis padrão (mesmos do arquivo 3DBenchy.gcode de referência)
DEFAULT_PRINTER="Bambu Lab X1 Carbon 0.4 nozzle"
DEFAULT_FILAMENT="Bambu PLA Matte @BBL X1C"
DEFAULT_PROCESS="0.20mm Standard @BBL X1C"

# Usar parâmetros ou valores padrão
PRINTER_PROFILE="${1:-$DEFAULT_PRINTER}"
FILAMENT_PROFILE="${2:-$DEFAULT_FILAMENT}"
PROCESS_PROFILE="${3:-$DEFAULT_PROCESS}"

# Caminhos
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
INPUT_FILE="$SCRIPT_DIR/../example_files/3DBenchy.stl"
OUTPUT_FILE="$SCRIPT_DIR/../output_files/test_compare.gcode"
REFERENCE_FILE="$SCRIPT_DIR/../comparable_files/3DBenchy.gcode"

echo "🔧 TESTE DE COMPARAÇÃO - OrcaSlicer CLI"
echo "========================================"
echo ""
echo "📋 Perfis utilizados:"
echo "   🖨️  Printer:  $PRINTER_PROFILE"
echo "   🧵 Filament: $FILAMENT_PROFILE"
echo "   ⚙️  Process:  $PROCESS_PROFILE"
echo ""

# Verificar executável
if [ ! -f "build/bin/orcaslicer-cli" ]; then
    echo "❌ ERRO: Executável não encontrado em build/bin/orcaslicer-cli"
    exit 1
fi

# Verificar arquivo de entrada
if [ ! -f "$INPUT_FILE" ]; then
    echo "❌ ERRO: Arquivo de entrada não encontrado: $INPUT_FILE"
    exit 1
fi

# Verificar arquivo de referência
if [ ! -f "$REFERENCE_FILE" ]; then
    echo "❌ ERRO: Arquivo de referência não encontrado: $REFERENCE_FILE"
    exit 1
fi

# Criar diretório de saída
mkdir -p "$(dirname "$OUTPUT_FILE")"
rm -f "$OUTPUT_FILE" "${OUTPUT_FILE}.tmp"

echo "📁 Arquivos:"
echo "   📥 Input:      $INPUT_FILE"
echo "   📤 Output:     $OUTPUT_FILE"
echo "   📋 Reference:  $REFERENCE_FILE"
echo ""

# Executar slice
echo "⚙️  Gerando G-code..."
cd build
./bin/orcaslicer-cli slice \
    --input "$INPUT_FILE" \
    --output "$OUTPUT_FILE" \
    --printer "$PRINTER_PROFILE" \
    --filament "$FILAMENT_PROFILE" \
    --process "$PROCESS_PROFILE" 2>&1 | grep -E "(INFO|DEBUG|ERROR)" || true
cd ..

# Verificar resultado
if [ -f "${OUTPUT_FILE}.tmp" ]; then
    mv "${OUTPUT_FILE}.tmp" "$OUTPUT_FILE"
fi

if [ ! -f "$OUTPUT_FILE" ]; then
    echo "❌ ERRO: Falha na geração do G-code"
    exit 1
fi

FILE_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null)
if [ "$FILE_SIZE" -lt 100000 ]; then
    echo "❌ ERRO: Arquivo muito pequeno (${FILE_SIZE} bytes)"
    exit 1
fi

echo "✅ G-code gerado com sucesso: $(( FILE_SIZE / 1024 ))KB"
echo ""

# COMPARAÇÕES DETALHADAS
echo "🔍 ANÁLISE COMPARATIVA DETALHADA"
echo "================================="
echo ""

# Função para extrair blocos
extract_header_block() {
    sed -n '/; HEADER_BLOCK_START/,/; HEADER_BLOCK_END/p' "$1"
}

extract_config_block() {
    sed -n '/; CONFIG_BLOCK_START/,/; CONFIG_BLOCK_END/p' "$1"
}

# 1. ESTATÍSTICAS BÁSICAS
echo "📊 1. ESTATÍSTICAS BÁSICAS:"
echo "   ┌─────────────────────────────────────────┐"

REF_LINES=$(wc -l < "$REFERENCE_FILE")
OUT_LINES=$(wc -l < "$OUTPUT_FILE")
REF_SIZE=$(stat -f%z "$REFERENCE_FILE" 2>/dev/null || stat -c%s "$REFERENCE_FILE" 2>/dev/null)
OUT_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null)

printf "   │ %-20s │ %10s │ %10s │\n" "Métrica" "Referência" "Gerado"
echo "   ├─────────────────────────────────────────┤"
printf "   │ %-20s │ %10d │ %10d │\n" "Linhas" "$REF_LINES" "$OUT_LINES"
printf "   │ %-20s │ %7dKB │ %7dKB │\n" "Tamanho" "$((REF_SIZE / 1024))" "$((OUT_SIZE / 1024))"

DIFF_LINES=$((OUT_LINES - REF_LINES))
DIFF_SIZE=$((OUT_SIZE - REF_SIZE))
if [ $DIFF_LINES -eq 0 ]; then
    printf "   │ %-20s │ %10s │ %10s │\n" "Diferença Linhas" "-" "✅ Igual"
else
    printf "   │ %-20s │ %10s │ %+9d │\n" "Diferença Linhas" "-" "$DIFF_LINES"
fi

if [ $DIFF_SIZE -eq 0 ]; then
    printf "   │ %-20s │ %10s │ %10s │\n" "Diferença Tamanho" "-" "✅ Igual"
else
    printf "   │ %-20s │ %10s │ %+7dKB │\n" "Diferença Tamanho" "-" "$((DIFF_SIZE / 1024))"
fi
echo "   └─────────────────────────────────────────┘"
echo ""

# 2. HEADER_BLOCK
echo "📊 2. HEADER_BLOCK:"
REF_HEADER=$(extract_header_block "$REFERENCE_FILE")
GEN_HEADER=$(extract_header_block "$OUTPUT_FILE")

if [ "$REF_HEADER" = "$GEN_HEADER" ]; then
    echo "   ✅ IDÊNTICO"
else
    echo "   ❌ DIFERENTE"
    echo ""
    echo "   📋 Principais diferenças:"
    
    # Extrair informações específicas
    REF_TIME=$(echo "$REF_HEADER" | grep "model printing time" | sed 's/.*: //')
    GEN_TIME=$(echo "$GEN_HEADER" | grep "model printing time" | sed 's/.*: //')
    
    REF_DENSITY=$(echo "$REF_HEADER" | grep "filament_density" | sed 's/.*: //')
    GEN_DENSITY=$(echo "$GEN_HEADER" | grep "filament_density" | sed 's/.*: //')
    
    REF_DIAMETER=$(echo "$REF_HEADER" | grep "filament_diameter" | sed 's/.*: //')
    GEN_DIAMETER=$(echo "$GEN_HEADER" | grep "filament_diameter" | sed 's/.*: //')
    
    echo "   • Tempo de impressão:"
    echo "     - Referência: $REF_TIME"
    echo "     - Gerado:     $GEN_TIME"
    echo ""
    echo "   • Densidade do filamento:"
    echo "     - Referência: $REF_DENSITY"
    echo "     - Gerado:     $GEN_DENSITY"
    echo ""
    echo "   • Diâmetro do filamento:"
    echo "     - Referência: $REF_DIAMETER"
    echo "     - Gerado:     $GEN_DIAMETER"
fi
echo ""

# 3. CONFIG_BLOCK - Análise de configurações críticas
echo "📊 3. CONFIG_BLOCK - Configurações Críticas:"

# Extrair configurações específicas
get_config_value() {
    local file="$1"
    local key="$2"
    extract_config_block "$file" | grep "^; $key = " | sed "s/^; $key = //"
}

echo "   ┌─────────────────────────────────────────────────────────────┐"
printf "   │ %-25s │ %-15s │ %-15s │\n" "Configuração" "Referência" "Gerado"
echo "   ├─────────────────────────────────────────────────────────────┤"

# Configurações importantes para comparar
configs=(
    "default_acceleration"
    "outer_wall_speed"
    "inner_wall_speed"
    "nozzle_temperature"
    "hot_plate_temp"
    "filament_settings_id"
    "printer_settings_id"
    "print_settings_id"
)

for config in "${configs[@]}"; do
    ref_val=$(get_config_value "$REFERENCE_FILE" "$config")
    gen_val=$(get_config_value "$OUTPUT_FILE" "$config")
    
    # Truncar valores muito longos
    ref_val_short=$(echo "$ref_val" | cut -c1-15)
    gen_val_short=$(echo "$gen_val" | cut -c1-15)
    
    if [ "$ref_val" = "$gen_val" ]; then
        printf "   │ %-25s │ %-15s │ %-15s │\n" "$config" "$ref_val_short" "✅ Igual"
    else
        printf "   │ %-25s │ %-15s │ %-15s │\n" "$config" "$ref_val_short" "$gen_val_short"
    fi
done

echo "   └─────────────────────────────────────────────────────────────┘"
echo ""

# 4. RESUMO FINAL
echo "🏁 RESUMO FINAL:"
echo "==============="

TOTAL_ISSUES=0

if [ $DIFF_LINES -ne 0 ]; then
    echo "❌ Número de linhas diferente (+$DIFF_LINES)"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
fi

if [ "$REF_HEADER" != "$GEN_HEADER" ]; then
    echo "❌ HEADER_BLOCK diferente"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
fi

REF_CONFIG=$(extract_config_block "$REFERENCE_FILE")
GEN_CONFIG=$(extract_config_block "$OUTPUT_FILE")
if [ "$REF_CONFIG" != "$GEN_CONFIG" ]; then
    echo "❌ CONFIG_BLOCK diferente"
    TOTAL_ISSUES=$((TOTAL_ISSUES + 1))
fi

echo ""
if [ $TOTAL_ISSUES -eq 0 ]; then
    echo "🎉 SUCESSO: G-code idêntico ao arquivo de referência!"
    exit 0
else
    echo "⚠️  DIFERENÇAS ENCONTRADAS: $TOTAL_ISSUES problema(s) identificado(s)"
    echo ""
    echo "💡 Principais causas prováveis:"
    echo "   • Arquivo de referência gerado com multi-extrusor (5 extrusores)"
    echo "   • CLI configurado para extrusor único"
    echo "   • Diferenças nas configurações de perfil"
    echo ""
    echo "📁 Arquivos para análise manual:"
    echo "   • Gerado:    $OUTPUT_FILE"
    echo "   • Referência: $REFERENCE_FILE"
    exit 1
fi
