# Pesquisa de Fontes e Referências

## Repositório OrcaSlicer
- URL: https://github.com/SoftFever/OrcaSlicer (confirmar tag 2.3.0)
- Fork + tag espelhada.

## Perfis de Impressora Disponíveis no Slicer-API

O projeto slicer-api utiliza os mesmos perfis de impressora do OrcaSlicer, carregados da pasta `go-api/data/`. Os perfis disponíveis atualmente são idênticos aos listados acima:

### Bambu Lab (BBL)
- Bambu Lab X1 Carbon
- Bambu Lab X1
- Bambu Lab X1E
- Bambu Lab P1P
- Bambu Lab P1S
- Bambu Lab A1 mini
- Bambu Lab A1

### Creality
- Creality CR-10 V2
- Creality CR-10 V3
- Creality CR-10 Max
- Creality CR-10 SE
- Creality CR-6 SE
- Creality CR-6 Max
- Creality CR-M4
- Creality Ender-3 V2
- Creality Ender-3 V2 Neo
- Creality Ender-3 S1
- Creality Ender-3
- Creality Ender-3 Pro
- Creality Ender-3 S1 Pro
- Creality Ender-3 S1 Plus
- Creality Ender-3 V3 SE
- Creality Ender-3 V3 KE
- Creality Ender-3 V3
- Creality Ender-3 V3 Plus
- Creality Ender-5
- Creality Ender-5 Max
- Creality Ender-5 Plus
- Creality Ender-5 Pro (2019)
- Creality Ender-5S
- Creality Ender-5 S1
- Creality Ender-6
- Creality Sermoon V1
- Creality K1
- Creality K1C
- Creality K1 Max
- Creality K1 SE
- Creality K2 Plus
- Creality Hi

### Prusa
- Prusa CORE One
- Prusa CORE One HF
- MK4IS
- MK4S
- MK4S HF
- Prusa XL
- Prusa XL 5T
- MK3.5
- MK3S
- MINI
- MINIIS

**Nota:** Não foram encontrados perfis genéricos de impressora no OrcaSlicer. Os perfis são específicos para os vendors acima. Para filamentos e processos, existem perfis genéricos como "Generic PLA" e "Generic ABS".

Esses perfis estão definidos nos arquivos `BBL.json`, `Creality.json` e `Prusa.json` na pasta `resources/profiles/` do repositório. Cada perfil inclui configurações de máquina, processos e filamentos compatíveis.

## Ferramentas de Análise
- `cloc` para métricas de tamanho.
- `grep`/`ripgrep` para localizar funções como `slice()`, `gcode`, `Toolpath`.
- `Doxygen` (opcional) para gerar visão.

## Referências Externas
- PrusaSlicer docs: arquitetura de slicing.
- OpenSCAD libs (geometria) se pertinentes.
- OpenTelemetry specs.
- Padrões REST/gRPC para jobs assíncronos.

## Riscos / Desafios
- Divergência futura de versões do OrcaSlicer.
- Complexidade de perfis proprietários específicos.
- Performance sob alta concorrência (CPU bound).
- Precisão numérica / diferenças sutis no G-code (ordem de floating points).

## Mitigações Iniciais
- Test harness automatizado comparando G-code com baseline.
- Hash de parâmetros + versionamento interno do core.
- Limitar MVP a subset de parâmetros mais usados.

## Tarefas de Pesquisa (Backlog)
- [ ] Confirmar licenciamento e obrigações (GPL? manter compatibilidade, publicação de alterações).
- [ ] Identificar ponto de entrada principal de slicing.
- [ ] Listar dependências de build (CMake, libs, versões do comp). 
- [ ] Provar build headless minimal.
- [ ] Definir formato de perfil normalizado (entrada API -> mapa para perfis do core).
- [ ] Especificar comparador de G-code (normalização + diff semântico).
- [ ] Avaliar custo de cold start do processo de slicing.
- [ ] Decidir linguagem do serviço API (comparar Go vs Rust vs Python protótipo).
- [ ] Mapear parâmetros críticos para gerar G-code idêntico.
- [ ] Planejar pipeline CI (build core + testes comparação).
