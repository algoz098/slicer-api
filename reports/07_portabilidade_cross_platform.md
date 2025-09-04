# Portabilidade & Cross-Platform

## Alvos
- Desenvolvimento: Windows, macOS (Apple Silicon + Intel), Linux.
- Produção: Containers Linux (x86_64 inicialmente, avaliar ARM64).

## Desafios
- Dependências do core C++ (bibliotecas gráficas/GUI) devem ser removidas ou mocked (headless build).
- Diferentes toolchains (MSVC vs clang/gcc) podem gerar variações nas otimizações (raro afetar G-code mas monitorar).
- Normalização de fins de linha e paths em testes de comparação de G-code.

## Estratégia de Build
- Docker multi-stage (base build + runtime leve).
- Uso de CMake presets para perfis: release-headless, debug.
- Cross compile para Linux/ARM64 possível via `dockcross` ou `zig cc` (futuro).

## Apple Silicon
- Build nativo arm64 + imagem de produção amd64 (uso de QEMU para teste básico se necessário). Priorizar CI em amd64 para baseline.

## Windows
- Fornecer script PowerShell: setup deps + build headless.
- Evitar caminhos com espaços em scripts (usar variáveis entre aspas).

## Testes de Equivalência
- Executar slicing core sob cada plataforma com mesmo input/profile.
- Normalizar output: remover linhas voláteis (timestamps, generator version se divergente).
- Dif semântico: comparar sequências de movimentos G1/G0 e parâmetros (E, F, X, Y, Z, extrusão cumulativa).

## Empacotamento
- Binário do core + assets mínimos (profiles baseline) incorporados na imagem.
- Versões etiquetadas: vCore-2.3.0-buildN.

## Ferramentas Auxiliares
- `git submodule update --init` para core.
- `ctest` para rodar smoke tests (futuro).
- Script `compare_gcode` (Python) multi-plataforma.

## Checklist Portabilidade (Iterativo)
- [ ] Build Linux x86_64.
- [ ] Build Linux arm64 (opcional inicial).
- [ ] Build macOS arm64.
- [ ] Build Windows x64.
- [ ] Test equivalência G-code por plataforma.
- [ ] Pipeline CI matrix.

# Portabilidade (Enxuto)

Alvos: Dev (Win/macOS/Linux). Prod: Docker Linux x86_64 (ARM64 depois).

Build: CMake presets (release-headless, debug). Docker multi-stage. Cross ARM64 futuro.

Remover UI: macros HEADLESS eliminando libs gráficas.

Teste equivalência: baseline GUI vs headless por plataforma -> normalização -> diff semântico.

Empacotamento: binário + perfis mínimos; tag vCore-2.3.0-buildN.

Checklist:
- [ ] Linux x86_64
- [ ] Windows x64
- [ ] macOS arm64
- [ ] (Opcional) Linux arm64
- [ ] Diff semântico ok
- [ ] CI matrix

Aceite: zero dif semântico (cubo, Benchy, overhang+suportes).
