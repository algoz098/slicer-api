# Pesquisa de Fontes e Referências

## Repositório OrcaSlicer
- URL: https://github.com/SoftFever/OrcaSlicer (confirmar tag 2.3.0)
- Fork + tag espelhada.

## Áreas para Mapear
1. Núcleo de fatiamento (derivado do PrusaSlicer / SuperSlicer?).
2. Parsing de formatos: STL, 3MF, AMF, OBJ.
3. Geração de suportes, infill, perímetros, otimização de trajetórias.
4. Pós-processamento: scripts, substituição de placeholders, cabeçalhos.
5. Perfis (JSON/ini) e parâmetros.
6. Motor de preview (não necessário fase 1).

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
