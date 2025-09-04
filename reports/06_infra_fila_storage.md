# Infra (Enxuto)

Fila: Redis Streams (MVP). Pivot NATS JetStream se throughput exigir. 
Storage: FS local + hash. Futuro S3/MinIO (M3+). 
Cache: chave hash(input+params+versão). TTL para limpeza.
Metadados: Redis agora; Postgres depois para histórico.

Observabilidade: logs JSON stdout, Prometheus métricas (queue, active_workers, slice_duration, cache_hit). Tracing OpenTelemetry (spans: enqueue, dequeue, slice_exec, postprocess).

Config (env): REDIS_URL, STORAGE_PATH, WORKER_PARALLELISM, TIMEOUT_SEC.

Segurança: limite upload, hash SHA256, sandbox processo (futuro seccomp/gVisor).

Escala: API stateless; pool aquecido de processos headless; backpressure HTTP 429.

Roadmap Infra:
1. Redis + FS.
2. Métricas Prometheus.
3. MinIO.
4. Tracing.
5. Postgres histórico.
