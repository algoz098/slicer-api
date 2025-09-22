# Overrides de Parâmetros (CLI, Addon e HTTP API)

Este documento descreve como enviar overrides de parâmetros de slicing e quais chaves são aceitas em todas as interfaces:
- CLI: orcaslicer-cli
- Addon Node (N-API): require('@orcaslicer/cli')
- HTTP API (node-api)

Resumo importante:
- Aceitamos TODAS as chaves de configuração reconhecidas pelo OrcaSlicer (libslic3r) na versão embutida neste repositório.
- As chaves são as mesmas que aparecem no INI exportado pelo OrcaSlicer GUI e em PrintConfig.cpp (DynamicPrintConfig).
- Também oferecemos aliases de compatibilidade para chaves comuns de outros slicers (tabela abaixo).
- Precedência: overrides (CLI --set / addon options / API options) > perfis explícitos (printer/filament/process) > configurações embutidas no 3MF > defaults.
- Erros: chave desconhecida ou valor inválido resulta em falha do CLI (exit code != 0), rejeição no Addon (throw) e HTTP 400 na API.


## Formatos de uso

- CLI
  ```bash
  ./orcaslicer-cli slice \
    --input path/to/model.stl \
    --output out.gcode \
    --printer "Bambu Lab X1 Carbon 0.4 nozzle" \
    --set "sparse_infill_density=30,layer_height=0.24,skirt_loops=1,infill_direction=45"
  ```
  Observações:
  - Passe múltiplos pares `k=v` separados por vírgula dentro de uma única opção `--set`.
  - Faça aspas quando o valor contiver espaços. Ex.: `--set "curr_bed_type=High Temp Plate"`.

- Addon Node (N-API)
  ```js
  const { slice } = require('OrcaSlicerCli/bindings/node')
  await slice({
    input: 'path/to/model.stl',
    output: 'out.gcode',
    printerProfile: 'Bambu Lab X1 Carbon 0.4 nozzle',
    filamentProfile: 'Bambu PLA Basic @BBL X1C',
    processProfile: '0.20mm Standard @BBL X1C',
    options: {
      sparse_infill_density: 30,
      layer_height: 0.24,
      skirt_loops: 1,
      infill_direction: 45
    }
  })
  ```

- HTTP API (node-api)
  ```json
  {
    "filePath": "path/to/model.stl",
    "printerProfile": "Bambu Lab X1 Carbon 0.4 nozzle",
    "filamentProfile": "Bambu PLA Basic @BBL X1C",
    "processProfile": "0.20mm Standard @BBL X1C",
    "options": {
      "sparse_infill_density": 30,
      "layer_height": 0.24,
      "skirt_loops": 1,
      "infill_direction": 45
    }
  }
  ```


## Tipos de valores

- Boolean: `true`/`false` (ou `1`/`0`).
- Percentual (coPercent): usar número com `%` (ex.: `30%`). Números sem `%` podem ser aceitos como atalho em alguns campos, porém prefira `%` quando a opção for percentual no Orca.
- Número (coInt / coFloat): números inteiros ou decimais (ponto como separador). Ex.: `0.24`.
- Enumerações (coEnum): valores aceitos são os rótulos/serializações usados pelo Orca. Ex.: `sparse_infill_pattern: "grid"`.
- Texto (coString / coStrings): usar strings. Para múltiplos valores, seguir a codificação esperada pelo Orca (por exemplo listas separadas por vírgula quando aplicável).

Dica: verifique o CONFIG_BLOCK dentro do G-code gerado; ele exibe a configuração efetiva e ajuda a confirmar a chave/valor aplicados.


## Chaves aceitas (escopo)

Aceitamos todas as chaves de configuração expostas por `DynamicPrintConfig` do OrcaSlicer (libslic3r). Exemplos representativos:

- Alturas e camadas: `layer_height`, `top_shell_layers`, `bottom_shell_layers`
- Paredes: `wall_loops`, `wall_sequence`, `outer_wall_line_width`, `inner_wall_line_width`
- Infill (sparse e sólido): `sparse_infill_density`, `sparse_infill_pattern`, `infill_direction`, `solid_infill_direction`, `internal_solid_infill_pattern`, `sparse_infill_line_width`, `internal_solid_infill_line_width`
- Primeira camada: `initial_layer_height`, `initial_layer_line_width`, `initial_layer_print_speed`
- Temperatura: `nozzle_temperature`, `bed_temperature`, `first_layer_temperature`, `first_layer_bed_temperature`
- Ventilação: `fan_speedup_time`, `overhang_fan_speed`, `reduce_fan_stop_start_freq`
- Materiais/extrusores: `wall_filament`, `sparse_infill_filament`, `solid_infill_filament`
- Suportes: `support_material`, `support_pattern`, `support_overhang_angle`
- Velocidades e acelerações: `default_speed`, `sparse_infill_speed`, `internal_solid_infill_speed`, `acceleration`, `jerk`
- Diversos: `brim_width`, `skirt_loops`, `seam_position`, `ironing_angle`, `ironing_pattern`

Observação: a lista completa é extensa (500+ opções) e depende da versão do OrcaSlicer incluída aqui. Consulte as fontes oficiais abaixo para a lista exata da sua versão.


## Fontes oficiais / Como descobrir todas as chaves

1) Exportar um INI pelo OrcaSlicer GUI (File → Export Config). As chaves exibidas são aceitas como overrides.
2) Conferir o código do OrcaSlicer neste repositório, arquivo:
   - `OrcaSlicer/src/libslic3r/PrintConfig.cpp` (busque por `this->add("…", …)`).
3) Inspecionar o CONFIG_BLOCK do G-code gerado — útil para validar a aplicação do override.

Essas fontes garantem sincronismo com a versão embutida, evitando divergência documental.


## Aliases de compatibilidade (mapeamentos)

Aceitamos os aliases abaixo e os mapeamos para chaves equivalentes do Orca:

- `perimeters` → `wall_loops`
- `top_solid_layers` → `top_shell_layers`
- `bottom_solid_layers` → `bottom_shell_layers`
- `skirts` → `skirt_loops`
- `infill_pattern` → `sparse_infill_pattern`
- `external_perimeters_first` → `wall_sequence` (mapeado para: `outer wall/inner wall` ou `inner wall/outer wall`)
- `fill_angle` → `infill_direction`
- `fan_always_on` → `reduce_fan_stop_start_freq`

Se um alias não estiver na tabela, use a chave nativa do OrcaSlicer (conforme INI/PrintConfig.cpp).


## Regras de validação e mensagens de erro

- Chaves desconhecidas ou incompatíveis com o preset/configuração atual resultarão em erro:
  - CLI: retorno com exit code != 0 e mensagem detalhada
  - Addon: Promise rejeitada com mensagem contendo a chave/valor problemáticos
  - HTTP API: status 400 Bad Request com mensagem “Invalid override option(s): …”
- Valores fora de faixa ou enums inválidos também falham.
- Quando aplicável, alguns valores serão normalizados internamente pelo Orca (ex.: limites, conversões), refletindo no CONFIG_BLOCK.


## Exemplos adicionais

- Percentuais e enums:
  ```bash
  --set "sparse_infill_density=15%,sparse_infill_pattern=grid"
  ```
- Ajustes de paredes e sobreposição:
  ```bash
  --set "wall_loops=3,infill_wall_overlap=15%"
  ```
- Ventilação orientada a overhang/bridges:
  ```bash
  --set "overhang_fan_speed=80"
  ```


## Dúvidas e contribuições

Se encontrar uma chave válida no INI/PrintConfig.cpp que não funcione como override, abra uma issue com:
- versão/commit do repositório,
- exemplo mínimo (CLI `--set` ou corpo JSON da API),
- trecho do CONFIG_BLOCK do G-code gerado.

Isso ajuda a manter a paridade total com o OrcaSlicer GUI.

