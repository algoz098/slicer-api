#!/usr/bin/env bash
set -euo pipefail

# Script automático para preparar o prebuild Linux do addon Node e gerar o pacote .tgz (sem publicar)
# 1) Descobre a versão em OrcaSlicerAddon/bindings/node/package.json
# 2) Baixa o artifact "prebuild-linux-<versão>" do workflow node-addon-prebuilds-amd64.yml (via GitHub CLI)
# 3) Copia o conteúdo para OrcaSlicerAddon/bindings/node/prebuilds
# 4) Gera o tarball do npm (npm pack) em ./dist
#
# Pré-requisitos:
# - Node.js instalado e disponível em PATH
# - GitHub CLI (gh) instalado e autenticado neste repositório (`gh auth status`)

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/../.." && pwd)
ADDON_DIR="${REPO_ROOT}/OrcaSlicerAddon/bindings/node"
ADDON_PKG="${ADDON_DIR}/package.json"
DEST_PREBUILDS_DIR="${ADDON_DIR}/prebuilds"
OUTPUT_DIR="${REPO_ROOT}/dist"
WORKFLOW_FILE="node-addon-prebuilds-amd64.yml"

bold() { printf "\033[1m%s\033[0m\n" "$*"; }
info() { printf "[info] %s\n" "$*"; }
err()  { printf "[error] %s\n" "$*"; }

# Checagens básicas
if [[ ! -d "$ADDON_DIR" ]]; then
  err "Diretório do addon não encontrado: $ADDON_DIR"
  exit 1
fi
if ! command -v node >/dev/null 2>&1 ; then
  err "Node.js é obrigatório (não encontrado em PATH)"
  exit 1
fi
if ! command -v gh >/dev/null 2>&1 ; then
  err "GitHub CLI (gh) é obrigatório para baixar o artifact. Instale e autentique-se (gh auth status)."
  exit 1
fi

# Descobre a versão do addon
VER=$(node -e 'const fs=require("fs"); const p=process.argv[1]; const j=JSON.parse(fs.readFileSync(p, "utf8")); if(!j.version){process.exit(2);} console.log(j.version);' "$ADDON_PKG")
ARTIFACT_NAME="prebuild-linux-${VER}"

bold "Preparando prebuild Linux para a versão ${VER}"
info "Raiz do repositório: $REPO_ROOT"
info "Addon dir: $ADDON_DIR"

# Baixa o artifact do GitHub Actions
TMP_DIR="${REPO_ROOT}/.tmp-prebuild-linux-${VER}"
rm -rf "$TMP_DIR" && mkdir -p "$TMP_DIR"
(
  cd "$REPO_ROOT"
  set +e
  # Caminho rápido: baixar direto por ID se PREBUILD_ARTIFACT_ID estiver definido
  if [[ -n "${PREBUILD_ARTIFACT_ID:-}" ]]; then
    NAME_WITH_OWNER=$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null)
    ART_ID="$PREBUILD_ARTIFACT_ID"
    echo "[info] Usando PREBUILD_ARTIFACT_ID=${ART_ID} para baixar ZIP..."
    DL_URL=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts/$ART_ID" --jq .archive_download_url 2>/dev/null)
    if [[ -n "$DL_URL" && "$DL_URL" != "null" ]]; then
      curl -fsSL -H "Authorization: token $(gh auth token)" -H "Accept: application/vnd.github+json" \
        "$DL_URL" -o "$TMP_DIR/artifact.zip"
    fi
    if [[ -s "$TMP_DIR/artifact.zip" ]]; then
      if command -v unzip >/dev/null 2>&1; then
        unzip -oq "$TMP_DIR/artifact.zip" -d "$TMP_DIR"; EX=$?
      elif command -v ditto >/dev/null 2>&1; then
        ditto -x -k "$TMP_DIR/artifact.zip" "$TMP_DIR"; EX=$?
      else
        python3 - <<'PY' "$TMP_DIR/artifact.zip" "$TMP_DIR"
import sys, zipfile
z=zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2])
PY

  # Tentativa adicional: baixar todos os artifacts do run (sem filtrar por nome)
  if [[ ${RC:-1} -ne 0 && -n "${RUN_ID:-}" && "$RUN_ID" != "null" ]]; then
    echo "[info] Tentando baixar todos os artifacts do run $RUN_ID..."
    gh run download "$RUN_ID" --dir "$TMP_DIR"
    if [[ $? -eq 0 ]]; then
      ARTIFACT_USED_NAME="all-from-run"; RC=0
    fi
  fi

        EX=$?
      fi
      if [[ $EX -eq 0 ]]; then ARTIFACT_USED_NAME="artifact-${ART_ID}"; RC=0; fi
    fi
  fi

  # Permite override opcional via PREBUILD_RUN_ID (por ex., ao colar um link de run conhecido)
  if [[ -n "${PREBUILD_RUN_ID:-}" ]]; then
    RUN_ID="$PREBUILD_RUN_ID"
  else
    RUN_ID=$(gh run list --workflow "$WORKFLOW_FILE" --status success --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
    if [[ -z "$RUN_ID" || "$RUN_ID" == "null" ]]; then
      RUN_ID=$(gh run list --workflow "$WORKFLOW_FILE" --limit 1 --json databaseId --jq '.[0].databaseId' 2>/dev/null || true)
    fi
  fi

  CAND_NAMES=("prebuild-linux-${VER}" "prebuild-linux")
  ARTIFACT_USED_NAME=""
  RC=1

  if [[ -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
    for NAME in "${CAND_NAMES[@]}"; do
      gh run download "$RUN_ID" --name "$NAME" --dir "$TMP_DIR"
      if [[ $? -eq 0 ]]; then ARTIFACT_USED_NAME="$NAME"; RC=0; break; fi
    done
  fi

  if [[ ${RC:-1} -ne 0 ]]; then
    NAME_WITH_OWNER=$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null)

    # 2) Procura repo-wide pelo artifact mais recente cujo nome contenha 'prebuild-linux'
    if [[ -n "$NAME_WITH_OWNER" ]]; then
      ART_ID=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts?per_page=100" \
        --jq '[.artifacts[] | select(.expired==false and (.name | test("prebuild-linux")))] | sort_by(.created_at) | reverse | .[0].id' 2>/dev/null)
      ART_NAME=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts?per_page=100" \
        --jq '[.artifacts[] | select(.expired==false and (.name | test("prebuild-linux")))] | sort_by(.created_at) | reverse | .[0].name' 2>/dev/null)
      if [[ -n "$ART_ID" && "$ART_ID" != "null" ]]; then
        echo "[info] Encontrado artifact repo-wide: name=${ART_NAME} id=${ART_ID}. Baixando ZIP..."
        DL_URL=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts/$ART_ID" --jq .archive_download_url 2>/dev/null)
        if [[ -n "$DL_URL" && "$DL_URL" != "null" ]]; then
          curl -fsSL -H "Authorization: token $(gh auth token)" -H "Accept: application/vnd.github+json" \
            "$DL_URL" -o "$TMP_DIR/artifact.zip"
        fi
        if [[ -s "$TMP_DIR/artifact.zip" ]]; then
          if command -v unzip >/dev/null 2>&1; then
            unzip -oq "$TMP_DIR/artifact.zip" -d "$TMP_DIR"; EX=$?
          elif command -v ditto >/dev/null 2>&1; then
            ditto -x -k "$TMP_DIR/artifact.zip" "$TMP_DIR"; EX=$?
          else
            python3 - <<'PY' "$TMP_DIR/artifact.zip" "$TMP_DIR"
import sys, zipfile
z=zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2])
PY
            EX=$?
          fi
          if [[ $EX -eq 0 ]]; then ARTIFACT_USED_NAME="$ART_NAME"; RC=0; fi
        fi
      fi
    fi

    # 3) Se ainda falhar, tenta localizar artifact deste run (por nome contendo 'prebuild-linux')
    if [[ ${RC:-1} -ne 0 && -n "$NAME_WITH_OWNER" && -n "$RUN_ID" && "$RUN_ID" != "null" ]]; then
      ART_ID=$(gh api "/repos/$NAME_WITH_OWNER/actions/runs/$RUN_ID/artifacts?per_page=100" \
        --jq '[.artifacts[] | select(.expired==false and (.name | test("prebuild-linux")))] | sort_by(.created_at) | reverse | .[0].id' 2>/dev/null)
      ART_NAME=$(gh api "/repos/$NAME_WITH_OWNER/actions/runs/$RUN_ID/artifacts?per_page=100" \
        --jq '[.artifacts[] | select(.expired==false and (.name | test("prebuild-linux")))] | sort_by(.created_at) | reverse | .[0].name' 2>/dev/null)
      if [[ -n "$ART_ID" && "$ART_ID" != "null" ]]; then
        echo "[info] Encontrado artifact no run: name=${ART_NAME} id=${ART_ID}. Baixando ZIP..."
        DL_URL=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts/$ART_ID" --jq .archive_download_url 2>/dev/null)
        if [[ -n "$DL_URL" && "$DL_URL" != "null" ]]; then
          curl -fsSL -H "Authorization: token $(gh auth token)" -H "Accept: application/vnd.github+json" \
            "$DL_URL" -o "$TMP_DIR/artifact.zip"
        fi
        if [[ -s "$TMP_DIR/artifact.zip" ]]; then
          if command -v unzip >/dev/null 2>&1; then
            unzip -oq "$TMP_DIR/artifact.zip" -d "$TMP_DIR"; EX=$?
          elif command -v ditto >/dev/null 2>&1; then
            ditto -x -k "$TMP_DIR/artifact.zip" "$TMP_DIR"; EX=$?
          else
            python3 - <<'PY' "$TMP_DIR/artifact.zip" "$TMP_DIR"
import sys, zipfile
z=zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2])
PY
            EX=$?
          fi
          if [[ $EX -eq 0 ]]; then ARTIFACT_USED_NAME="$ART_NAME"; RC=0; fi
        fi
      fi
    fi

    # 4) Se ainda falhar, tenta repo-wide com os nomes candidatos exatos (compat)
    if [[ ${RC:-1} -ne 0 && -n "$NAME_WITH_OWNER" ]]; then
      echo "[info] Artifact não encontrado. Buscando no repositório por nomes candidatos exatos..."
      for NAME in "${CAND_NAMES[@]}"; do
        ART_ID=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts?per_page=100" \
          --jq "[.artifacts[] | select(.name=='${NAME}' and .expired==false)] | sort_by(.created_at) | reverse | .[0].id" 2>/dev/null)
        if [[ -n "$ART_ID" && "$ART_ID" != "null" ]]; then
          echo "[info] Encontrado artifact repo-wide: name=${NAME} id=$ART_ID. Baixando ZIP..."
          DL_URL=$(gh api "/repos/$NAME_WITH_OWNER/actions/artifacts/$ART_ID" --jq .archive_download_url 2>/dev/null)
          if [[ -n "$DL_URL" && "$DL_URL" != "null" ]]; then
            curl -fsSL -H "Authorization: token $(gh auth token)" -H "Accept: application/vnd.github+json" \
              "$DL_URL" -o "$TMP_DIR/artifact.zip"
          fi
          if [[ -s "$TMP_DIR/artifact.zip" ]]; then
            if command -v unzip >/dev/null 2>&1; then
              unzip -oq "$TMP_DIR/artifact.zip" -d "$TMP_DIR"; EX=$?
            elif command -v ditto >/dev/null 2>&1; then
              ditto -x -k "$TMP_DIR/artifact.zip" "$TMP_DIR"; EX=$?
            else
              python3 - <<'PY' "$TMP_DIR/artifact.zip" "$TMP_DIR"
import sys, zipfile
z=zipfile.ZipFile(sys.argv[1]); z.extractall(sys.argv[2])
PY
              EX=$?
            fi
            if [[ $EX -eq 0 ]]; then ARTIFACT_USED_NAME="$NAME"; RC=0; break; fi
          fi
        fi
      done
    fi
  fi
  set -e

  if [[ ${RC:-1} -ne 0 ]]; then
    echo "[error] Não foi possível baixar nenhum artifact (tentados: ${CAND_NAMES[*]}). Execute o workflow e tente novamente." >&2
    exit 1
  fi
)

# Localiza a pasta 'prebuilds' dentro do artifact
SRC_PREBUILDS=""
if [[ -d "${TMP_DIR}/prebuilds" ]]; then
  SRC_PREBUILDS="${TMP_DIR}/prebuilds"
else
  SRC_PREBUILDS=$(find "$TMP_DIR" -maxdepth 3 -type d -name prebuilds -print -quit)
fi

if [[ -z "$SRC_PREBUILDS" ]]; then
  # Estrutura alternativa: alguns artifacts trazem linux-x64 direto na raiz (sem pasta 'prebuilds')
  if [[ -d "${TMP_DIR}/linux-x64" ]]; then
    info "Estrutura alternativa detectada ('linux-x64' na raiz). Copiando para $DEST_PREBUILDS_DIR..."
    rm -rf "$DEST_PREBUILDS_DIR"
    mkdir -p "$DEST_PREBUILDS_DIR"
    cp -R "${TMP_DIR}/linux-x64" "$DEST_PREBUILDS_DIR"/
  else
    err "Não foi possível encontrar a pasta 'prebuilds' no artifact baixado (${ARTIFACT_USED_NAME:-desconhecido})."
    exit 1
  fi
else
  bold "Copiando prebuilds de: $SRC_PREBUILDS"
  rm -rf "$DEST_PREBUILDS_DIR"
  mkdir -p "$DEST_PREBUILDS_DIR"
  cp -R "$SRC_PREBUILDS"/* "$DEST_PREBUILDS_DIR"/
fi

# Valida presença do binário esperado
EXPECTED_FILE="${DEST_PREBUILDS_DIR}/linux-x64/orcaslicer_node.node"
if [[ ! -f "$EXPECTED_FILE" ]]; then
  err "Arquivo esperado não encontrado: ${EXPECTED_FILE}. Verifique o conteúdo do artifact."
  exit 1
fi
info "Prebuild Linux posicionado em: $DEST_PREBUILDS_DIR"

# Empacota (sem publicar)
bold "Gerando tarball com npm pack (sem publicar)"
mkdir -p "$OUTPUT_DIR"
(
  cd "$ADDON_DIR"
  npm pack --pack-destination "$OUTPUT_DIR"
)

# Mostra resultado
TGZ=$(ls -1 "${OUTPUT_DIR}"/orcaslicer-addon-*.tgz 2>/dev/null | tail -n1 || true)
if [[ -z "${TGZ}" ]]; then
  err "npm pack não gerou tarball em ${OUTPUT_DIR}"
  exit 1
fi
bold "Tarball gerado: ${TGZ}"

# Checagem rápida: prebuild Linux incluso no tarball
if command -v tar >/dev/null 2>&1; then
  if tar -tzf "$TGZ" | grep -q "prebuilds/linux-x64/orcaslicer_node.node" ; then
    info "Verificado: prebuild Linux incluído no tarball."
  else
    err "Prebuild Linux não encontrado dentro do tarball."
    exit 1
  fi
fi

bold "Concluído."

