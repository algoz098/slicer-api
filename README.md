# Slicer API

API para expor funcionalidades equivalentes ao OrcaSlicer 2.3.0, visando produzir G-code indistinguível do oficial.

- MVP atual: Node.js (Feathers + Koa)
- Core do OrcaSlicer: submódulo Git fixado na tag `v2.3.0`
- Testes automatizados cobrem upload/extração de `.3mf` e contagem de plates

Consulte a pasta `reports/` para relatórios incrementais. Versões antigas de documentos foram movidas para `reports/lixo`.

## Requisitos
- Node.js 22 LTS (>= 22.0.0)
- Git com suporte a submódulos

## Setup do repositório
1. Inicialize o submódulo do OrcaSlicer (tag v2.3.0):
   ```bash
   git submodule update --init --recursive
   (cd source_OrcaSlicer && git checkout v2.3.0)
   ```
2. Instale e valide o Node API:
   ```bash
   cd node-api
   npm install
   npm run compile
   npm test
   ```

## Executando a API
- Desenvolvimento (hot reload):
  ```bash
  cd node-api
  npm run dev
  ```
- Produção (após compilar):
  ```bash
  cd node-api
  npm run compile
  npm start
  ```

A aplicação expõe por padrão:
- GET `/` → JSON com name/version/status
- POST `/api/plates/count` → multipart (campo `file`) com `.3mf`, `.stl` ou `.obj`; retorna `{ count, fileName, ... }`
- POST `/api/files/info` → multipart (campo `file`) com `.3mf`; retorna metadados extraídos + `plateCount`

## Estrutura e perfis
- O repositório do OrcaSlicer está em `source_OrcaSlicer` (submódulo, tag `v2.3.0`).
- Perfis do slicer são resolvidos a partir de locais padrão, incluindo:
  - `node-api/config/orcaslicer/profiles/resources/profiles`
  - `source_OrcaSlicer/resources/profiles`
- Para dados persistentes próprios da API (ex.: perfis locais), use `data/profiles` na raiz. Em Docker, mapeie `./data` como volume.

## Scripts úteis (node-api)
- `npm run compile` → compila TypeScript para `lib/`
- `npm test` → executa a suíte de testes (Mocha + ts-node)
- `npm run dev` → desenvolvimento com nodemon/ts-node
- `npm run lint` / `npm run lint:fix` → lint TypeScript (ESLint v9)
- `npm run prettier` → formata arquivos suportados

## Notas
- CI e Docker ainda não estão habilitados neste repositório.
- Se atualizar o submódulo, não esqueça de versionar `.gitmodules` e o ponteiro do submódulo.
