# @orcaslicer/cli — Addon N-API do OrcaSlicerCli

Bindings nativos para usar o motor do OrcaSlicer (libslic3r) diretamente em Node.js, sem sub‑processo. Este pacote expõe inicialização, inspeção de modelos e fatiamento (STL/3MF → G‑code).

## Requisitos

- Node.js >= 16
- Toolchain C/C++ e CMake (o build é nativo na máquina destino)
- cmake-js (instalado por este pacote quando usado localmente)

## Instalação e Build (neste repositório)

```bash
cd OrcaSlicerCli/bindings/node
npm install
npm run configure
npm run build
```

Para rodar os testes e validar slicing com os resources do repositório:

```bash
npm run -s slice:resources:quiet
```

## Uso (API)

Tipos completos em `types/index.d.ts`.

- initialize(opts?): `opts = { resourcesPath?: string, verbose?: boolean }`
- version(): `string`
- getModelInfo(file: string): `Promise<ModelInfo>`
- slice(params: SliceParams): `Promise<{ output: string }>`

Estrutura de `SliceParams` (resumo):

- `input: string` — STL/3MF de entrada
- `output?: string` — caminho do G‑code (auto se omitido)
- `plate?: number` — índice 1‑based (para 3MF com múltiplas plates)
- `printerProfile?`, `filamentProfile?`, `processProfile?`: nomes
- `custom?: Record<string,string>` — overrides de chaves de preset
- `verbose?: boolean`, `dryRun?: boolean`

### Exemplo mínimo

```js
const orca = require('@orcaslicer/cli');
// Se os resources do OrcaSlicer estiverem fora do padrão, informe:
// orca.initialize({ resourcesPath: '/caminho/para/OrcaSlicer/resources' });
orca.initialize();

(async () => {
  const info = await orca.getModelInfo('example_files/3DBenchy.stl');
  console.log(info);
  const { output } = await orca.slice({
    input: 'example_files/3DBenchy.stl',
    output: 'output_files/Benchy.gcode'
  });
  console.log('G-code em:', output);
})();
```

## Resources do OrcaSlicer

- Por padrão, o addon tenta localizar `OrcaSlicer/resources` relativo à raiz do projeto CLI.
- Para forçar um caminho, use:
  - `initialize({ resourcesPath: '/abs/path/OrcaSlicer/resources' })`, ou
  - variável de ambiente `ORCACLI_RESOURCES=/abs/path/OrcaSlicer/resources`.

## Controle de logs

- `ORCACLI_LOG_LEVEL`: 0=fatal, 1=error, 2=warning, 3=info, 4=debug, 5=trace (padrão recomendado para CI: 1)
- `ORCACLI_QUIET=1`: força nível `error`
- Scripts úteis neste pacote:
  - `npm run -s slice:resources:quiet`
  - `npm run -s slice:all:resources:quiet`

## Exportar/usar em outros repositórios

Você tem três caminhos. Recomendações em ordem de praticidade:

1) Publicar no npm (privado ou público)
- Requer ativar `private: false` e publicar com versionamento. Peça autorização antes de publicar.
- Vantagens: `npm install @orcaslicer/cli` direto; controle de versões.

2) Empacotar e consumir como tarball (.tgz)
- Gere o pacote a partir deste diretório:
  ```bash
  cd OrcaSlicerCli/bindings/node
  npm pack   # gera @orcaslicer/cli-<versao>.tgz
  ```
- No outro repositório, adicione em `package.json`:
  ```json
  {
    "dependencies": {
      "@orcaslicer/cli": "file:../caminho/para/@orcaslicer/cli-0.1.0.tgz"
    }
  }
  ```
- Após `npm install`, rode o build do addon (uma vez por máquina/ambiente):
  ```bash
  npx cmake-js build --directory node_modules/@orcaslicer/cli
  ```
  Dica: automatize com um script `postinstall` no seu projeto.

3) Submodule Git + dependência local
- Adicione este repositório como submódulo (ou subtree) no seu projeto, por exemplo em `vendor/orcaslicer-cli`.
- No `package.json` do seu projeto, referencie a pasta do addon:
  ```json
  {
    "dependencies": {
      "@orcaslicer/cli": "file:vendor/orcaslicer-cli/OrcaSlicerCli/bindings/node"
    }
  }
  ```
- Depois do `npm install`, faça o build do addon:
  ```bash
  npx cmake-js build --directory node_modules/@orcaslicer/cli
  ```

Notas importantes sobre distribuição
- Este é um addon nativo. O build ocorre na máquina de destino; portanto, garanta toolchain/CMake disponíveis no ambiente de CI/CD e dev.
- Para evitar compilar no consumidor, poderíamos adicionar pré‑builds binários (ex.: prebuildify). Isso exige pipeline de release para cada plataforma. Se houver interesse, podemos preparar isso.

## Testes locais rápidos

- Smoke/units/e2e:
  ```bash
  npm test
  ```
- Comparador de slicing com resources do repo (quiet):
  ```bash
  npm run -s slice:resources:quiet
  ```

## Troubleshooting

- "Failed to load printer profile": confirme `resourcesPath`/`ORCACLI_RESOURCES` apontando para `OrcaSlicer/resources` corretos.
- Logs excessivos: use `ORCACLI_LOG_LEVEL=1` ou `ORCACLI_QUIET=1`.
- Diferenças em 3MF: o teste já normaliza campos voláteis (ex.: `model label id`).


## Consumir pela raiz do projeto (import root)

Para que outro repositório possa simplesmente apontar para a PASTA RAIZ deste projeto e ter tudo que o OrcaSlicerCli precisa:

1) No outro repositório, adicione a dependência apontando para a raiz deste repo (via caminho local ou git):

```json
{
  "dependencies": {
    "@orcaslicer/cli": "file:../orcaslicer-addon" // caminho até a raiz deste projeto
  }
}
```

2) Instale normalmente. O postinstall do pacote raiz executará o build nativo no submódulo do addon:

```bash
npm install
```

3) Use normalmente no código do consumidor:

```js
const orca = require('@orcaslicer/cli');
orca.initialize({ resourcesPath: '/abs/path/OrcaSlicer/resources' }); // ou exporte ORCACLI_RESOURCES
```

Notas:
- O package.json na raiz reexporta o addon de `OrcaSlicerCli/bindings/node`, e possui um `postinstall` que chama `cmake-js` no subdiretório.
- Certifique-se de que a máquina de destino possui toolchain C/C++ e CMake.
- Se os resources do OrcaSlicer estiverem presentes dentro do pacote (como neste repositório), você pode referenciá-los com `resourcesPath` absoluto para `OrcaSlicer/resources` ou usar a variável `ORCACLI_RESOURCES`.

