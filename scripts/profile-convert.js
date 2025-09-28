#!/usr/bin/env node
/*
  Envia um arquivo de perfil (filament/printer/process) para o serviço /profile-converter
  e imprime o objeto `options` resultante (overrides compatíveis com slicer/3mf).

  Ajuste as constantes abaixo conforme necessário e rode:
    node scripts/profile-convert.js

  Observações:
  - Modo principal: multipart/form-data (envia o arquivo de fato)
  - Fallback: JSON (envia o caminho do arquivo em `data`), caso multipart falhe
*/

const fs = require('node:fs');
const fsp = require('node:fs/promises');
const path = require('node:path');

// ===== Configuração através de variáveis globais (sem parâmetros de linha de comando) =====
// Exemplos de arquivos do repositório:
//   Filament: ./example_files/Filament presets/Voolt PLA.json
//   Printer:  ./example_files/priter_profiles/K1 Max.orca_printer
//   Process:  (se existir)
const FILE = './example_files/Filament presets/Voolt PLA.json';
const TYPE = 'filament'; // 'filament' | 'printer' | 'process'
const URL = 'http://localhost:3030/profile-converter';

// Opcional: salvar o resultado também em arquivo
const OUT_JSON = './output_files/profile_options.json';

async function ensureOutDir(filePath) {
  try {
    await fsp.mkdir(path.dirname(path.resolve(filePath)), { recursive: true });
  } catch {}
}

async function main() {
  const abs = path.resolve(FILE);

  try {
    await fsp.access(abs, fs.constants.R_OK);
  } catch {
    console.error(`Input file not found or not readable: ${abs}`);
    console.error('Atualize a constante FILE no scripts/profile-convert.js');
    process.exit(1);
  }

  if (!['filament', 'printer', 'process'].includes(TYPE)) {
    console.error(`TYPE inválido: ${TYPE}. Use 'filament' | 'printer' | 'process'.`);
    process.exit(1);
  }

  console.log(`POST multipart para ${URL} ...`);

  // ========== Modo principal: multipart ==========
  const buf = await fsp.readFile(abs);
  const blob = new Blob([buf]);
  const form = new FormData();
  form.append('file', blob, path.basename(abs));
  form.append('type', TYPE);

  let res = await fetch(URL, { method: 'POST', body: form });
  let bodyText = await res.text();

  // ========== Fallback: JSON (caminho do arquivo) ==========
  if (!res.ok) {
    console.warn(`Multipart falhou (HTTP ${res.status}). Fazendo fallback para JSON { type, data: filePath } ...`);
    const payload = { type: TYPE, data: abs };
    res = await fetch(URL, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify(payload)
    });
    bodyText = await res.text();

    if (!res.ok) {
      console.error(`Request failed. HTTP ${res.status}\nResponse: ${bodyText}`);
      process.exit(1);
    }
  }

  let json;
  try {
    json = JSON.parse(bodyText);
  } catch (e) {
    console.error('Falha ao parsear JSON de resposta:', e);
    console.error('Resposta bruta:', bodyText);
    process.exit(1);
  }

  if (!json || typeof json.options !== 'object') {
    console.error('Resposta não contém `options`. Resposta completa:');
    console.error(JSON.stringify(json, null, 2));
    process.exit(1);
  }

  const options = json.options || {};
  const keys = Object.keys(options);
  console.log(`\n===== options (${keys.length} chaves) =====`);
  console.log(JSON.stringify(options, null, 2));

  if (OUT_JSON) {
    await ensureOutDir(OUT_JSON);
    await fsp.writeFile(path.resolve(OUT_JSON), JSON.stringify(options, null, 2), 'utf8');
    console.log(`\nSalvo em: ${path.resolve(OUT_JSON)}`);
  }
}

main().catch((err) => {
  console.error(err?.stack || err?.message || String(err));
  process.exit(1);
});

