#!/usr/bin/env bash
set -euo pipefail

# Script para testar o pacote orcaslicer-addon localmente (sem npm registry)
# 1) Cria um diretório temporário de teste
# 2) Instala o tarball gerado (.tgz) via npm install <path>
# 3) Executa um script Node.js que importa o pacote e valida a API
# 4) Limpa o diretório temporário

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)
DIST_DIR="${REPO_ROOT}/dist"
TGZ_FILE="${DIST_DIR}/orcaslicer-addon-0.1.0.tgz"

bold() { printf "\033[1m%s\033[0m\n" "$*"; }
info() { printf "[info] %s\n" "$*"; }
err()  { printf "[error] %s\n" "$*"; }

# Checagens básicas
if [[ ! -f "$TGZ_FILE" ]]; then
  err "Tarball não encontrado: $TGZ_FILE"
  err "Execute primeiro: ./scripts/ci/prepare_linux_prebuild.sh"
  exit 1
fi

bold "Testando pacote local: $TGZ_FILE"

# Cria diretório temporário de teste
TEST_DIR=$(mktemp -d -t orcaslicer-test-XXXXXX)
info "Diretório de teste: $TEST_DIR"

cleanup() {
  info "Limpando diretório de teste..."
  rm -rf "$TEST_DIR"
}
trap cleanup EXIT

# Cria package.json mínimo no diretório de teste
cat > "$TEST_DIR/package.json" <<'EOF'
{
  "name": "orcaslicer-test",
  "version": "1.0.0",
  "private": true,
  "type": "commonjs"
}
EOF

# Instala o tarball localmente
info "Instalando tarball via npm install..."
(
  cd "$TEST_DIR"
  npm install "$TGZ_FILE" --loglevel=error
)

# Cria script de teste que importa o pacote
cat > "$TEST_DIR/test.js" <<'EOF'
"use strict";

console.log("=== Teste de importação do pacote orcaslicer-addon ===\n");

try {
  const orcaslicer = require("orcaslicer-addon");
  
  console.log("✓ Pacote importado com sucesso");
  console.log("✓ Tipo:", typeof orcaslicer);
  
  // Lista propriedades/métodos exportados
  const props = Object.getOwnPropertyNames(orcaslicer);
  console.log("✓ Propriedades/métodos exportados:", props.length);
  
  if (props.length > 0) {
    console.log("\nAPI disponível:");
    props.forEach(p => {
      const type = typeof orcaslicer[p];
      console.log(`  - ${p}: ${type}`);
    });
  }
  
  // Tenta chamar algum método se existir (exemplo: slice, configure, etc.)
  if (typeof orcaslicer.slice === "function") {
    console.log("\n✓ Método 'slice' encontrado (função)");
  }
  
  if (typeof orcaslicer.configure === "function") {
    console.log("✓ Método 'configure' encontrado (função)");
  }
  
  console.log("\n=== Teste concluído com sucesso ===");
  process.exit(0);
  
} catch (err) {
  console.error("✗ Erro ao importar ou usar o pacote:");
  console.error(err.message);
  console.error(err.stack);
  process.exit(1);
}
EOF

# Executa o teste
bold "Executando teste de importação..."
(
  cd "$TEST_DIR"
  node test.js
)

bold "Teste local concluído com sucesso!"

