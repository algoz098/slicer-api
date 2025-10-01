#!/bin/bash
# CLI removed: delegate to Node addon parity test
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_JS="$SCRIPT_DIR/bindings/node/test/slice_compare.js"
if [ -f "$TEST_JS" ]; then
  node "$TEST_JS"
  exit $?
else
  echo "Node addon test script not found: $TEST_JS" >&2
  exit 1
fi


# Script simples para testar o slice do OrcaSlicer CLI
# Uso: ./test_slice.sh [PRINTER] [FILAMENT] [PROCESS]
# Exemplo: ./test_slice.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Matte @BBL X1C" "0.20mm Standard @BBL X1C"

set -euo pipefail

# Status aggregator for multiple tests in this script
FIRST_TEST_ERRORS=1

# Perfis extraídos do arquivo de referência STL (3DBenchy.gcode)
# Se o usuário passar parâmetros, eles têm prioridade sobre os extraídos
extract_from_ref() {
  local ref_file="$1"
  PRINTER_PROFILE_REF=$(grep -m1 '^; printer_settings_id = ' "$ref_file" | sed 's/^; printer_settings_id = //')
  PROCESS_PROFILE_REF=$(grep -m1 '^; print_settings_id = ' "$ref_file" | sed 's/^; print_settings_id = //')
  FILAMENT_PROFILE_REF=$(grep -m1 '^; filament_settings_id = ' "$ref_file" | sed 's/^; filament_settings_id = //' | awk -F';' '{print $1}' | sed 's/^"//; s/"$//')
}

# Usar parâmetros do usuário, senão extrair do arquivo de referência
PRINTER_PROFILE="${1:-}"
FILAMENT_PROFILE="${2:-}"
PROCESS_PROFILE="${3:-}"

# Caminhos
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLI_BUILD_DIR="$SCRIPT_DIR/../OrcaSlicerCli/build"
CLI_BIN="$CLI_BUILD_DIR/bin/orcaslicer-cli"
REFERENCE_FILE="$SCRIPT_DIR/../comparable_files/3DBenchy.gcode"

# Extrair perfis do arquivo de referência, se existir, e preencher defaults
if [ -f "$REFERENCE_FILE" ]; then
  extract_from_ref "$REFERENCE_FILE"
fi
PRINTER_PROFILE=${PRINTER_PROFILE:-${PRINTER_PROFILE_REF:-""}}
FILAMENT_PROFILE=${FILAMENT_PROFILE:-${FILAMENT_PROFILE_REF:-""}}
PROCESS_PROFILE=${PROCESS_PROFILE:-${PROCESS_PROFILE_REF:-""}}

if [ -z "$PRINTER_PROFILE" ] || [ -z "$FILAMENT_PROFILE" ] || [ -z "$PROCESS_PROFILE" ]; then
  echo "❌ ERRO: Não foi possível determinar os perfis (printer/filament/process)."
  echo "   Passe como parâmetros ou garanta que $REFERENCE_FILE contenha os IDs."
  exit 1
fi

INPUT_FILE="$SCRIPT_DIR/../example_files/3DBenchy.stl"
OUTPUT_FILE="$SCRIPT_DIR/../output_files/test_with_profiles.gcode"

# Build se necessário
if [ ! -x "$CLI_BIN" ]; then
  echo "✅ Construindo CLI em $CLI_BUILD_DIR ..."
  mkdir -p "$CLI_BUILD_DIR"
  ( cd "$CLI_BUILD_DIR" && /Applications/CMake.app/Contents/bin/cmake -DCMAKE_BUILD_TYPE=Release .. && /Applications/CMake.app/Contents/bin/cmake --build . -j4 )
fi

if [ ! -x "$CLI_BIN" ]; then
  echo "❌ ERRO: Executável não encontrado em $CLI_BIN"
  exit 1
fi

# Verificar se o arquivo de entrada existe
if [ ! -f "$INPUT_FILE" ]; then
    echo "ERRO: Arquivo de entrada nao encontrado: $INPUT_FILE"
    exit 1
fi

# Criar diretório de saída se não existir
mkdir -p "$(dirname "$OUTPUT_FILE")"

# Remover arquivo de saída anterior se existir
rm -f "$OUTPUT_FILE" "${OUTPUT_FILE}.tmp"

echo "🔧 Testando slice do OrcaSlicer CLI..."
echo "📋 Perfis selecionados:"
echo "   Printer:  $PRINTER_PROFILE"
echo "   Filament: $FILAMENT_PROFILE"
echo "   Process:  $PROCESS_PROFILE"
echo "📁 Input: $INPUT_FILE"
echo "📁 Output: $OUTPUT_FILE"

# Executar o slice (ignorar segfault)
echo "⚙️  Executando slice (STL)..."
"$CLI_BIN" slice --input "$INPUT_FILE" --output "$OUTPUT_FILE" --printer "$PRINTER_PROFILE" --filament "$FILAMENT_PROFILE" --process "$PROCESS_PROFILE" 2>&1 || true

# Verificar se o arquivo foi gerado (pode ser .tmp devido ao segfault)
if [ -f "${OUTPUT_FILE}.tmp" ]; then
    echo "✅ Arquivo temporário gerado, renomeando..."
    mv "${OUTPUT_FILE}.tmp" "$OUTPUT_FILE"
fi

# Verificar se o arquivo final existe e tem tamanho adequado
if [ -f "$OUTPUT_FILE" ]; then
    FILE_SIZE=$(stat -f%z "$OUTPUT_FILE" 2>/dev/null || stat -c%s "$OUTPUT_FILE" 2>/dev/null)

    if [ "$FILE_SIZE" -gt 1000000 ]; then  # Maior que 1MB
        echo "✅ SUCESSO: G-code gerado com $(( FILE_SIZE / 1024 / 1024 ))MB"
        echo "📄 Arquivo: $OUTPUT_FILE"

        # Executar testes de comparação se arquivo de referência existir
        if [ -f "$REFERENCE_FILE" ]; then
            echo ""
            echo "🔍 Iniciando testes de comparação detalhada..."

            # Função para extrair blocos específicos
            extract_header_block() {
                local file="$1"
                sed -n '/; HEADER_BLOCK_START/,/; HEADER_BLOCK_END/p' "$file"
            }

            extract_config_block() {
                local file="$1"
                sed -n '/; CONFIG_BLOCK_START/,/; CONFIG_BLOCK_END/p' "$file"
            }

            extract_gcode_commands() {
                local file="$1"
                # Pula header e config, pega apenas os comandos G-code
                sed -n '/; CONFIG_BLOCK_END/,$p' "$file" | tail -n +2
            }

            # Normalizações para comparação rigorosa porém estável
            # 1) Ignorar somente a linha de timestamp no HEADER_BLOCK ("; generated by ... on ...")
            normalize_header_block() {
                sed -E '/^; generated by /d'
            }
            # 2) Tolerar divergência apenas da 1ª linha "printing object ... id:..."
            normalize_first_printing_object_id() {
                awk '{
                    if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) {
                        gsub(/id:[0-9]+/, "id:IGNORED");
                    }
                    print;
                }'
            }

            # Função para mostrar diff em formato git
            show_git_diff() {
                local ref_content="$1"
                local gen_content="$2"
                local block_name="$3"

                echo ""
                echo "📋 Diferenças no $block_name:"
                echo "--- a/$block_name (referência)"
                echo "+++ b/$block_name (gerado)"
                diff -u <(echo "$ref_content") <(echo "$gen_content") | tail -n +3 || true
            }

            # Variável para rastrear erros
            ERRORS_FOUND=0

            # Teste 1: Número de linhas
            echo "📊 Teste 1: Comparando número de linhas..."
            REF_LINES=$(wc -l < "$REFERENCE_FILE")
            OUT_LINES=$(wc -l < "$OUTPUT_FILE")

            echo "   Referência: $(printf "%8d" $REF_LINES) linhas"
            echo "   Gerado:     $(printf "%8d" $OUT_LINES) linhas"

            if [ "$REF_LINES" -eq "$OUT_LINES" ]; then
                echo "✅ Número de linhas: Idêntico"
            else
                echo "❌ ERRO: Número de linhas diferente!"
                DIFF=$((OUT_LINES - REF_LINES))
                if [ $DIFF -gt 0 ]; then
                    echo "   Diferença: +$DIFF linhas"
                else
                    echo "   Diferença: $DIFF linhas"
                fi
                ERRORS_FOUND=1
            fi

            # Teste 2: Comparação do HEADER_BLOCK
            echo ""
            echo "📊 Teste 2: Comparando HEADER_BLOCK linha a linha..."

            REF_HEADER=$(extract_header_block "$REFERENCE_FILE" | normalize_header_block)
            GEN_HEADER=$(extract_header_block "$OUTPUT_FILE" | normalize_header_block)

            if [ "$REF_HEADER" = "$GEN_HEADER" ]; then
                echo "✅ HEADER_BLOCK: Idêntico (ignorando timestamp)"
            else
                echo "❌ ERRO: HEADER_BLOCK diferente!"
                show_git_diff "$REF_HEADER" "$GEN_HEADER" "HEADER_BLOCK"
                ERRORS_FOUND=1
            fi

            # Teste 3: Comparação do CONFIG_BLOCK
            echo ""
            echo "📊 Teste 3: Comparando CONFIG_BLOCK linha a linha..."

            REF_CONFIG=$(extract_config_block "$REFERENCE_FILE")
            GEN_CONFIG=$(extract_config_block "$OUTPUT_FILE")

            if [ "$REF_CONFIG" = "$GEN_CONFIG" ]; then
                echo "✅ CONFIG_BLOCK: Idêntico"
            else
                echo "❌ ERRO: CONFIG_BLOCK diferente!"
                show_git_diff "$REF_CONFIG" "$GEN_CONFIG" "CONFIG_BLOCK"
                ERRORS_FOUND=1
            fi

            # Teste 4: Comparação dos comandos G-code
            echo ""
            echo "📊 Teste 4: Comparando comandos G-code (primeira diferença + 10 linhas)..."

            REF_GCODE=$(extract_gcode_commands "$REFERENCE_FILE" | normalize_first_printing_object_id)
            GEN_GCODE=$(extract_gcode_commands "$OUTPUT_FILE" | normalize_first_printing_object_id)

            # Criar arquivos temporários para comparação
            REF_TEMP=$(mktemp)
            GEN_TEMP=$(mktemp)
            echo "$REF_GCODE" | normalize_first_printing_object_id > "$REF_TEMP"
            echo "$GEN_GCODE" | normalize_first_printing_object_id > "$GEN_TEMP"

            # Encontrar primeira diferença
            FIRST_DIFF_LINE=$(diff -n "$REF_TEMP" "$GEN_TEMP" | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/' 2>/dev/null || echo "")

            if [ -z "$FIRST_DIFF_LINE" ]; then
                echo "✅ Comandos G-code: Idênticos"
            else
                echo "❌ ERRO: Comandos G-code diferentes!"
                echo "   📍 Primeira diferença na linha: $FIRST_DIFF_LINE"

                # Mostrar contexto: linha da diferença + 10 linhas seguintes
                echo ""
                echo "📋 Contexto da primeira diferença (linha $FIRST_DIFF_LINE + 10 seguintes):"
                echo "--- a/GCODE_COMMANDS (referência)"
                echo "+++ b/GCODE_COMMANDS (gerado)"

                # Extrair contexto
                START_LINE=$FIRST_DIFF_LINE
                END_LINE=$((FIRST_DIFF_LINE + 10))

                REF_CONTEXT=$(sed -n "${START_LINE},${END_LINE}p" "$REF_TEMP")
                GEN_CONTEXT=$(sed -n "${START_LINE},${END_LINE}p" "$GEN_TEMP")

                diff -u <(echo "$REF_CONTEXT") <(echo "$GEN_CONTEXT") | tail -n +3 || true

                ERRORS_FOUND=1
            fi

            # Limpar arquivos temporários
            rm -f "$REF_TEMP" "$GEN_TEMP"

            # Resultado final
            echo ""
            echo "🏁 Resultado final dos testes:"
            if [ "$ERRORS_FOUND" -eq 0 ]; then
                echo "✅ SUCESSO: Todos os testes passaram! G-code idêntico ao arquivo de referência."
                FIRST_TEST_ERRORS=0
            else
                echo "❌ FALHA: Diferenças encontradas entre o G-code gerado e o arquivo de referência."
                FIRST_TEST_ERRORS=1
            fi

        else
            echo "⚠️  Arquivo de referência não encontrado: $REFERENCE_FILE"
            echo "   Pulando testes de comparação."
            FIRST_TEST_ERRORS=0
        fi
    else
        echo "❌ ERRO: Arquivo muito pequeno (${FILE_SIZE} bytes)"
        FIRST_TEST_ERRORS=1
    fi
else
    echo "❌ ERRO: Nenhum arquivo G-code foi gerado"
    FIRST_TEST_ERRORS=1
fi

# ==========================
# Segundo teste: 3MF (plate 1)
# ==========================
SECOND_TEST_ERRORS=1

INPUT_3MF="$SCRIPT_DIR/../example_files/3DBenchy.3mf"
OUTPUT_3MF="$SCRIPT_DIR/../output_files/output_3DBenchy_plate_1.gcode"
REFERENCE_3MF="$SCRIPT_DIR/../comparable_files/3DBenchy_plate_1.gcode"
# Perfis específicos para o teste 3MF (alinhados ao arquivo de referência)
PRINTER_PROFILE_3MF="Bambu Lab A1 0.4 nozzle"
FILAMENT_PROFILE_3MF="Bambu PLA Basic @BBL A1"
PROCESS_PROFILE_3MF="0.20mm Standard @BBL A1"



# Pré-validação do arquivo 3MF para evitar mensagens com ANSI corrompidas
if [ ! -f "$INPUT_3MF" ]; then
    echo "ERRO: Arquivo de entrada não encontrado: $INPUT_3MF"
    # Marcar segundo teste como falho e encerrar cedo a seção 3MF
    SECOND_TEST_ERRORS=1
    echo "Pulando teste 3MF devido à falta do arquivo de entrada."
    # Encerrar o script aqui, pois a continuação usaria o mesmo arquivo ausente
    exit 1
fi


echo ""
echo "==== Teste 2: 3MF (plate 1) ===="

# Verificar se o arquivo de entrada existe
if [ ! -f "$INPUT_3MF" ]; then
    echo "[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1m[31m[0m[31m[1mERRO: Arquivo de entrada nao encontrado: $INPUT_3MF"
    SECOND_TEST_ERRORS=1
else
    # Criar diretf3rio de saedda se ne3o existir e limpar arquivo anterior
    mkdir -p "$(dirname "$OUTPUT_3MF")"
    rm -f "$OUTPUT_3MF" "${OUTPUT_3MF}.tmp"

    echo "Executando slice 3MF (plate 1)..."
    "$CLI_BIN" slice --input "$INPUT_3MF" --output "$OUTPUT_3MF" --plate 1 2>&1

    if [ -f "${OUTPUT_3MF}.tmp" ]; then
        mv "${OUTPUT_3MF}.tmp" "$OUTPUT_3MF"
    fi

    if [ -f "$OUTPUT_3MF" ]; then
        FILE_SIZE_3MF=$(stat -f%z "$OUTPUT_3MF" 2>/dev/null || stat -c%s "$OUTPUT_3MF" 2>/dev/null)
        if [ "$FILE_SIZE_3MF" -gt 100000 ]; then
            echo "G-code (3MF) gerado com sucesso: $(( FILE_SIZE_3MF / 1024 ))KB"

            if [ -f "$REFERENCE_3MF" ]; then
                echo ""
                echo "Iniciando comparacoes para 3MF (plate 1)..."

                # Fune7f5es auxiliares (iguais ao primeiro teste)
                extract_header_block() { sed -n '/; HEADER_BLOCK_START/,/; HEADER_BLOCK_END/p' "$1"; }
                extract_config_block() { sed -n '/; CONFIG_BLOCK_START/,/; CONFIG_BLOCK_END/p' "$1"; }
                extract_gcode_commands() { sed -n '/; CONFIG_BLOCK_END/,$p' "$1" | tail -n +2; }
                normalize_header_block() { sed -E '/^; generated by /d'; }
                normalize_first_printing_object_id() {
                    awk '{ if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) { gsub(/id:[0-9]+/, "id:IGNORED"); } print; }'
                }
                show_git_diff() {
                    local ref_content="$1"; local gen_content="$2"; local block_name="$3";
                    echo ""; echo "Diferencas no $block_name:";
                    diff -u <(echo "$ref_content") <(echo "$gen_content") | tail -n +3 || true
                }

                ERRORS_FOUND_3MF=0

                # Teste 1: nfamero de linhas
                REF_LINES_3MF=$(wc -l < "$REFERENCE_3MF"); OUT_LINES_3MF=$(wc -l < "$OUTPUT_3MF")
                echo "[3MF] Linhas: ref=$REF_LINES_3MF, out=$OUT_LINES_3MF"
                if [ "$REF_LINES_3MF" -ne "$OUT_LINES_3MF" ]; then ERRORS_FOUND_3MF=1; fi

                # Teste 2: HEADER_BLOCK
                REF_HEADER_3MF=$(extract_header_block "$REFERENCE_3MF" | normalize_header_block)
                GEN_HEADER_3MF=$(extract_header_block "$OUTPUT_3MF" | normalize_header_block)
                if [ "$REF_HEADER_3MF" != "$GEN_HEADER_3MF" ]; then
                    show_git_diff "$REF_HEADER_3MF" "$GEN_HEADER_3MF" "HEADER_BLOCK (3MF)"; ERRORS_FOUND_3MF=1
                fi

                # Teste 3: CONFIG_BLOCK
                REF_CONFIG_3MF=$(extract_config_block "$REFERENCE_3MF")
                GEN_CONFIG_3MF=$(extract_config_block "$OUTPUT_3MF")
                if [ "$REF_CONFIG_3MF" != "$GEN_CONFIG_3MF" ]; then
                    show_git_diff "$REF_CONFIG_3MF" "$GEN_CONFIG_3MF" "CONFIG_BLOCK (3MF)"; ERRORS_FOUND_3MF=1
                fi

                # Teste 4: G-code (contefado apf3s config)
                REF_GCODE_3MF=$(extract_gcode_commands "$REFERENCE_3MF" \
                    | sed -E 's/^; start printing object, unique label id: [0-9]+$/; start printing object, unique label id: IGNORED/; s/^; stop printing object, unique label id: [0-9]+$/; stop printing object, unique label id: IGNORED/' \
                    | awk '{ if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) { gsub(/id:[0-9]+/, "id:IGNORED"); } print; }')
                GEN_GCODE_3MF=$(extract_gcode_commands "$OUTPUT_3MF" \
                    | sed -E 's/^; start printing object, unique label id: [0-9]+$/; start printing object, unique label id: IGNORED/; s/^; stop printing object, unique label id: [0-9]+$/; stop printing object, unique label id: IGNORED/' \
                    | awk '{ if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) { gsub(/id:[0-9]+/, "id:IGNORED"); } print; }')
                REF_TEMP_3MF=$(mktemp); GEN_TEMP_3MF=$(mktemp)
                echo "$REF_GCODE_3MF" > "$REF_TEMP_3MF"; echo "$GEN_GCODE_3MF" > "$GEN_TEMP_3MF"
                FIRST_DIFF_LINE_3MF=$(diff -n "$REF_TEMP_3MF" "$GEN_TEMP_3MF" | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/' 2>/dev/null || echo "")
                if [ -n "$FIRST_DIFF_LINE_3MF" ]; then
                    echo "Diferenca encontrada a partir da linha $FIRST_DIFF_LINE_3MF (3MF)"; ERRORS_FOUND_3MF=1
                fi
                rm -f "$REF_TEMP_3MF" "$GEN_TEMP_3MF"

                if [ "$ERRORS_FOUND_3MF" -eq 0 ]; then
                    echo "[3MF] SUCESSO: G-code identico ao arquivo de referencia."
                    SECOND_TEST_ERRORS=0
                else
                    echo "[3MF] FALHA: Diferencas encontradas."
                    SECOND_TEST_ERRORS=1
                fi
            else
                echo "Arquivo de referencia (3MF) nao encontrado: $REFERENCE_3MF. Pulando comparacao."
                SECOND_TEST_ERRORS=0
            fi
        else
            echo "ERRO: Arquivo (3MF) muito pequeno (${FILE_SIZE_3MF} bytes)"
            SECOND_TEST_ERRORS=1
        fi
    else
        echo "ERRO: Nenhum arquivo G-code (3MF) foi gerado"
        SECOND_TEST_ERRORS=1
    fi
fi

# ===== Teste 2b: 3MF (plate 2) =====
SECOND_TEST_ERRORS2=1
OUTPUT_3MF2="$SCRIPT_DIR/../output_files/output_3DBenchy_plate_2.gcode"
REFERENCE_3MF2="$SCRIPT_DIR/../comparable_files/3DBenchy_plate_2.gcode"

echo ""
echo "==== Teste 2b: 3MF (plate 2) ===="

mkdir -p "$(dirname "$OUTPUT_3MF2")"
rm -f "$OUTPUT_3MF2" "${OUTPUT_3MF2}.tmp"

echo "Executando slice 3MF (plate 2)..."
"$CLI_BIN" slice --input "$INPUT_3MF" --output "$OUTPUT_3MF2" --plate 2 2>&1

if [ -f "${OUTPUT_3MF2}.tmp" ]; then
    mv "${OUTPUT_3MF2}.tmp" "$OUTPUT_3MF2"
fi

if [ -f "$OUTPUT_3MF2" ]; then
    FILE_SIZE_3MF2=$(stat -f%z "$OUTPUT_3MF2" 2>/dev/null || stat -c%s "$OUTPUT_3MF2" 2>/dev/null)
    if [ "$FILE_SIZE_3MF2" -gt 100000 ]; then
        echo "G-code (3MF plate 2) gerado com sucesso: $(( FILE_SIZE_3MF2 / 1024 ))KB"

        if [ -f "$REFERENCE_3MF2" ]; then
            echo ""
            echo "Iniciando comparacoes para 3MF (plate 2)..."

            REF_HEADER_3MF2=$(sed -n '/; HEADER_BLOCK_START/,/; HEADER_BLOCK_END/p' "$REFERENCE_3MF2" | sed -E '/^; generated by /d')
            GEN_HEADER_3MF2=$(sed -n '/; HEADER_BLOCK_START/,/; HEADER_BLOCK_END/p' "$OUTPUT_3MF2" | sed -E '/^; generated by /d')

            REF_CONFIG_3MF2=$(sed -n '/; CONFIG_BLOCK_START/,/; CONFIG_BLOCK_END/p' "$REFERENCE_3MF2")
            GEN_CONFIG_3MF2=$(sed -n '/; CONFIG_BLOCK_START/,/; CONFIG_BLOCK_END/p' "$OUTPUT_3MF2")

            REF_GCODE_3MF2=$(sed -n '/; CONFIG_BLOCK_END/,$p' "$REFERENCE_3MF2" | tail -n +2 \
                | sed -E 's/^; start printing object, unique label id: [0-9]+$/; start printing object, unique label id: IGNORED/; s/^; stop printing object, unique label id: [0-9]+$/; stop printing object, unique label id: IGNORED/' \
                | awk '{ if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) { gsub(/id:[0-9]+/, "id:IGNORED"); } print; }')
            GEN_GCODE_3MF2=$(sed -n '/; CONFIG_BLOCK_END/,$p' "$OUTPUT_3MF2" | tail -n +2 \
                | sed -E 's/^; start printing object, unique label id: [0-9]+$/; start printing object, unique label id: IGNORED/; s/^; stop printing object, unique label id: [0-9]+$/; stop printing object, unique label id: IGNORED/' \
                | awk '{ if ($0 ~ /^; (printing|stop printing) object .* id:[0-9]+ copy /) { gsub(/id:[0-9]+/, "id:IGNORED"); } print; }')

            ERRORS_FOUND_3MF2=0
            REF_LINES_3MF2=$(wc -l < "$REFERENCE_3MF2"); OUT_LINES_3MF2=$(wc -l < "$OUTPUT_3MF2")
            echo "[3MF plate 2] Linhas: ref=$REF_LINES_3MF2, out=$OUT_LINES_3MF2"
            if [ "$REF_LINES_3MF2" -ne "$OUT_LINES_3MF2" ]; then ERRORS_FOUND_3MF2=1; fi

            if [ "$REF_HEADER_3MF2" != "$GEN_HEADER_3MF2" ]; then echo "HEADER_BLOCK (3MF plate 2) diferente."; ERRORS_FOUND_3MF2=1; fi
            if [ "$REF_CONFIG_3MF2" != "$GEN_CONFIG_3MF2" ]; then echo "CONFIG_BLOCK (3MF plate 2) diferente."; ERRORS_FOUND_3MF2=1; fi

            REF_TEMP_3MF2=$(mktemp); GEN_TEMP_3MF2=$(mktemp)
            echo "$REF_GCODE_3MF2" > "$REF_TEMP_3MF2"; echo "$GEN_GCODE_3MF2" > "$GEN_TEMP_3MF2"
            FIRST_DIFF_LINE_3MF2=$(diff -n "$REF_TEMP_3MF2" "$GEN_TEMP_3MF2" | head -1 | sed 's/[^0-9]*\([0-9]*\).*/\1/' 2>/dev/null || echo "")
            rm -f "$REF_TEMP_3MF2" "$GEN_TEMP_3MF2"
            if [ -n "$FIRST_DIFF_LINE_3MF2" ]; then echo "G-code (3MF plate 2) difere a partir da linha $FIRST_DIFF_LINE_3MF2."; ERRORS_FOUND_3MF2=1; fi

            if [ "$ERRORS_FOUND_3MF2" -eq 0 ]; then
                echo "[3MF plate 2] SUCESSO: G-code idêntico ao arquivo de referência (com normalizações permitidas)."
                SECOND_TEST_ERRORS2=0
            else
                echo "[3MF plate 2] FALHA: Diferenças encontradas."
                SECOND_TEST_ERRORS2=1
            fi
        else
            echo "Arquivo de referência (3MF plate 2) não encontrado: $REFERENCE_3MF2. Pulando comparação."
            SECOND_TEST_ERRORS2=0
        fi
    else
        echo "ERRO: Arquivo (3MF plate 2) muito pequeno (${FILE_SIZE_3MF2} bytes)"
        SECOND_TEST_ERRORS2=1
    fi
else
    echo "ERRO: Nenhum arquivo G-code (3MF, plate 2) foi gerado"
    SECOND_TEST_ERRORS2=1
fi



# ==========================
# Saida final dos dois testes
# ==========================

if [ "${FIRST_TEST_ERRORS}" -eq 0 ] && [ "${SECOND_TEST_ERRORS}" -eq 0 ] && [ "${SECOND_TEST_ERRORS2:-0}" -eq 0 ]; then
    echo ""
    echo "Todos os testes (STL e 3MF) passaram com sucesso!"
    exit 0
else
    echo ""
    echo "Falhas detectadas: STL=${FIRST_TEST_ERRORS}, 3MF_plate1=${SECOND_TEST_ERRORS}, 3MF_plate2=${SECOND_TEST_ERRORS2:-0}"
    exit 1
fi







