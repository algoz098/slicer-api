# Roadmap de Implementação - Suporte K2 Plus no OrcaSlicer

## 🎯 Objetivo

Adicionar suporte completo para impressoras Creality K2 Plus no OrcaSlicer, incluindo:
- Sistema de multicor otimizado para hotend grande (183mm³)
- Cálculo inteligente de purge baseado em HSV
- Integração com sistema Creality (Klipper)

---

## 📊 Análise de Impacto

### Problemas Atuais no OrcaSlicer

1. **Volume de Purge Insuficiente**
   - OrcaSlicer: ~800mm³ máximo
   - K2 Plus precisa: até 1200mm³
   - **Resultado**: Cores não limpam completamente

2. **Não Considera Volume do Hotend**
   - OrcaSlicer: Não tem parâmetro `nozzle_volume`
   - K2 Plus: 183mm³ (3.6x maior que Bambu)
   - **Resultado**: Cálculos incorretos de purge

3. **Algoritmo de Flush Simplificado**
   - OrcaSlicer: Baseado em matriz fixa
   - CrealityPrint: Cálculo dinâmico HSV + Luminância
   - **Resultado**: Desperdício ou falta de material

4. **Tempo de Impressão Incorreto**
   - OrcaSlicer: Não considera tempo de flush específico
   - K2 Plus: 86s por troca de filamento
   - **Resultado**: Estimativas erradas

---

## 🗺️ Roadmap de Implementação

### Fase 1: Fundação (1-2 semanas)

#### 1.1 Adicionar Parâmetros de Configuração
**Arquivos**: `PrintConfig.cpp`, `PrintConfig.hpp`

```cpp
// Novos parâmetros a adicionar:
- nozzle_volume (coFloat, default: 0.0)
- creality_flush_time (coFloat, default: 86.0)
- wipe_tower_max_purge_speed (coFloat, default: 90.0)
- multicolor_method (coBool, default: false)
```

**Tarefas**:
- [ ] Adicionar definições em `PrintConfig.cpp`
- [ ] Adicionar campos em `PrintConfig.hpp`
- [ ] Criar testes unitários para novos parâmetros
- [ ] Documentar cada parâmetro

**Estimativa**: 3-4 dias

---

#### 1.2 Implementar FlushVolCalculator
**Arquivos**: Criar `FlushVolCalc.cpp`, `FlushVolCalc.hpp`

**Tarefas**:
- [ ] Portar código de `CrealityPrint/src/libslic3r/FlushVolCalc.cpp`
- [ ] Implementar funções auxiliares:
  - [ ] `DeltaHS_BBS()` - Distância HSV
  - [ ] `get_luminance()` - Cálculo de luminância
  - [ ] `calc_triangle_3rd_edge()` - Geometria
- [ ] Criar classe `FlushVolCalculator`
- [ ] Adicionar constantes:
  ```cpp
  const int g_min_flush_volume_from_support = 700;
  const int g_flush_volume_to_support = 230;
  const int g_max_flush_volume = 1200;
  ```
- [ ] Criar testes com cores conhecidas:
  - [ ] Branco → Preto: ~1200mm³
  - [ ] Preto → Branco: ~700mm³
  - [ ] Vermelho → Azul: ~800mm³

**Estimativa**: 4-5 dias

---

### Fase 2: WipeTower (2-3 semanas)

#### 2.1 Adaptar WipeTower para Volumes Maiores
**Arquivos**: `GCode/WipeTower.cpp`, `GCode/WipeTower.hpp`

**Opção A**: Criar `WipeTowerCreality` separada
- ✅ Não quebra código existente
- ✅ Mais fácil de manter
- ❌ Duplicação de código

**Opção B**: Adaptar `WipeTower` existente
- ✅ Sem duplicação
- ✅ Beneficia todas as impressoras
- ❌ Risco de quebrar funcionalidade existente

**Recomendação**: Opção A inicialmente, migrar para B depois

**Tarefas**:
- [ ] Criar `WipeTowerCreality.cpp` e `.hpp`
- [ ] Implementar `plan_toolchange()` com `purge_volume`
- [ ] Adicionar suporte a `wipe_tower_max_purge_speed`
- [ ] Implementar lógica de flush dividido (135mm³/vez)
- [ ] Adicionar suporte a comandos Klipper:
  ```cpp
  if (m_gcode_flavor == gcfKlipper)
      m_gcode += "SET_PRESSURE_ADVANCE ADVANCE=0\n";
  ```

**Estimativa**: 7-10 dias

---

#### 2.2 Integrar FlushVolCalculator com WipeTower
**Arquivos**: `WipeTowerCreality.cpp`, `Print.cpp`

**Tarefas**:
- [ ] Modificar `plan_toolchange()` para usar `FlushVolCalculator`
- [ ] Calcular `purge_volume` dinamicamente baseado em cores
- [ ] Passar `nozzle_volume` para cálculos
- [ ] Validar que volumes não excedem limites físicos

**Estimativa**: 3-4 dias

---

### Fase 3: Geração de GCode (1-2 semanas)

#### 3.1 Adaptar GCode.cpp
**Arquivos**: `GCode.cpp`, `GCode.hpp`

**Tarefas**:
- [ ] Adicionar constantes:
  ```cpp
  static const float g_min_purge_volume = 100.f;
  static const float g_purge_volume_one_time = 135.f;
  ```
- [ ] Implementar lógica de flush dividido
- [ ] Calcular `flush_count` baseado em `purge_volume`
- [ ] Configurar `first_flush_volume` e `second_flush_volume`
- [ ] Adicionar suporte a `WipeTowerCreality`

**Estimativa**: 5-6 dias

---

#### 3.2 Processar Tags e Tempo
**Arquivos**: `GCode/GCodeProcessor.cpp`, `GCode/GCodeProcessor.hpp`

**Tarefas**:
- [ ] Adicionar processamento de tag `creality_flush_time`
- [ ] Calcular tempo extra de flush:
  ```cpp
  float extra_time = total_filamentchanges * s_creality_flush_time;
  ```
- [ ] Atualizar estatísticas de impressão
- [ ] Adicionar ao tempo total estimado

**Estimativa**: 2-3 dias

---

### Fase 4: Perfis e Configuração (1 semana)

#### 4.1 Criar Perfil K2 Plus
**Arquivos**: `resources/profiles/Creality/`

**Tarefas**:
- [ ] Criar estrutura de diretórios:
  ```
  resources/profiles/Creality/
  ├── machine/
  │   ├── Creality K2 Plus.json
  │   ├── Creality K2 Plus 0.2 nozzle.json
  │   ├── Creality K2 Plus 0.4 nozzle.json
  │   ├── Creality K2 Plus 0.6 nozzle.json
  │   └── Creality K2 Plus 0.8 nozzle.json
  ├── filament/
  └── process/
  ```
- [ ] Configurar parâmetros base:
  ```json
  {
    "nozzle_volume": "183",
    "creality_flush_time": "86.0",
    "wipe_tower_max_purge_speed": "90",
    "multicolor_method": "1",
    "gcode_flavor": "klipper"
  }
  ```
- [ ] Adicionar GCode de start/end/change
- [ ] Configurar velocidades e acelerações

**Estimativa**: 3-4 dias

---

#### 4.2 Interface de Usuário
**Arquivos**: GUI relacionados

**Tarefas**:
- [ ] Adicionar campos na UI para novos parâmetros
- [ ] Criar tooltips explicativos
- [ ] Adicionar validação de valores
- [ ] Testar usabilidade

**Estimativa**: 2-3 dias

---

### Fase 5: Testes e Validação (2-3 semanas)

#### 5.1 Testes Unitários
**Tarefas**:
- [ ] Testar `FlushVolCalculator` com cores conhecidas
- [ ] Testar cálculo de `purge_volume`
- [ ] Testar geração de GCode
- [ ] Testar processamento de tempo

**Estimativa**: 4-5 dias

---

#### 5.2 Testes de Integração
**Tarefas**:
- [ ] Testar slice completo com multicor
- [ ] Validar GCode gerado
- [ ] Verificar estimativas de tempo
- [ ] Testar com diferentes combinações de cores

**Estimativa**: 3-4 dias

---

#### 5.3 Testes Reais
**Tarefas**:
- [ ] Imprimir teste simples (2 cores)
- [ ] Imprimir teste complexo (4+ cores)
- [ ] Validar qualidade de troca de cor
- [ ] Ajustar parâmetros se necessário
- [ ] Documentar resultados

**Estimativa**: 5-7 dias

---

## 📋 Checklist Completo

### Código
- [ ] `FlushVolCalc.cpp` e `.hpp` implementados
- [ ] `WipeTowerCreality.cpp` e `.hpp` implementados
- [ ] `PrintConfig.cpp` atualizado com novos parâmetros
- [ ] `GCode.cpp` adaptado para flush dividido
- [ ] `GCodeProcessor.cpp` processando tags Creality

### Configuração
- [ ] Perfis K2 Plus criados (todos os nozzles)
- [ ] GCode de start/end/change configurado
- [ ] Parâmetros de velocidade/aceleração ajustados

### Testes
- [ ] Testes unitários passando
- [ ] Testes de integração passando
- [ ] Pelo menos 3 impressões reais bem-sucedidas

### Documentação
- [ ] Código comentado
- [ ] README atualizado
- [ ] Guia de uso para K2 Plus
- [ ] Changelog atualizado

---

## ⏱️ Estimativa Total

| Fase | Duração | Dependências |
|------|---------|--------------|
| Fase 1: Fundação | 1-2 semanas | Nenhuma |
| Fase 2: WipeTower | 2-3 semanas | Fase 1 |
| Fase 3: GCode | 1-2 semanas | Fase 2 |
| Fase 4: Perfis | 1 semana | Fase 3 |
| Fase 5: Testes | 2-3 semanas | Fase 4 |

**Total**: 7-11 semanas (2-3 meses)

---

## 🚨 Riscos e Mitigações

### Risco 1: Quebrar Funcionalidade Existente
**Probabilidade**: Média
**Impacto**: Alto
**Mitigação**:
- Criar `WipeTowerCreality` separada inicialmente
- Testes extensivos antes de merge
- Feature flag para habilitar/desabilitar

### Risco 2: Algoritmo HSV Não Funcionar Bem
**Probabilidade**: Baixa
**Impacto**: Médio
**Mitigação**:
- Testar com cores reais antes de implementar
- Manter opção de usar matriz manual
- Permitir ajuste de multiplicador

### Risco 3: Tempo de Desenvolvimento Maior que Estimado
**Probabilidade**: Alta
**Impacto**: Médio
**Mitigação**:
- Implementar em fases independentes
- Cada fase entrega valor
- Priorizar funcionalidades core

---

## 🎯 Critérios de Sucesso

### Mínimo Viável (MVP)
- [ ] K2 Plus consegue imprimir multicor
- [ ] Volumes de purge corretos (sem cores misturadas)
- [ ] Tempo estimado razoavelmente preciso

### Ideal
- [ ] Algoritmo HSV otimiza uso de material
- [ ] Suporte completo a Klipper
- [ ] Interface amigável para configuração
- [ ] Documentação completa

### Excelente
- [ ] Beneficia outras impressoras também
- [ ] Código limpo e bem testado
- [ ] Contribuição aceita no OrcaSlicer oficial

---

## 📚 Recursos Necessários

### Conhecimento
- C++ (intermediário/avançado)
- Algoritmos de cor (RGB/HSV)
- GCode e impressão 3D
- Klipper firmware

### Hardware
- Impressora K2 Plus para testes
- Filamentos de cores variadas
- Tempo para impressões de teste

### Software
- Ambiente de desenvolvimento C++
- OrcaSlicer source code
- CrealityPrint source code (referência)

---

**Versão**: 1.0
**Data**: 2025-10-07
**Status**: Planejamento

