# 🏁 Bugfix-Bruteforce — GRAND TOTAL (27 Rodadas)

## 📊 Sumário Final

| Métrica | Valor |
|----------|-------|
| Rodadas | 27 |
| Bugs encontrados | ~95 |
| Bugs corrigidos | ~80 |
| Arquivos modificados | 30 |
| Linhas alteradas | +702 / −428 |
| Arquivos novos | 1 (logging-guard.ts) |

## 🔝 Top 15 Bugs

1. CRITICAL - stdout/stderr permanent corruption (double silencing)
2. CRITICAL - KlipperClient never exported (index.js early return)
3. CRITICAL - Docker cross-arch cache contamination
4. CRITICAL - No auth/rate-limit/file-caps → trivial DoS
5. CRITICAL - Zero crash isolation (SIGSEGV kills Node.js)
6. HIGH - 5 extern "C" functions without try-catch
7. HIGH - 4 null function pointer → SIGSEGV
8. HIGH - Zip bomb OOM (no file size limit)
9. HIGH - 3MF state leaks into STL slices
10. HIGH - Path traversal bypass (/tmp2 passes /tmp check)
11. HIGH - NaN/Infinity propagate to G-code
12. HIGH - dup2 errors permanently lose stdout/stderr
13. HIGH - User resourcesPath silently ignored
14. HIGH - G-code read into memory with no size limit
15. HIGH - Thread pool starvation + unbounded queue

## 📂 Full details: .bugfix-bruteforce/round-*.json
