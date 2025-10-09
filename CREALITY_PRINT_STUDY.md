# Estudo: CrealityPrint vs OrcaSlicer - Suporte K2 Plus

## 📋 Resumo Executivo

Este documento analisa as diferenças entre CrealityPrint e OrcaSlicer no suporte para impressoras Creality (especialmente K2 Plus), focando em:
1. **Sistema de multicor/purge**
2. **Envio para o sistema Creality**
3. **Configurações específicas do hotend K2**

---

## 🎯 Principais Diferenças Identificadas

### 1. Volume do Nozzle (Hotend)

#### CrealityPrint
- **Arquivo**: `resources/profiles/Creality/machine/Creality K2 Plus 0.4 nozzle.json`
- **Parâmetro**: `"nozzle_volume": "183"`
- **Localização no código**: `src/libslic3r/PrintConfig.cpp:3690`

```cpp
def = this->add("nozzle_volume", coFloat);
def->label = L("Nozzle volume");
def->tooltip = L("Volume of nozzle between the cutter and the end of nozzle");
def->sidetext = L("mm³");
def->mode = comAdvanced;
def->readonly = false;
def->set_default_value(new ConfigOptionFloat { 0.0 });
```

**Importância**: O hotend da K2 Plus tem volume de **183mm³**, muito maior que o da Bambu Lab (~50mm³). Isso afeta diretamente o cálculo de purge necessário.

---

### 2. Sistema de Purge/Flush

#### CrealityPrint - Cálculo de Volume de Flush

**Arquivo principal**: `src/libslic3r/FlushVolCalc.cpp`

**Constantes importantes**:
```cpp
const int g_min_flush_volume_from_support = 700.f;
const int g_flush_volume_to_support = 230;
const int g_max_flush_volume = 1200;
```

**Algoritmo de cálculo** (linhas 47-100):
```cpp
int FlushVolCalculator::calc_flush_vol(
    unsigned char src_a, unsigned char src_r, unsigned char src_g, unsigned char src_b,
    unsigned char dst_a, unsigned char dst_r, unsigned char dst_g, unsigned char dst_b)
{
    // Materiais transparentes são tratados como brancos
    if (src_a == 0) {
        src_r = src_g = src_b = 255;
    }
    if (dst_a == 0) {
        dst_r = dst_g = dst_b = 255;
    }

    // Calcula distância de cor no espaço HSV
    RGB2HSV(src_r_f, src_g_f, src_b_f, &from_hsv_h, &from_hsv_s, &from_hsv_v);
    RGB2HSV(dst_r_f, dst_g_f, dst_b_f, &to_hsv_h, &to_hsv_s, &to_hsv_v);
    float hs_dist = DeltaHS_BBS(from_hsv_h, from_hsv_s, from_hsv_v, 
                                 to_hsv_h, to_hsv_s, to_hsv_v);

    // 1. Diferença de cor é mais óbvia se a cor destino tem alta luminância
    // 2. Diferença de cor é mais óbvia se a cor origem tem baixa luminância
    float from_lumi = get_luminance(src_r_f, src_g_f, src_b_f);
    float to_lumi = get_luminance(dst_r_f, dst_g_f, dst_b_f);
    
    // Cálculo baseado em luminância e matiz/saturação
    float flush_volume = calc_triangle_3rd_edge(hs_flush, lumi_flush, 120.f);
    flush_volume = std::max(flush_volume, 60.f);
    flush_volume += m_min_flush_vol;
    
    return std::min((int)flush_volume, m_max_flush_vol);
}
```

**Características**:
- Volume mínimo de flush: **700mm³** (de suporte)
- Volume máximo de flush: **1200mm³**
- Cálculo baseado em distância de cor no espaço HSV
- Considera luminância das cores origem e destino

---

### 3. WipeTower Específica da Creality

**Arquivo**: `src/libslic3r/FDM/WipeTowerCreality.cpp` (1578 linhas)
**Header**: `src/libslic3r/FDM/WipeTowerCreality.hpp`

**Diferenças da implementação padrão**:

```cpp
class WipeTowerCreality
{
public:
    WipeTower::ToolChangeResult construct_tcr(
        WipeTowerWriterCreality& writer, 
        bool priming,
        size_t old_tool, 
        bool is_finish, 
        float purge_volume) const;

    void plan_toolchange(
        float z_par,
        float layer_height_par,
        unsigned int old_tool,
        unsigned int new_tool,
        float wipe_volume  = 0.f,
        float purge_volume = 0.f);
```

**Parâmetros específicos**:
```cpp
float m_wipe_tower_max_purge_speed = 90.f;  // Velocidade máxima de purge
```

**Configuração no PrintConfig.cpp** (linha 5504):
```cpp
def = this->add("wipe_tower_max_purge_speed", coFloat);
def->label = L("Maximum wipe tower print speed");
def->tooltip = L("The maximum print speed when purging in the wipe tower...");
def->sidetext = L("mm/s");
def->mode = comAdvanced;
def->min = 10;
def->set_default_value(new ConfigOptionFloat(90.));
```

---

### 4. Tempo de Flush da Creality

**Parâmetro**: `creality_flush_time`
**Valor padrão K2 Plus**: `86.0` segundos

**Arquivo de configuração**: `Creality K2 Plus 0.4 nozzle.json`
```json
"creality_flush_time": "86.0"
```

**Implementação no código** (`src/libslic3r/GCode/GCodeProcessor.cpp`):
```cpp
// Linha 1658-1660
const ConfigOptionFloat* creality_flush_time = config.option<ConfigOptionFloat>("creality_flush_time");
if (creality_flush_time != nullptr)
    s_creality_flush_time = creality_flush_time->value;

// Linha 2272 - Cálculo do tempo total
float flush_times = s_creality_flush_time * m_result.print_statistics.total_filamentchanges;

// Linha 8335 - Tempo extra adicionado
float extra_time = m_result.print_statistics.total_filamentchanges * s_creality_flush_time;
```

---

### 5. Método de Multicor

**Parâmetro**: `multicolor_method`
**Valor K2 Plus**: `"1"`

**Arquivo de configuração**: `Creality K2 Plus 0.4 nozzle.json`
```json
"multicolor_method": "1"
```

**Definição no código** (`src/libslic3r/PrintConfig.cpp:1718`):
```cpp
def = this->add("multicolor_method", coBool);
```

---

### 6. Constantes de Purge no GCode.cpp

**Arquivo**: `src/libslic3r/GCode.cpp`

```cpp
static const float g_min_purge_volume = 100.f;
static const float g_purge_volume_one_time = 135.f;
```

**Uso no código**:
```cpp
float purge_volume = tcr.purge_volume < EPSILON ? 0 : 
                     std::max(tcr.purge_volume, g_min_purge_volume);

int flush_count = std::min(g_max_flush_count, 
                          (int)std::round(purge_volume / g_purge_volume_one_time));
float flush_unit = purge_length / flush_count;
```

---

## 📊 Comparação de Valores

| Parâmetro | CrealityPrint (K2 Plus) | OrcaSlicer (Bambu) | Diferença |
|-----------|-------------------------|-------------------|-----------|
| Nozzle Volume | 183 mm³ | ~50 mm³ | +266% |
| Max Flush Volume | 1200 mm³ | ~800 mm³ | +50% |
| Min Flush Volume (support) | 700 mm³ | ~400 mm³ | +75% |
| Flush Time | 86s | N/A | Específico Creality |
| Max Purge Speed | 90 mm/s | 90 mm/s | Igual |

---

## 🔍 Arquivos-Chave Identificados

### Configuração
1. `resources/profiles/Creality/machine/Creality K2 Plus.json`
2. `resources/profiles/Creality/machine/Creality K2 Plus 0.4 nozzle.json`

### Código Core
1. `src/libslic3r/FlushVolCalc.cpp` - Cálculo de volume de flush
2. `src/libslic3r/FlushVolCalc.hpp` - Interface do calculador
3. `src/libslic3r/FDM/WipeTowerCreality.cpp` - Torre de limpeza específica
4. `src/libslic3r/FDM/WipeTowerCreality.hpp` - Header da torre
5. `src/libslic3r/GCode.cpp` - Geração de GCode com purge
6. `src/libslic3r/PrintConfig.cpp` - Definições de configuração

### GCode Processing
1. `src/libslic3r/GCode/GCodeProcessor.cpp` - Processamento e estatísticas
2. `src/libslic3r/GCode/WipeTower.cpp` - Torre padrão (1747 linhas)
3. `src/libslic3r/GCode/WipeTower2.cpp` - Versão alternativa

---

## 🎨 Algoritmo de Cálculo de Flush

### Fórmula Simplificada

```
flush_volume = f(hs_distance, luminance_diff) + min_flush_vol

Onde:
- hs_distance: Distância no espaço HSV (matiz/saturação)
- luminance_diff: Diferença de luminância entre cores
- min_flush_vol: Volume mínimo configurado
```

### Fatores Considerados

1. **Distância de Cor (HSV)**:
   - Matiz (Hue)
   - Saturação (Saturation)
   - Valor/Brilho (Value)

2. **Luminância**:
   - Cores claras → escuras: Mais flush
   - Cores escuras → claras: Menos flush

3. **Transparência**:
   - Materiais transparentes tratados como brancos

---

## 🚀 Próximos Passos Sugeridos

### Para Integração no OrcaSlicer

1. **Adicionar parâmetro `nozzle_volume`**:
   - Permitir configuração por impressora
   - Usar no cálculo de purge

2. **Implementar `FlushVolCalculator`**:
   - Portar algoritmo de cálculo baseado em HSV
   - Ajustar constantes para K2 Plus

3. **Criar `WipeTowerCreality` ou adaptar existente**:
   - Suportar volumes maiores de purge
   - Implementar `wipe_tower_max_purge_speed`

4. **Adicionar `creality_flush_time`**:
   - Para estimativa correta de tempo de impressão
   - Específico para impressoras Creality

5. **Implementar `multicolor_method`**:
   - Suportar método específico da Creality
   - Diferenciar de método Bambu

---

## 📝 Notas Importantes

### Diferenças Críticas

1. **Volume do Hotend**: O hotend da K2 Plus é **3.6x maior** que o da Bambu
2. **Purge Máximo**: CrealityPrint permite até **1200mm³** vs ~800mm³ do OrcaSlicer
3. **Cálculo Inteligente**: Usa espaço de cor HSV para otimizar volume de flush
4. **Tempo de Flush**: Adiciona tempo específico por troca de filamento

### Compatibilidade

- CrealityPrint é fork do OrcaSlicer (que é fork do BambuStudio)
- Mantém compatibilidade com estrutura base
- Adiciona extensões específicas para Creality

---

## 🔗 Referências

- Repositório: https://github.com/CrealityOfficial/CrealityPrint
- Baseado em: OrcaSlicer (SoftFever) → BambuStudio → PrusaSlicer → Slic3r
- Versão analisada: Latest (commit mais recente)

---

## 🔧 Detalhes de Implementação Técnica

### 7. Suporte a Klipper

**GCode Flavor**: `"gcode_flavor": "klipper"`

**Comandos específicos Klipper** (`WipeTowerCreality.cpp`):

```cpp
WipeTowerWriterCreality& disable_linear_advance() {
    if (m_gcode_flavor == gcfRepRapSprinter || m_gcode_flavor == gcfRepRapFirmware)
        m_gcode += (std::string("M572 D") + std::to_string(m_current_tool) + " S0\n");
    else if (m_gcode_flavor == gcfKlipper)
        m_gcode += "SET_PRESSURE_ADVANCE ADVANCE=0\n";  // Comando Klipper
    else
        m_gcode += "M900 K0\n";
    return *this;
}
```

**Start GCode K2 Plus**:
```gcode
M140 S0
M104 S0
START_PRINT EXTRUDER_TEMP=[nozzle_temperature_initial_layer] BED_TEMP=[bed_temperature_initial_layer_single]
T[initial_no_support_extruder]
M104 S[nozzle_temperature_initial_layer]
M204 S2000
G1 Z3 F600
M83
G1 Y150 F12000
G1 X0 F12000
G1 Z0.2 F600
G1 X0 Y150 F6000
G1 E0.8 F300
G1 X0 Y0 E9 F{filament_max_volumetric_speed[initial_extruder]/0.3*60}
G1 X150 Y0 E9 F{filament_max_volumetric_speed[initial_extruder]/0.3*60}
G92 E0
G1 Z1 F600
```

**End GCode K2 Plus**:
```gcode
END_PRINT
```

**Change Filament GCode**:
```gcode
G2 Z{z_after_toolchange + 0.4} I0.86 J0.86 P1 F10000 ; spiral lift a little from second lift
G1 X0 Y245 F30000
G1 Z{z_after_toolchange} F600
```

---

### 8. Estrutura de Dados ToolChange

**Arquivo**: `src/libslic3r/FDM/WipeTowerCreality.hpp`

```cpp
WipeTower::ToolChangeResult construct_tcr(
    WipeTowerWriterCreality& writer,
    bool priming,
    size_t old_tool,
    bool is_finish,
    float purge_volume) const;

// Resultado contém:
result.purge_volume = purge_volume;  // Volume de purge calculado
```

**Planejamento de ToolChange** (`WipeTowerCreality.cpp:1232`):
```cpp
void WipeTowerCreality::plan_toolchange(
    float z_par,
    float layer_height_par,
    unsigned int old_tool,
    unsigned int new_tool,
    float wipe_volume,
    float purge_volume)
{
    m_plan.back().tool_changes.push_back(
        WipeTowerInfo::ToolChange(
            old_tool,
            new_tool,
            depth,
            0.0f,
            0.0f,
            wipe_volume,
            purge_volume  // Armazenado no plano
        )
    );
}
```

---

### 9. Cálculo de Flush Count

**Arquivo**: `src/libslic3r/GCode.cpp`

```cpp
static const float g_min_purge_volume = 100.f;
static const float g_purge_volume_one_time = 135.f;

// Cálculo do número de flushes
float purge_volume = tcr.purge_volume < EPSILON ? 0 :
                     std::max(tcr.purge_volume, g_min_purge_volume);

float purge_length = purge_volume / filament_area;

// Divide em múltiplos flushes de 135mm³ cada
int flush_count = std::min(g_max_flush_count,
                          (int)std::round(purge_volume / g_purge_volume_one_time));

float flush_unit = purge_length / flush_count;

// Configuração para cada flush
config.set_key_value("first_flush_volume", new ConfigOptionFloat(purge_length / 2.f));
config.set_key_value("second_flush_volume", new ConfigOptionFloat(purge_length / 2.f));
config.set_key_value("flush_length", new ConfigOptionFloat(purge_length));
```

---

### 10. Velocidades e Acelerações K2 Plus

**Configuração de máquina**:
```json
{
    "machine_max_acceleration_e": "5000",
    "machine_max_acceleration_extruding": "30000",
    "machine_max_acceleration_retracting": "5000",
    "machine_max_acceleration_travel": "30000",
    "machine_max_acceleration_x": "30000",
    "machine_max_acceleration_y": "30000",
    "machine_max_acceleration_z": "1000",

    "machine_max_jerk_e": "10",
    "machine_max_jerk_x": "300",
    "machine_max_jerk_y": "300",
    "machine_max_jerk_z": "30",

    "machine_max_speed_e": "50",
    "machine_max_speed_x": "800",
    "machine_max_speed_y": "800",
    "machine_max_speed_z": "30"
}
```

**Velocidade de purge** (`WipeTowerCreality.cpp:855`):
```cpp
const float target_speed = first_layer || (m_num_tool_changes <= 1 && m_no_sparse_layers)
    ? m_first_layer_speed * 60.f
    : std::min(m_wipe_tower_max_purge_speed * 60.f, m_infill_speed * 60.f);
```

---

### 11. Retração e Z-Hop

**Configuração K2 Plus**:
```json
{
    "retraction_length": "0.8",
    "retraction_speed": "40",
    "deretraction_speed": "40",
    "retract_before_wipe": "70",
    "wipe": "1",
    "wipe_distance": "2",
    "z_hop": "0.4",
    "z_hop_types": "Auto Lift"
}
```

---

### 12. Sistema de Tags para GCode Processor

**Arquivo**: `WipeTowerCreality.cpp:38-48`

```cpp
WipeTowerWriterCreality& change_analyzer_line_width(float line_width) {
    // Adiciona tag para o processador:
    std::stringstream str;
    str << ";" << GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width)
        << line_width << "\n";
    m_gcode += str.str();
    return *this;
}

WipeTowerWriterCreality& change_analyzer_mm3_per_mm(float len, float e) {
    static const float area = float(M_PI) * 1.75f * 1.75f / 4.f;
    float mm3_per_mm = (len == 0.f ? 0.f : area * e / len);
    std::stringstream str;
    str << ";" << GCodeProcessor::Mm3_Per_Mm_Tag << mm3_per_mm << "\n";
    m_gcode += str.str();
    return *this;
}
```

**Tags processadas** (`GCode/GCodeProcessor.cpp:3399-3401`):
```cpp
if (boost::starts_with(comment, " creality_flush_time = ")) {
    auto data = comment.substr(strlen(" creality_flush_time = "));
    this->m_result.creality_flush_time = std::stof(data.data());
}
```

---

## 🎯 Diferenças Críticas vs OrcaSlicer

### Sistema de Purge

| Aspecto | CrealityPrint | OrcaSlicer | Impacto |
|---------|---------------|------------|---------|
| Algoritmo de cálculo | HSV + Luminância | Baseado em matriz | Alto |
| Volume máximo | 1200mm³ | ~800mm³ | Alto |
| Considera nozzle volume | Sim (183mm³) | Não explícito | Crítico |
| Flush dividido | Sim (135mm³/vez) | Diferente | Médio |
| Tempo de flush | Sim (86s) | Não | Médio |

### Integração com Impressora

| Aspecto | CrealityPrint | OrcaSlicer | Impacto |
|---------|---------------|------------|---------|
| GCode Flavor | Klipper nativo | Marlin/Klipper | Médio |
| Comandos específicos | START_PRINT/END_PRINT | Genéricos | Alto |
| Pressure Advance | SET_PRESSURE_ADVANCE | M900/M572 | Médio |
| Multicolor method | Flag específica | Padrão | Alto |

---

## 📋 Checklist de Implementação

### Fase 1: Configuração Base
- [ ] Adicionar parâmetro `nozzle_volume` ao PrintConfig
- [ ] Adicionar parâmetro `creality_flush_time` ao PrintConfig
- [ ] Adicionar parâmetro `wipe_tower_max_purge_speed` ao PrintConfig
- [ ] Adicionar parâmetro `multicolor_method` ao PrintConfig

### Fase 2: Algoritmo de Flush
- [ ] Portar `FlushVolCalc.cpp` e `FlushVolCalc.hpp`
- [ ] Implementar cálculo HSV de distância de cor
- [ ] Ajustar constantes para K2 Plus:
  - [ ] `g_min_flush_volume_from_support = 700`
  - [ ] `g_flush_volume_to_support = 230`
  - [ ] `g_max_flush_volume = 1200`
- [ ] Integrar com sistema de flush existente

### Fase 3: WipeTower
- [ ] Criar `WipeTowerCreality` ou adaptar existente
- [ ] Implementar suporte a `purge_volume` maior
- [ ] Adicionar lógica de flush dividido (135mm³/vez)
- [ ] Implementar velocidade máxima de purge

### Fase 4: GCode Generation
- [ ] Adicionar suporte a comandos Klipper específicos
- [ ] Implementar tags de processamento
- [ ] Adicionar cálculo de tempo com `creality_flush_time`
- [ ] Testar geração de GCode

### Fase 5: Perfis de Impressora
- [ ] Criar perfil K2 Plus com todos os parâmetros
- [ ] Configurar `nozzle_volume = 183`
- [ ] Configurar `creality_flush_time = 86.0`
- [ ] Configurar `multicolor_method = 1`
- [ ] Adicionar GCode de start/end/change

### Fase 6: Testes
- [ ] Testar cálculo de flush com diferentes cores
- [ ] Validar volumes de purge gerados
- [ ] Verificar tempo estimado de impressão
- [ ] Testar impressão real na K2 Plus

---

## 🔍 Arquivos para Análise Detalhada

### Prioridade Alta
1. ✅ `src/libslic3r/FlushVolCalc.cpp` - Algoritmo core
2. ✅ `src/libslic3r/FDM/WipeTowerCreality.cpp` - Torre específica
3. ✅ `src/libslic3r/GCode.cpp` - Geração de GCode
4. ✅ `src/libslic3r/PrintConfig.cpp` - Configurações

### Prioridade Média
5. `src/libslic3r/GCode/GCodeProcessor.cpp` - Processamento
6. `src/libslic3r/GCode/WipeTower.cpp` - Torre padrão
7. `src/libslic3r/Preset.cpp` - Sistema de presets

### Prioridade Baixa
8. `src/slic3r/GUI/SendToPrinter.cpp` - Envio para impressora
9. `src/slic3r/Utils/NetworkAgent.cpp` - Comunicação de rede

---

**Data do Estudo**: 2025-10-07
**Autor**: Análise automatizada via Augment AI
**Versão**: 1.1 - Detalhes técnicos adicionados

