# Resumo Consolidado

Objetivo Macro: API que gera G-code indistinguível do OrcaSlicer 2.3.0.

## Pilares
- Reuso máximo do core C++ (sem reimplementação de algoritmos complexos).
- Headless custom (não existe CLI funcional nativo).
- Arquitetura: API -> Fila -> Workers (processos headless) -> Storage/Cache.
- Portável (Win/macOS/Linux) e empacotado em Docker.
- Observabilidade desde o início (logs JSON, métricas, tracing).

## Decisões Atuais
- MVP da API em Node.js (Feathers + Koa), com upload e extração de informações de .3mf e contagem de plates funcionando com testes automatizados.
- Redis Streams + FS local (planejado para fases futuras, não habilitado agora).
- Normalizador/Comparador de G-code em Python (planejado para CI/Testes).
- Patch minimal sobre tag 2.3.0 do OrcaSlicer; isolamento UI via macros HEADLESS (com o código fonte como submódulo).

## Critérios de Equivalência
- Sem dif semântico (movimentos, extrusão, perímetros, retrações).
- Dif byte-a-byte só em metadados voláteis (timestamps, caminhos).

## Métricas Gate
| Métrica | Alvo |
|---------|------|
| Overhead spawn | <5% tempo slice |
| Dif extrusão total | 0 |
| Dif tempo estimado | <=0.5% |
| Cache hit (após tuning) | >50% |

## Roadmap (Simplificado)
M0: Pesquisa + build headless mínimo.
M1: CLI headless + testes equivalência.
M2: API MVP + fila in-memory/Redis + download.
M3: Observabilidade + cache + Redis Streams prod.
M4: Otimização / FFI avaliação / gRPC opcional.

## Riscos Principais
- Acoplamento UI-core → Mitigar com façade.
- Divergência futura upstream → Fixar tag + patch pequeno.
- Overhead processo → Avaliar FFI só se métrica exceder alvo.

## Próximos Passos Imediatos
1. Inicializar git + adicionar submódulo OrcaSlicer 2.3.0.
2. Mapear entrypoint (função slice na UI) e criar alvo CMake headless.
3. Gerar baseline G-code (GUI) para 3 modelos.
4. Implementar normalizador & dif semântico.
5. Comparar outputs (assert 0 diferenças semânticas).
