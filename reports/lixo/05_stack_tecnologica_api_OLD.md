# Stack API (VERSÃO ANTIGA)

Linguagem inicial: Go (binário simples, concorrência). 
Possível pivot: Rust FFI se overhead spawn >5% tempo slice.
Python: apenas scripts de teste/normalização.

Integração Core:
1. Subprocess headless (atual).
2. FFI (Go CGO ou Rust) se necessário.
3. Sidecar gRPC opcional (stream de progresso).

Formato API: REST JSON (jobs). Futuro gRPC para streaming.

Critérios Seleção:
- Simplicidade deploy Docker.
- Observabilidade nativa.
- Overhead mínimo frente ao custo do slice.

Indicadores:
- Overhead enqueue->exec <150ms p95.
- Overhead spawn <%5 tempo slice.
- Memória estável sem growth anômalo.

