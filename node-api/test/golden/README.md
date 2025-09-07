# Golden Tests para Paridade com OrcaSlicer

Este diretório contém testes de paridade (golden tests) que comparam a saída do nosso slicer com a saída do OrcaSlicer oficial.

## Estrutura

- `fixtures/` - Arquivos .3mf de teste
- `expected/` - G-code esperado gerado pelo OrcaSlicer
- `golden.test.ts` - Testes de comparação

## Como funciona

1. Fixtures .3mf são processados pelo nosso addon
2. O G-code gerado é comparado com o G-code de referência do Orca
3. Diferenças triviais (timestamps, comentários específicos) são normalizadas
4. Diferenças significativas causam falha no teste

## Adicionando novos testes

1. Adicione um arquivo .3mf em `fixtures/`
2. Gere o G-code de referência com OrcaSlicer oficial
3. Salve em `expected/` com o mesmo nome base
4. Execute os testes para validar

## Normalização

As seguintes diferenças são ignoradas:
- Timestamps e datas
- Comentários de versão específicos
- Caminhos de arquivo absolutos
- Ordem de comentários não-funcionais
