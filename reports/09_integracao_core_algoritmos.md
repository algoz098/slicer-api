# Integração Core (Enxuto)

Princípio: reutilizar 100% dos algoritmos de geometria, geração de trajetórias, infill, suportes e emissão de G-code. Não reescrever.

Headless: criar alvo próprio (não existe CLI pronto). Inicializa somente o necessário e termina com `exit code` claro.

Estratégia Fases:
1. Headless subprocess (processo por job ou pool aquecido).
2. Avaliar FFI se overhead >5%.
3. (Opcional) Sidecar gRPC para streaming progresso.

Determinismo: baseline GUI -> normalização -> diff semântico (movimentos, extrusão total, retrações, perímetros/layer).

Sensibilidade: floating point flags, ordem de iteração, seeds determinísticos.

Entrypoint Extração:
1. Localizar função slice acionada pela UI.
2. Encapsular em `SliceEngine`.
3. Macros `HEADLESS` para remover UI.
4. Smoke test: cube.stl.

Métricas Chave:
- Linhas G-code dif: 0
- Extrusão total dif: 0
- Tempo estimado dif: <=0.5%
- Retrações dif: 0

Riscos & Mitigação:
- Acoplamento UI: façade + macros.
- Globals/thread safety: processo isolado.
- Divergência upstream: fixar tag.
- Overhead spawn: medir e decidir FFI.

Checklist:
- [ ] Mapear entrypoint
- [ ] Criar alvo headless
- [ ] Baseline 3 modelos
- [ ] Normalizador
- [ ] Diff semântico
- [ ] Métricas spawn
- [ ] Decisão FFI

Estado: manter modelo subprocess até métricas pedirem mudança.
