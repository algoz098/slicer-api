# Exemplos Práticos - Integração Creality K2 Plus

## 📌 Exemplos de Uso e Testes

Este documento contém exemplos práticos de como os componentes funcionam juntos.

---

## 1. Exemplo de Cálculo de Flush Volume

### Cenário: Branco → Preto

```cpp
#include "FlushVolCalc.hpp"

// Configuração
FlushVolCalculator calculator(
    700,    // min_flush_vol
    1200,   // max_flush_vol
    1.0f    // multiplier
);

// Cores
unsigned char white_r = 255, white_g = 255, white_b = 255, white_a = 255;
unsigned char black_r = 0,   black_g = 0,   black_b = 0,   black_a = 255;

// Cálculo
int flush_volume = calculator.calc_flush_vol(
    white_a, white_r, white_g, white_b,  // Cor origem (branco)
    black_a, black_r, black_g, black_b   // Cor destino (preto)
);

// Resultado esperado: ~1200mm³ (máximo)
std::cout << "Flush volume: " << flush_volume << "mm³" << std::endl;
```

**Explicação**:
- Branco → Preto é a transição mais difícil
- Alta diferença de luminância
- Resultado próximo ao máximo (1200mm³)

---

### Cenário: Vermelho → Vermelho Claro

```cpp
// Cores similares
unsigned char red_r = 255, red_g = 0,   red_b = 0,   red_a = 255;
unsigned char light_red_r = 255, light_red_g = 128, light_red_b = 128, light_red_a = 255;

int flush_volume = calculator.calc_flush_vol(
    red_a, red_r, red_g, red_b,
    light_red_a, light_red_r, light_red_g, light_red_b
);

// Resultado esperado: ~750-850mm³
std::cout << "Flush volume: " << flush_volume << "mm³" << std::endl;
```

**Explicação**:
- Cores similares em matiz
- Diferença principalmente em saturação/valor
- Volume intermediário

---

## 2. Exemplo de Configuração de Impressora

### Arquivo: `Creality K2 Plus 0.4 nozzle.json`

```json
{
    "type": "machine",
    "from": "system",
    "printer_model": "Creality K2 Plus",
    "printer_variant": "0.4",
    
    // ========== PARÂMETROS CRÍTICOS ==========
    
    // Volume do hotend (3.6x maior que Bambu)
    "nozzle_volume": "183",
    
    // Tempo de flush por troca
    "creality_flush_time": "86.0",
    
    // Velocidade máxima de purge
    "wipe_tower_max_purge_speed": "90",
    
    // Método de multicor Creality
    "multicolor_method": "1",
    
    // Flavor de GCode
    "gcode_flavor": "klipper",
    
    // ========== VELOCIDADES ==========
    
    "machine_max_speed_x": "800",
    "machine_max_speed_y": "800",
    "machine_max_speed_z": "30",
    "machine_max_speed_e": "50",
    
    // ========== ACELERAÇÕES ==========
    
    "machine_max_acceleration_x": "30000",
    "machine_max_acceleration_y": "30000",
    "machine_max_acceleration_z": "1000",
    "machine_max_acceleration_e": "5000",
    "machine_max_acceleration_extruding": "30000",
    
    // ========== RETRAÇÃO ==========
    
    "retraction_length": "0.8",
    "retraction_speed": "40",
    "z_hop": "0.4",
    "wipe_distance": "2",
    
    // ========== GCODE CUSTOMIZADO ==========
    
    "machine_start_gcode": "M140 S0\nM104 S0\nSTART_PRINT EXTRUDER_TEMP=[nozzle_temperature_initial_layer] BED_TEMP=[bed_temperature_initial_layer_single]\nT[initial_no_support_extruder]\nM104 S[nozzle_temperature_initial_layer]\nM204 S2000\nG1 Z3 F600\nM83\nG1 Y150 F12000\nG1 X0 F12000\nG1 Z0.2 F600\nG1 X0 Y150 F6000\nG1 E0.8 F300\nG1 X0 Y0 E9 F{filament_max_volumetric_speed[initial_extruder]/0.3*60}\nG1 X150 Y0 E9 F{filament_max_volumetric_speed[initial_extruder]/0.3*60}\nG92 E0\nG1 Z1 F600",
    
    "machine_end_gcode": "END_PRINT",
    
    "change_filament_gcode": "G2 Z{z_after_toolchange + 0.4} I0.86 J0.86 P1 F10000 ; spiral lift\nG1 X0 Y245 F30000\nG1 Z{z_after_toolchange} F600"
}
```

---

## 3. Exemplo de Uso da WipeTower

### Planejamento de ToolChange

```cpp
#include "FDM/WipeTowerCreality.hpp"

// Criar torre
WipeTowerCreality tower(
    config,                  // PrintConfig
    default_region_config,   // PrintRegionConfig
    plate_idx,              // Índice da placa
    plate_origin,           // Origem da placa
    wiping_matrix,          // Matriz de limpeza
    initial_tool            // Ferramenta inicial
);

// Configurar extrusoras
for (size_t i = 0; i < num_extruders; i++) {
    tower.set_extruder(i, config);
}

// Planejar trocas de ferramenta
// Exemplo: Camada 1, trocar de T0 (branco) para T1 (preto)
float z = 0.2f;              // Altura da camada
float layer_height = 0.2f;   // Altura da camada
unsigned int old_tool = 0;   // T0 (branco)
unsigned int new_tool = 1;   // T1 (preto)

// Calcular volume de purge
FlushVolCalculator calc(700, 1200, 1.0f);
int purge_volume = calc.calc_flush_vol(
    255, 255, 255, 255,  // Branco
    255, 0, 0, 0         // Preto
);

// Planejar a troca
tower.plan_toolchange(
    z,                   // Z position
    layer_height,        // Layer height
    old_tool,           // Old tool
    new_tool,           // New tool
    0.0f,               // Wipe volume
    (float)purge_volume // Purge volume (1200mm³)
);

// Gerar GCode
std::vector<std::vector<WipeTower::ToolChangeResult>> results;
tower.generate(results);
```

---

## 4. Exemplo de GCode Gerado

### Troca de Ferramenta com Purge

```gcode
; ========== INÍCIO DA TROCA DE FERRAMENTA ==========
; Trocar de T0 (Branco) para T1 (Preto)
; Purge volume: 1200mm³

; Desabilitar pressure advance
SET_PRESSURE_ADVANCE ADVANCE=0

; Mover para posição de troca
G1 X50 Y50 F18000

; Trocar ferramenta
T1

; Esperar temperatura
M109 S210

; ========== PURGE DIVIDIDO ==========
; Total: 1200mm³ dividido em 9 flushes de 135mm³

; Flush 1/9
G1 E15.5 F300
G1 E-0.8 F2400

; Flush 2/9
G1 E15.5 F300
G1 E-0.8 F2400

; ... (continua até 9/9)

; Flush 9/9
G1 E15.5 F300
G1 E-0.8 F2400

; ========== LIMPEZA ==========
; Wipe na torre
G1 X55 Y50 F9000
G1 X55 Y55 E0.5 F5400
G1 X50 Y55 E0.5 F5400

; Retornar ao print
G1 Z0.6 F600
G1 X100 Y100 F18000

; ========== FIM DA TROCA ==========
```

**Análise**:
- Purge de 1200mm³ dividido em 9 partes de ~135mm³
- Cada flush tem retração para evitar oozing
- Velocidade controlada (300mm/min = 5mm/s)
- Tempo total: ~86 segundos

---

## 5. Exemplo de Cálculo de Tempo

### Estimativa de Tempo de Impressão

```cpp
#include "GCode/GCodeProcessor.hpp"

// Processar GCode
GCodeProcessor processor;
processor.process_file("output.gcode", config);

// Obter estatísticas
const auto& stats = processor.get_result().print_statistics;

// Tempo base
float base_time = stats.estimated_print_time;

// Tempo de flush
int num_changes = stats.total_filamentchanges;
float flush_time = num_changes * 86.0f;  // creality_flush_time

// Tempo total
float total_time = base_time + flush_time;

std::cout << "Tempo base: " << base_time << "s" << std::endl;
std::cout << "Trocas de filamento: " << num_changes << std::endl;
std::cout << "Tempo de flush: " << flush_time << "s" << std::endl;
std::cout << "Tempo total: " << total_time << "s" << std::endl;
```

**Exemplo de saída**:
```
Tempo base: 3600s (1h)
Trocas de filamento: 25
Tempo de flush: 2150s (35min 50s)
Tempo total: 5750s (1h 35min 50s)
```

---

## 6. Exemplo de Teste Unitário

### Teste de FlushVolCalculator

```cpp
#include <gtest/gtest.h>
#include "FlushVolCalc.hpp"

TEST(FlushVolCalculatorTest, WhiteToBlack) {
    FlushVolCalculator calc(700, 1200, 1.0f);
    
    int volume = calc.calc_flush_vol(
        255, 255, 255, 255,  // Branco
        255, 0, 0, 0         // Preto
    );
    
    // Deve estar próximo ao máximo
    EXPECT_GE(volume, 1100);
    EXPECT_LE(volume, 1200);
}

TEST(FlushVolCalculatorTest, BlackToWhite) {
    FlushVolCalculator calc(700, 1200, 1.0f);
    
    int volume = calc.calc_flush_vol(
        255, 0, 0, 0,        // Preto
        255, 255, 255, 255   // Branco
    );
    
    // Deve estar próximo ao mínimo
    EXPECT_GE(volume, 700);
    EXPECT_LE(volume, 800);
}

TEST(FlushVolCalculatorTest, SimilarColors) {
    FlushVolCalculator calc(700, 1200, 1.0f);
    
    int volume = calc.calc_flush_vol(
        255, 255, 0, 0,      // Vermelho
        255, 255, 50, 50     // Vermelho claro
    );
    
    // Deve estar no meio
    EXPECT_GE(volume, 750);
    EXPECT_LE(volume, 900);
}

TEST(FlushVolCalculatorTest, TransparentMaterial) {
    FlushVolCalculator calc(700, 1200, 1.0f);
    
    int volume = calc.calc_flush_vol(
        0, 100, 100, 100,    // Transparente (tratado como branco)
        255, 0, 0, 0         // Preto
    );
    
    // Deve ser similar a branco → preto
    EXPECT_GE(volume, 1100);
    EXPECT_LE(volume, 1200);
}
```

---

## 7. Exemplo de Matriz de Flush

### Comparação: Manual vs Calculado

```cpp
// Matriz manual (OrcaSlicer atual)
std::vector<std::vector<float>> manual_matrix = {
    //   T0    T1    T2    T3
    {    0,   800,  800,  800 },  // De T0
    {  800,    0,   800,  800 },  // De T1
    {  800,  800,    0,   800 },  // De T2
    {  800,  800,  800,    0  }   // De T3
};

// Matriz calculada (CrealityPrint)
FlushVolCalculator calc(700, 1200, 1.0f);

// Cores dos filamentos
struct Color { unsigned char a, r, g, b; };
std::vector<Color> colors = {
    {255, 255, 255, 255},  // T0: Branco
    {255, 0,   0,   0  },  // T1: Preto
    {255, 255, 0,   0  },  // T2: Vermelho
    {255, 0,   0,   255}   // T3: Azul
};

// Calcular matriz
std::vector<std::vector<int>> calculated_matrix(4, std::vector<int>(4));
for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
        if (i == j) {
            calculated_matrix[i][j] = 0;
        } else {
            calculated_matrix[i][j] = calc.calc_flush_vol(
                colors[i].a, colors[i].r, colors[i].g, colors[i].b,
                colors[j].a, colors[j].r, colors[j].g, colors[j].b
            );
        }
    }
}

// Resultado:
//        T0     T1     T2     T3
// T0:    0    1200    850    900
// T1:  700      0     950   1000
// T2:  800    1150     0     850
// T3:  850    1100    900     0
```

**Análise**:
- Matriz manual: Todos 800mm³ (exceto diagonal)
- Matriz calculada: Otimizada por cor
- Economia: ~15-20% de material em média

---

## 8. Exemplo de Debug

### Logging de Purge

```cpp
#include "FlushVolCalc.hpp"
#include <iostream>
#include <iomanip>

void debug_flush_calculation(
    const char* from_name, unsigned char from_r, unsigned char from_g, unsigned char from_b,
    const char* to_name, unsigned char to_r, unsigned char to_g, unsigned char to_b)
{
    FlushVolCalculator calc(700, 1200, 1.0f);
    
    int volume = calc.calc_flush_vol(
        255, from_r, from_g, from_b,
        255, to_r, to_g, to_b
    );
    
    std::cout << std::setw(15) << from_name 
              << " → " 
              << std::setw(15) << to_name 
              << ": " 
              << std::setw(4) << volume 
              << "mm³" 
              << std::endl;
}

int main() {
    std::cout << "=== Flush Volume Calculator Debug ===" << std::endl;
    
    debug_flush_calculation("Branco", 255, 255, 255, "Preto", 0, 0, 0);
    debug_flush_calculation("Preto", 0, 0, 0, "Branco", 255, 255, 255);
    debug_flush_calculation("Vermelho", 255, 0, 0, "Azul", 0, 0, 255);
    debug_flush_calculation("Amarelo", 255, 255, 0, "Magenta", 255, 0, 255);
    debug_flush_calculation("Verde", 0, 255, 0, "Laranja", 255, 128, 0);
    
    return 0;
}
```

**Saída esperada**:
```
=== Flush Volume Calculator Debug ===
         Branco → Preto          : 1200mm³
          Preto → Branco         :  700mm³
       Vermelho → Azul           :  950mm³
        Amarelo → Magenta        :  880mm³
          Verde → Laranja        :  820mm³
```

---

## 📝 Notas Finais

### Dicas de Implementação

1. **Sempre validar limites**:
   ```cpp
   purge_volume = std::max(min_vol, std::min(purge_volume, max_vol));
   ```

2. **Logar valores para debug**:
   ```cpp
   BOOST_LOG_TRIVIAL(debug) << "Purge volume: " << purge_volume << "mm³";
   ```

3. **Testar com cores extremas primeiro**:
   - Branco ↔ Preto
   - Cores complementares (Vermelho ↔ Ciano)

4. **Permitir override manual**:
   ```cpp
   if (config.has("manual_flush_volume")) {
       purge_volume = config.get_float("manual_flush_volume");
   }
   ```

---

**Versão**: 1.0
**Data**: 2025-10-07

