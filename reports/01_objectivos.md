# Objetivos Essenciais
1. G-code indistinguível (sem dif semântico) do OrcaSlicer 2.3.0.
2. Reuso total do core C++ (sem reescrever algoritmos críticos).
3. API REST (gRPC opcional depois) + jobs assíncronos.
4. Escalabilidade via múltiplos processos headless.
5. Observabilidade (logs JSON, métricas, tracing) desde o início.
6. Portabilidade completa (Win/macOS/Linux + Docker).
7. Segurança mínima: validação entrada, limites recurso, isolamento processo.

## Indistinguível =
- Mesmos movimentos, extrusão, perímetros, retrações.
- Diferenças apenas em metadados voláteis.

## Fase 1 (Pesquisa): Saídas
- Lista componentes reutilizáveis.
- Estratégia build headless.
- Arquitetura alto nível validada.
- Roadmap inicial.
