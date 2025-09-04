# Resumo Prós & Contras (Enxuto)

## Linguagens
| Opção | Prós | Contras | Papel Sugerido |
|-------|------|---------|----------------|
| Go | Simplicidade deploy, binário único, concorrência nativa | CGO cross-complica, menos controle baixo nível | API inicial, orquestração |
| Rust | Performance, segurança memória, FFI forte | Curva aprendizado, builds lentos | Evolução futura, bindings diretos |
| Python | Rapidez protótipo, libs utilitárias | Performance, GIL, overhead runtime | Scripts de comparação/testes |
| C++ Puro | Sem overhead, máximo controle | Produtividade baixa, complexidade build | Core interno somente |

## Integração Core
| Estratégia | Prós | Contras | Fase |
|------------|------|---------|------|
| Headless Custom (subprocess) | Implementação clara, isolamento | Trabalho extra inicial, overhead spawn | M1-M2 |
| FFI Direto | Baixa latência, menor overhead I/O | Gestão memória/threads complexa | M3+ (se necessário) |
| Sidecar RPC | Isolamento forte, escalável | Engenharia extra, latência RPC | Futuro |

## Fila
| Opção | Prós | Contras | Fase |
|-------|------|---------|------|
| Redis Streams | Simples, rápido | Memória, persistência limitada | MVP |
| NATS JetStream | Performance, leve | Operação menos trivial | Escala |
| RabbitMQ | Maturidade | Overhead | Casos específicos |
| Kafka | Alta escala | Overkill, complexidade | Não inicial |

## Storage
| Opção | Prós | Contras | Fase |
|-------|------|---------|------|
| FS Local | Simples, rápido | Não distribuído | MVP |
| S3/MinIO | Escalável, versiona | Setup extra | M3 |

## Observabilidade
- Logs JSON + Prometheus + OpenTelemetry desde início para evitar retrabalho.

## Decisões Iniciais (Propostas)
1. Go + CLI subprocess para entregar valor rápido.
2. Redis Streams + FS local.
3. Normalizador de G-code em Python para testes.
4. Estrutura de build headless C++ via CMake + Docker.
5. CI matrix cross-platform prioritária (Linux + Windows + macOS) assim que headless estabilizar.

## Pivots Condicionais
- Migrar para Rust FFI caso overhead de subprocess > alvo ou necessidade de streaming de progresso em tempo real.
- Introduzir MinIO se volume de arquivos crescer além do disco local compartilhado.
- Adicionar Postgres se histórico de jobs precisar consultas avançadas.

## Métricas Gate para Decidir Pivot
| Métrica | Limite | Ação |
|---------|--------|------|
| Overhead spawn (%) | >5% tempo médio slice | Avaliar FFI |
| Cache hit rate | <30% após tuning | Revisar estratégia perfis/param hash |
| Memória processo core | Crescente/leak > N jobs | Reciclagem agressiva ou FFI |
| Backlog delay p95 | > SLA (ex: 2x tempo slice) | Escalar workers / otimizar fila |
