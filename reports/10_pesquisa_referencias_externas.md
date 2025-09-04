# Pesquisa Externa: Referências & Abordagens

## Objetivo
Mapear como projetos relacionados implementam (a) slicing headless, (b) exposição via API / automação, (c) estrutura modular reutilizável. Servirá para guiar criação do nosso headless custom + API.

## Fontes Principais
- OrcaSlicer (fork BambuStudio/PrusaSlicer): https://github.com/SoftFever/OrcaSlicer (tag alvo v2.3.0)
- PrusaSlicer: https://github.com/prusa3d/PrusaSlicer
- SuperSlicer: https://github.com/supermerill/SuperSlicer
- CuraEngine: https://github.com/Ultimaker/CuraEngine
- OctoPrint (controle impressora, não slicing core): https://github.com/OctoPrint/OctoPrint
- Moonraker (API p/ Klipper firmware): https://github.com/Arksine/moonraker

## Modelos de Arquitetura Observados
| Projeto | Core Slicing | CLI Nativo | API Web | Observações |
|---------|--------------|-----------|---------|-------------|
| OrcaSlicer | Fork libslic3r + extensões | Parcial/GUI-focused | Não | Forte acoplamento UI; requer extração headless.
| PrusaSlicer | libslic3r | Sim (thin wrapper) | Não | CLI referência de wrapping mínimo sobre lib.
| SuperSlicer | libslic3r modificado | Sim | Não | Expõe funcionalidades extras mantendo padrão CLI.
| CuraEngine | Engine isolada | Sim (primária) | Não | Design como console app; fácil embed via subprocess.
| OctoPrint | Não (usa slicers externos/ plugins) | N/A | Sim (REST) | Usa wrappers para chamar slicers; plugin system.
| Moonraker | Não (API p/ firmware) | N/A | Sim (WebSocket + HTTP) | Modelo de streaming de estado e jobs (inspiração para status/progresso).

## Padrões Técnicos Relevantes
1. Separação de biblioteca (libslic3r) + CLI muito fina (Prusa/SuperSlicer) — ideal a replicar ao extrair entrypoint.
2. CuraEngine como engine independente: interface clara via parâmetros CLI — útil para definir formato de perfil no wrapper.
3. OctoPrint & Moonraker para: (a) estrutura de endpoints, (b) streaming (WebSocket) de progresso e eventos.
4. Uso de parâmetros estruturados via arquivos de configuração (INI/JSON) + overrides via linha de comando (PrusaSlicer) — relevante para nossa API mapear overrides específicos sem reconstruir perfil inteiro.

## Licenciamento (AGPL-3.0)
- Todos: OrcaSlicer, PrusaSlicer, SuperSlicer, CuraEngine → AGPL-3.0 ou GPL derivado.
- Implicação AGPL: uso via rede (SaaS) exige disponibilizar código fonte modificado do componente AGPL e de qualquer obra derivada integrada.
- Mitigação / Conformidade:
  - Publicar repositório com patches headless e instruções build.
  - Separar componentes que não derivam (ex: camada Go da API) mas ainda assim considerar publicar para transparência.
  - Manter cabeçalhos de licença intactos.

## Estratégias de Extração Baseadas em Observação de Prusa/SuperSlicer
1. Localizar wrapper CLI deles (ex: função `main` que instancia config, carrega modelo, aciona `slice()`).
2. Mapear fluxo mínimo de inicialização e replicar no nosso alvo headless removendo UI (wxWidgets / OpenGL / eventos).
3. Reaproveitar lógica de parsing de perfil (INI -> structs) para manter compatibilidade.
4. Introduzir camada de serialização JSON fina para entrada (API -> JSON -> INI temporário -> pipeline).

## Geração de Progresso (Moonraker / OctoPrint Inspiração)
- Progresso publicado por: layer_current / layer_total; tempo estimado restante; bytes de G-code gerados.
- Para headless: inserir hooks após geração de cada layer ou fase (perímetro, infill, suporte) acumulando métricas.
- Transport inicial: polling (status endpoint). Evolução: WebSocket.

## Lições sobre Determinismo
- libslic3r pretende ser determinístico, mas ordens de iteração podem divergir se estruturas não ordenadas forem tocadas — manter mesmo compilador/flags para baseline inicial.
- CuraEngine orienta parâmetros explícitos; nossa abordagem: normalizar parâmetros antes de invocar (ordenação consistente de keys em arquivo gerado temporário).

## Riscos Identificados via Comparação
| Risco | Fonte Observada | Mitigação Proposta |
|-------|-----------------|--------------------|
| Atraso extração headless | Acoplamento UI (Orca) | Reusar padrão CLI simples de PrusaSlicer como blueprint |
| Divergência perfis | Variação forks (Orca vs Prusa) | Mapear dif de chaves/config e documentar compat layer |
| Conformidade licença | AGPL rede | Repositório público + publicar diffs patches |
| Falta de progresso em tempo real | CLI puro | Hooks + canal status incremental |
| Overhead spawn processo | Subprocess para cada job | Medir e considerar pool/FFI |

## Itens para Investigação Futura
- Localizar exatamente onde `libslic3r` é invocada no wrapper CLI de PrusaSlicer para replicar.
- Enumerar parâmetros CLI relevantes que precisamos suportar inicialmente.
- Ver se existe código de teste comparando G-code upstream que possamos adaptar (pasta `tests/`).
- Avaliar se processar 3MF diretamente é necessário logo ou iniciar apenas com STL.
- Ver formato interno de perfis Orca vs Prusa para garantir equivalência (renome de chaves, ranges).

## Ações Recomendadas Próximas
1. Extrair `main.cpp` do CLI de Prusa/SuperSlicer como referência (anotar chamadas essenciais).
2. Listar dependências condicionais de build que podem ser desabilitadas (GUI, OpenGL, network extras).
3. Criar stub `headless_main.cpp` com pipeline mínimo e compilar contra fonte do Orca.
4. Prototipar coleta de progresso (contador de layers) — log simples JSON por stderr.
5. Iniciar documento de compat de parâmetros (map Orca -> exposição API).

## Referências Wiki / Docs Úteis
- PrusaSlicer CLI: https://github.com/prusa3d/PrusaSlicer/wiki/Command-Line-Interface
- SuperSlicer build docs: `doc/How to build - <platform>.md`
- CuraEngine Internals: https://github.com/Ultimaker/CuraEngine/wiki/Internals
- OrcaSlicer Build: https://github.com/SoftFever/OrcaSlicer/wiki/How-to-build
- Moonraker Docs: https://moonraker.readthedocs.io/en/latest/
- OctoPrint Plugin system: https://docs.octoprint.org/

## Conclusão
Nenhum projeto oferece diretamente uma API web de slicing full equivalence pronta; padrão consolidado é biblioteca + CLI. Nossa vantagem: construir headless custom seguindo padrão mínimo (Prusa) e depois encapsular em serviço (modelo inspirado em Moonraker/OctoPrint para flow de jobs/progresso). Licenciamento AGPL reforça necessidade de manter patches públicos e aderir à reciprocidade.
