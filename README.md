# Slicer API

Objetivo: Expor via API (web service) funcionalidades equivalentes ao OrcaSlicer 2.3.0 stable de forma que os artefatos (ex: G-code) sejam indistinguíveis aos gerados pelo OrcaSlicer oficial.

Primeira fase: Pesquisa e planejamento.

Consulte a pasta `reports/` para relatórios incrementais.

## Estrutura de dados persistentes

Os perfis de impressora e arquivos persistentes ficam na pasta fixa `data/profiles` na raiz do projeto. Para uso com Docker, basta mapear o volume `./data`.
