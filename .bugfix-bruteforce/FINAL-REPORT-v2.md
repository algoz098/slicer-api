# 🏁 Bugfix-Bruteforce — RELATÓRIO FINAL (43 Rodadas)

## 📊 Estatísticas

| Métrica | Valor |
|----------|-------|
| Rodadas | 43 |
| Bugs encontrados | ~124 |
| Bugs corrigidos | ~98 |
| Known issues | ~22 |
| Ignorados (submodule) | 4 |
| Arquivos modificados | 31 |
| Linhas alteradas | +840 / −501 |
| Novo arquivo | 1 (logging-guard.ts) |

## 🔝 Top 15 Descobertas

1. CRITICAL - ORCACLI_ENGINE_PATH = RCE via dlopen (zero path validation)
2. CRITICAL - RTLD_GLOBAL exporta símbolos do engine (colisão com outros módulos)
3. CRITICAL - Double silencing stdout/stderr corrupção permanente
4. HIGH - Semicolons quebram arrays (ConfigOptionFloats truncados ao 1º elemento)
5. HIGH - realpathSync calculado mas DESCARTADO (TOCTOU symlink bypass)
6. HIGH - Path traversal /tmp2 passa startsWith('/tmp')
7. HIGH - Config state leakage 3MF→STL
8. HIGH - print->validate() NUNCA chamado (7 validações bypassadas)
9. HIGH - Zip bomb DoS sem limite de tamanho
10. HIGH - 4 function pointers FFI sem null-check → SIGSEGV
11. HIGH - NaN/Infinity propagam até G-code
12. HIGH - modes[] out-of-bounds → SIGSEGV com zero layers
13. HIGH - koa-body temp files nunca limpos
14. HIGH - std::cout vaza config values em 60+ locais
15. HIGH - Stale 3MF override state leak em loadModelFromFile failure

## 📈 Por severidade (total)

| | Encontrados | Corrigidos |
|---|------------|------------|
| CRITICAL | 8 | 5 |
| HIGH | 42 | 35 |
| MEDIUM | 50 | 42 |
| LOW | 24 | 16 |
| **TOTAL** | **~124** | **~98** |
