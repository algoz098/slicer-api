# Stack API (Atual)

- MVP em Node.js (Feathers + Koa) para entrega rápida de valor e maior velocidade de iteração.
- Go e/ou Rust seguem considerados para etapas futuras (quando houver medição real de overhead que justifique), mas não prioritários agora.
- Python reservado para scripts de teste/normalização em CI no futuro.

Integração com Core C++ (OrcaSlicer 2.3.0):
1. Inicialmente via subprocess/headless custom (quando disponível);
2. Avaliar FFI no futuro se overhead de spawn for relevante (>5% do tempo de slicing);
3. Sidecar gRPC opcional para stream de progresso em etapas futuras.

API: REST JSON (jobs), com possibilidade de gRPC no futuro.

Critérios de Seleção:
- Simplicidade de deploy (Docker posteriormente);
- Observabilidade nativa;
- Overhead mínimo comparado ao custo do slice.

Indicadores de Saúde:
- Overhead enqueue->exec <150ms p95;
- Overhead spawn <%5 tempo slice;
- Memória estável sem growth anômalo.
