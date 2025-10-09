# CrealityPrint - Trechos de Código Importantes

## 📌 Índice de Arquivos e Linhas

Este documento contém os trechos de código mais relevantes do CrealityPrint para implementação no OrcaSlicer.

---

## 1. FlushVolCalc.cpp - Algoritmo de Cálculo de Flush

### Arquivo: `src/libslic3r/FlushVolCalc.cpp`

#### Constantes Globais (Linhas 9-12)
```cpp
const int g_min_flush_volume_from_support = 700.f;
const int g_flush_volume_to_support = 230;
const int g_max_flush_volume = 1200;
```

#### Função de Cálculo de Distância HSV (Linhas 30-39)
```cpp
static float DeltaHS_BBS(float h1, float s1, float v1, float h2, float s2, float v2)
{
    float h1_rad = to_radians(h1);
    float h2_rad = to_radians(h2);

    float dx = std::cos(h1_rad) * s1 * v1 - cos(h2_rad) * s2 * v2;
    float dy = std::sin(h1_rad) * s1 * v1 - sin(h2_rad) * s2 * v2;
    float dxy = std::sqrt(dx * dx + dy * dy);
    return std::min(1.2f, dxy);
}
```

#### Algoritmo Principal de Cálculo (Linhas 47-100)
```cpp
int FlushVolCalculator::calc_flush_vol(
    unsigned char src_a, unsigned char src_r, unsigned char src_g, unsigned char src_b,
    unsigned char dst_a, unsigned char dst_r, unsigned char dst_g, unsigned char dst_b)
{
    // BBS: Transparent materials are treated as white materials
    if (src_a == 0) {
        src_r = src_g = src_b = 255;
    }
    if (dst_a == 0) {
        dst_r = dst_g = dst_b = 255;
    }

    float src_r_f, src_g_f, src_b_f, dst_r_f, dst_g_f, dst_b_f;
    float from_hsv_h, from_hsv_s, from_hsv_v;
    float to_hsv_h, to_hsv_s, to_hsv_v;

    src_r_f = (float)src_r / 255.f;
    src_g_f = (float)src_g / 255.f;
    src_b_f = (float)src_b / 255.f;
    dst_r_f = (float)dst_r / 255.f;
    dst_g_f = (float)dst_g / 255.f;
    dst_b_f = (float)dst_b / 255.f;

    // Calculate color distance in HSV color space
    RGB2HSV(src_r_f, src_g_f, src_b_f, &from_hsv_h, &from_hsv_s, &from_hsv_v);
    RGB2HSV(dst_r_f, dst_g_f, dst_b_f, &to_hsv_h, &to_hsv_s, &to_hsv_v);
    float hs_dist = DeltaHS_BBS(from_hsv_h, from_hsv_s, from_hsv_v, 
                                 to_hsv_h, to_hsv_s, to_hsv_v);

    // 1. Color difference is more obvious if the dest color has high luminance
    // 2. Color difference is more obvious if the source color has low luminance
    float from_lumi = get_luminance(src_r_f, src_g_f, src_b_f);
    float to_lumi = get_luminance(dst_r_f, dst_g_f, dst_b_f);
    float lumi_flush = 0.f;
    
    if (to_lumi >= from_lumi) {
        lumi_flush = std::pow(to_lumi - from_lumi, 0.7f) * 560.f;
    }
    else {
        lumi_flush = (from_lumi - to_lumi) * 80.f;
        float inter_hsv_v = 0.67 * to_hsv_v + 0.33 * from_hsv_v;
        hs_dist = std::min(inter_hsv_v, hs_dist);
    }
    
    float hs_flush = 230.f * hs_dist;

    // Calcula usando teorema dos cossenos (triângulo com ângulo de 120°)
    float flush_volume = calc_triangle_3rd_edge(hs_flush, lumi_flush, 120.f);
    flush_volume = std::max(flush_volume, 60.f);

    flush_volume += m_min_flush_vol;
    return std::min((int)flush_volume, m_max_flush_vol);
}
```

**Explicação do Algoritmo**:
1. Converte cores RGB para HSV
2. Calcula distância no espaço HSV (matiz/saturação)
3. Calcula diferença de luminância
4. Combina os dois fatores usando geometria (triângulo 120°)
5. Aplica limites mínimo e máximo

---

## 2. WipeTowerCreality.hpp - Interface da Torre

### Arquivo: `src/libslic3r/FDM/WipeTowerCreality.hpp`

#### Declaração da Classe (Linhas 16-42)
```cpp
class WipeTowerCreality
{
public:
    // Construct ToolChangeResult from current state
    WipeTower::ToolChangeResult construct_tcr(
        WipeTowerWriterCreality& writer, 
        bool priming,
        size_t old_tool, 
        bool is_finish, 
        float purge_volume) const;

    WipeTowerCreality(
        const PrintConfig& config,
        const PrintRegionConfig& default_region_config,
        int plate_idx, 
        Vec3d plate_origin,
        const std::vector<std::vector<float>>& wiping_matrix,
        size_t initial_tool);

    void set_extruder(size_t idx, const PrintConfig& config);

    void plan_toolchange(
        float z_par,
        float layer_height_par,
        unsigned int old_tool,
        unsigned int new_tool,
        float wipe_volume  = 0.f,
        float purge_volume = 0.f);  // ← Parâmetro importante!

    void generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &result);
```

#### Parâmetros Privados (Linhas 165-175)
```cpp
private:
    float m_wipe_tower_max_purge_speed = 90.f;  // Velocidade máxima de purge
    float m_travel_speed       = 0.f;
    float m_infill_speed       = 0.f;
    float m_perimeter_speed    = 0.f;
    float m_first_layer_speed  = 0.f;
```

---

## 3. GCode.cpp - Geração de GCode com Purge

### Arquivo: `src/libslic3r/GCode.cpp`

#### Constantes de Purge (Linhas ~2100)
```cpp
static const float g_min_purge_volume = 100.f;
static const float g_purge_volume_one_time = 135.f;
```

#### Cálculo de Flush Count (Linhas ~2150-2180)
```cpp
float purge_volume = tcr.purge_volume < EPSILON ? 0 : 
                     std::max(tcr.purge_volume, g_min_purge_volume);

float purge_length = purge_volume / filament_area;

config.set_key_value("first_flush_volume", 
                     new ConfigOptionFloat(purge_length / 2.f));
config.set_key_value("second_flush_volume", 
                     new ConfigOptionFloat(purge_length / 2.f));

int flush_count = std::min(g_max_flush_count, 
                          (int)std::round(purge_volume / g_purge_volume_one_time));

float flush_unit = purge_length / flush_count;
```

**Lógica**:
- Garante volume mínimo de 100mm³
- Divide purge em unidades de 135mm³
- Configura first/second flush volumes

---

## 4. PrintConfig.cpp - Definições de Configuração

### Arquivo: `src/libslic3r/PrintConfig.cpp`

#### Nozzle Volume (Linha 3690)
```cpp
def = this->add("nozzle_volume", coFloat);
def->label = L("Nozzle volume");
def->tooltip = L("Volume of nozzle between the cutter and the end of nozzle");
def->sidetext = L("mm³");
def->mode = comAdvanced;
def->readonly = false;
def->set_default_value(new ConfigOptionFloat { 0.0 });
```

#### Creality Flush Time (Linha 3569)
```cpp
def = this->add("creality_flush_time", coFloat);
def->label = L("Creality flush time");
def->tooltip = L("Time required for flushing filament on Creality printers");
def->sidetext = L("s");
def->mode = comAdvanced;
def->set_default_value(new ConfigOptionFloat { 86.0 });
```

#### Wipe Tower Max Purge Speed (Linha 5504)
```cpp
def = this->add("wipe_tower_max_purge_speed", coFloat);
def->label = L("Maximum wipe tower print speed");
def->tooltip = L("The maximum print speed when purging in the wipe tower and printing the wipe tower sparse layers. "
                 "When purging, if the sparse infill speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                 "When printing the sparse layers, if the internal perimeter speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                 "Increasing this speed may affect the tower's stability as well as increase the force with which the nozzle collides with any blobs that may have formed on the wipe tower.\n\n"
                 "Before increasing this parameter beyond the default of 90mm/sec, make sure your printer can reliably bridge at the increased speeds and that ooze when tool changing is well controlled.\n\n"
                 "For the wipe tower external perimeters the internal perimeter speed is used regardless of this setting.");
def->sidetext = L("mm/s");
def->mode = comAdvanced;
def->min = 10;
def->set_default_value(new ConfigOptionFloat(90.));
```

#### Flush Volumes Matrix (Linha 5403)
```cpp
def = this->add("flush_volumes_matrix", coFloats);
def->label = L("Flush volumes matrix");
def->tooltip = L("Matrix of flush volumes between different filaments");
def->mode = comAdvanced;
```

#### Flush Multiplier (Linha 5413)
```cpp
def = this->add("flush_multiplier", coFloat);
def->label = L("Flush multiplier");
def->tooltip = L("Multiplier for flush volumes");
def->mode = comAdvanced;
def->min = 0.5;
def->max = 2.0;
def->set_default_value(new ConfigOptionFloat(1.0));
```

#### Multicolor Method (Linha 1718)
```cpp
def = this->add("multicolor_method", coBool);
def->label = L("Multicolor method");
def->tooltip = L("Use Creality-specific multicolor method");
def->mode = comAdvanced;
def->set_default_value(new ConfigOptionBool(false));
```

---

## 5. WipeTowerCreality.cpp - Implementação

### Arquivo: `src/libslic3r/FDM/WipeTowerCreality.cpp`

#### Suporte a Klipper (Linhas 140-150)
```cpp
WipeTowerWriterCreality& disable_linear_advance() {
    if (m_gcode_flavor == gcfRepRapSprinter || m_gcode_flavor == gcfRepRapFirmware)
        m_gcode += (std::string("M572 D") + std::to_string(m_current_tool) + " S0\n");
    else if (m_gcode_flavor == gcfKlipper)
        m_gcode += "SET_PRESSURE_ADVANCE ADVANCE=0\n";
    else
        m_gcode += "M900 K0\n";
    return *this;
}
```

#### Cálculo de Velocidade de Purge (Linha 855)
```cpp
const float target_speed = first_layer || (m_num_tool_changes <= 1 && m_no_sparse_layers) 
    ? m_first_layer_speed * 60.f 
    : std::min(m_wipe_tower_max_purge_speed * 60.f, m_infill_speed * 60.f);
```

#### Plan Toolchange (Linha 1232)
```cpp
void WipeTowerCreality::plan_toolchange(
    float z_par, 
    float layer_height_par, 
    unsigned int old_tool, 
    unsigned int new_tool, 
    float wipe_volume, 
    float purge_volume)
{
    // ... código de validação ...
    
    m_plan.back().tool_changes.push_back(
        WipeTowerInfo::ToolChange(
            old_tool, 
            new_tool, 
            depth, 
            0.0f, 
            0.0f, 
            wipe_volume, 
            purge_volume  // Armazenado para uso posterior
        )
    );
}
```

---

## 6. GCodeProcessor.cpp - Processamento de Tags

### Arquivo: `src/libslic3r/GCode/GCodeProcessor.cpp`

#### Processamento de Creality Flush Time (Linha 3399)
```cpp
if (boost::starts_with(comment, " creality_flush_time = ")) {
    auto data = comment.substr(strlen(" creality_flush_time = "));
    this->m_result.creality_flush_time = std::stof(data.data());
}
```

#### Cálculo de Tempo Extra (Linha 2272)
```cpp
float flush_times = s_creality_flush_time * m_result.print_statistics.total_filamentchanges;
```

#### Tempo Total com Flush (Linha 8335)
```cpp
float extra_time = m_result.print_statistics.total_filamentchanges * s_creality_flush_time;
```

---

## 📝 Notas de Implementação

### Ordem de Implementação Sugerida

1. **FlushVolCalc** - Implementar primeiro, é independente
2. **PrintConfig** - Adicionar novos parâmetros
3. **WipeTowerCreality** - Adaptar ou criar nova classe
4. **GCode** - Integrar cálculos de purge
5. **GCodeProcessor** - Adicionar processamento de tags

### Dependências

```
FlushVolCalc.cpp
    ↓
PrintConfig.cpp (nozzle_volume, flush params)
    ↓
WipeTowerCreality.cpp (usa flush calculator)
    ↓
GCode.cpp (gera gcode com purge)
    ↓
GCodeProcessor.cpp (processa e calcula tempo)
```

### Testes Necessários

1. **Teste de cálculo de flush**:
   - Branco → Preto: ~1200mm³
   - Preto → Branco: ~700mm³
   - Cores similares: ~300mm³

2. **Teste de geração de GCode**:
   - Verificar comandos Klipper
   - Validar volumes de purge
   - Conferir velocidades

3. **Teste de tempo**:
   - Comparar tempo estimado vs real
   - Validar cálculo com creality_flush_time

---

**Versão**: 1.0
**Data**: 2025-10-07

