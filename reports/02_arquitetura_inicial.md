# Arquitetura (Versão Enxuta)

Fluxo: Cliente -> API (Go) -> Fila (Redis Streams) -> Workers (processo headless C++) -> Cache/Storage -> Retorno.

Componentes chave:
- API REST: criação job, status, download.
- Worker headless: executa slicing isolado.
- Cache: hash(input + parâmetros + versão core) -> G-code.
- Observabilidade: logs JSON + métricas + tracing.

Integração Core:
- Criar binário headless custom (não existe CLI funcional).
- Evoluir para FFI apenas se overhead spawn > alvo.

Controle de Versão:
- Submódulo tag 2.3.0 + patch mínimo (macros HEADLESS).

Escalabilidade & Isolamento:
- Process pool (warm) + limites CPU/mem/timeout.

Próximos Passos:
1. Mapear entrypoint slicing.
2. Adicionar alvo CMake headless.
3. Build e gerar baseline comparativo.
4. Normalizador + diff semântico.
