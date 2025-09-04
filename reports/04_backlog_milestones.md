# Backlog & Milestones (Versão 0.1)

## Milestone M0 - Pesquisa & Setup (Semanas 1-2)
- [ ] Clonar OrcaSlicer 2.3.0 (submódulo ou fork).
- [ ] Confirmar licença e implicações (GPLv3?).
- [ ] Script de build reprodutível (Dockerfile base).
- [ ] Documentar ponto de entrada de slicing.
- [ ] Prova de conceito: gerar G-code via UI original para baseline.

## Milestone M1 - Headless CLI (Semanas 3-4)
- [ ] Extrair/ativar modo headless.
- [ ] Implementar CLI wrapper simples.
- [ ] Test harness comparando G-code.
- [ ] Normalizador de G-code (remove timestamps, paths).
- [ ] Métricas básicas de tempo de slicing.

## Milestone M2 - API Service MVP (Semanas 5-6)
- [ ] Escolha linguagem serviço.
- [ ] Endpoint upload modelo + parâmetros.
- [ ] Fila interna in-memory (protótipo).
- [ ] Worker chamando CLI.
- [ ] Endpoint status + download resultado.
- [ ] Logs estruturados.

## Milestone M3 - Escalabilidade & Observabilidade (Semanas 7-9)
- [ ] Migrar fila para Redis Streams.
- [ ] Cache de resultados (hash inputs).
- [ ] OpenTelemetry tracing + métricas.
- [ ] Limites de recursos e timeouts.

## Milestone M4 - Qualidade & Extensões (Semanas 10-12)
- [ ] gRPC opcional.
- [ ] Suporte perfis multi-material.
- [ ] Otimizações de warm pool de workers.
- [ ] CI completo (lint, build, testes, comparação G-code).

## Futuro
- Multi-tenant isolado.
- UI web simples para debug.
- Autoscaling baseado em fila.
