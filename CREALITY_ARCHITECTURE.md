# Arquitetura do Sistema de Multicor - CrealityPrint

## 🏗️ Visão Geral da Arquitetura

```
┌─────────────────────────────────────────────────────────────────┐
│                        CREALITY PRINT                            │
│                     Sistema de Multicor                          │
└─────────────────────────────────────────────────────────────────┘
                                │
                ┌───────────────┴───────────────┐
                │                               │
        ┌───────▼────────┐            ┌────────▼────────┐
        │  Configuração  │            │   Processamento │
        │   (Config)     │            │     (Runtime)   │
        └───────┬────────┘            └────────┬────────┘
                │                               │
    ┌───────────┼───────────┐       ┌──────────┼──────────┐
    │           │           │       │          │          │
┌───▼───┐  ┌───▼───┐  ┌───▼───┐ ┌─▼──┐  ┌────▼────┐ ┌──▼───┐
│Printer│  │Filam. │  │Process│ │Calc│  │WipeTower│ │GCode │
│Profile│  │Profile│  │Profile│ │Flush│  │         │ │Gen   │
└───────┘  └───────┘  └───────┘ └─┬──┘  └────┬────┘ └──┬───┘
                                   │          │         │
                                   └──────────┴─────────┘
                                              │
                                    ┌─────────▼─────────┐
                                    │   GCode Output    │
                                    │  (com purge tags) │
                                    └───────────────────┘
```

---

## 🔄 Fluxo de Processamento

### 1. Inicialização

```
Usuário seleciona impressora K2 Plus
            │
            ▼
Carrega PrintConfig
            │
            ├─► nozzle_volume = 183mm³
            ├─► creality_flush_time = 86s
            ├─► wipe_tower_max_purge_speed = 90mm/s
            └─► multicolor_method = 1
            │
            ▼
Cria FlushVolCalculator(min=700, max=1200)
            │
            ▼
Cria WipeTowerCreality
```

---

### 2. Planejamento de Camadas

```
Para cada camada:
    │
    ├─► Identifica trocas de ferramenta necessárias
    │
    ├─► Para cada troca (T_old → T_new):
    │       │
    │       ├─► Obtém cores dos filamentos
    │       │       │
    │       │       ├─► Color_old (R, G, B, A)
    │       │       └─► Color_new (R, G, B, A)
    │       │
    │       ├─► Calcula purge_volume
    │       │       │
    │       │       └─► FlushVolCalculator.calc_flush_vol()
    │       │               │
    │       │               ├─► RGB → HSV
    │       │               ├─► Calcula distância HSV
    │       │               ├─► Calcula diferença luminância
    │       │               └─► Combina fatores → volume
    │       │
    │       └─► WipeTower.plan_toolchange(
    │               z, layer_height, 
    │               old_tool, new_tool,
    │               wipe_volume, purge_volume
    │           )
    │
    └─► Continua para próxima camada
```

---

### 3. Geração de GCode

```
WipeTower.generate()
    │
    ├─► Para cada toolchange planejado:
    │       │
    │       ├─► Calcula flush_count
    │       │       │
    │       │       └─► purge_volume / 135mm³
    │       │
    │       ├─► Divide purge em múltiplos flushes
    │       │       │
    │       │       └─► flush_unit = purge_length / flush_count
    │       │
    │       ├─► Gera GCode de troca:
    │       │       │
    │       │       ├─► Desabilita pressure advance
    │       │       ├─► Move para posição
    │       │       ├─► Troca ferramenta (T_new)
    │       │       ├─► Espera temperatura
    │       │       ├─► Executa flushes (N vezes):
    │       │       │       │
    │       │       │       ├─► Extrude flush_unit
    │       │       │       └─► Retrai
    │       │       │
    │       │       ├─► Limpa na torre
    │       │       └─► Retorna ao print
    │       │
    │       └─► Adiciona tags para processamento:
    │               │
    │               ├─► ; creality_flush_time = 86.0
    │               └─► ; Width = X.XX
    │
    └─► Retorna ToolChangeResult
```

---

### 4. Processamento Final

```
GCodeProcessor.process()
    │
    ├─► Lê GCode linha por linha
    │
    ├─► Processa tags:
    │       │
    │       ├─► ; creality_flush_time = 86.0
    │       │       └─► Armazena para cálculo de tempo
    │       │
    │       └─► ; Width = X.XX
    │               └─► Atualiza estatísticas
    │
    ├─► Calcula estatísticas:
    │       │
    │       ├─► total_filamentchanges
    │       ├─► flush_time = changes × 86s
    │       └─► total_time = base_time + flush_time
    │
    └─► Retorna resultado com estatísticas
```

---

## 🧮 Algoritmo de Cálculo de Flush (Detalhado)

```
calc_flush_vol(src_color, dst_color):
    │
    ├─► 1. Normalizar cores
    │       │
    │       ├─► Se transparente (A=0) → Branco (255,255,255)
    │       └─► RGB [0-255] → [0.0-1.0]
    │
    ├─► 2. Converter para HSV
    │       │
    │       ├─► RGB2HSV(src) → (H1, S1, V1)
    │       └─► RGB2HSV(dst) → (H2, S2, V2)
    │
    ├─► 3. Calcular distância HSV
    │       │
    │       └─► DeltaHS_BBS(H1,S1,V1, H2,S2,V2):
    │               │
    │               ├─► dx = cos(H1)×S1×V1 - cos(H2)×S2×V2
    │               ├─► dy = sin(H1)×S1×V1 - sin(H2)×S2×V2
    │               ├─► dxy = √(dx² + dy²)
    │               └─► return min(1.2, dxy)
    │
    ├─► 4. Calcular componente de luminância
    │       │
    │       ├─► lumi_src = 0.3×R + 0.59×G + 0.11×B
    │       ├─► lumi_dst = 0.3×R + 0.59×G + 0.11×B
    │       │
    │       └─► Se lumi_dst >= lumi_src:
    │           │   └─► lumi_flush = (lumi_dst - lumi_src)^0.7 × 560
    │           └─► Senão:
    │               ├─► lumi_flush = (lumi_src - lumi_dst) × 80
    │               └─► hs_dist = min(inter_v, hs_dist)
    │
    ├─► 5. Combinar componentes
    │       │
    │       ├─► hs_flush = 230 × hs_dist
    │       └─► flush_vol = calc_triangle_3rd_edge(
    │               hs_flush, lumi_flush, 120°
    │           )
    │
    ├─► 6. Aplicar limites
    │       │
    │       ├─► flush_vol = max(flush_vol, 60)
    │       ├─► flush_vol += min_flush_vol (700)
    │       └─► flush_vol = min(flush_vol, max_flush_vol) (1200)
    │
    └─► return flush_vol
```

---

## 📦 Estrutura de Classes

### FlushVolCalculator

```cpp
class FlushVolCalculator {
private:
    int m_min_flush_vol;    // 700mm³
    int m_max_flush_vol;    // 1200mm³
    float m_multiplier;     // 1.0

public:
    FlushVolCalculator(int min, int max, float mult);
    
    int calc_flush_vol(
        unsigned char src_a, src_r, src_g, src_b,
        unsigned char dst_a, dst_r, dst_g, dst_b
    );
};
```

---

### WipeTowerCreality

```cpp
class WipeTowerCreality {
private:
    // Configuração
    float m_wipe_tower_max_purge_speed;  // 90mm/s
    float m_wipe_tower_width;
    float m_wipe_tower_depth;
    Vec2f m_wipe_tower_pos;
    
    // Estado
    std::vector<WipeTowerInfo> m_plan;
    size_t m_current_tool;
    float m_z_pos;
    
public:
    WipeTowerCreality(
        const PrintConfig& config,
        int plate_idx,
        Vec3d plate_origin,
        const std::vector<std::vector<float>>& wiping_matrix,
        size_t initial_tool
    );
    
    void plan_toolchange(
        float z,
        float layer_height,
        unsigned int old_tool,
        unsigned int new_tool,
        float wipe_volume,
        float purge_volume  // ← Calculado por FlushVolCalculator
    );
    
    void generate(
        std::vector<std::vector<WipeTower::ToolChangeResult>>& result
    );
    
    WipeTower::ToolChangeResult construct_tcr(
        WipeTowerWriterCreality& writer,
        bool priming,
        size_t old_tool,
        bool is_finish,
        float purge_volume
    ) const;
};
```

---

## 🔗 Dependências entre Componentes

```
PrintConfig
    │
    ├─► nozzle_volume ──────────┐
    ├─► creality_flush_time ────┼──► GCodeProcessor
    ├─► wipe_tower_max_purge ───┼──► WipeTowerCreality
    └─► multicolor_method ──────┘
                                │
FlushVolCalculator ◄────────────┤
    │                           │
    └─► purge_volume ──────────►│
                                │
WipeTowerCreality ◄─────────────┤
    │                           │
    └─► ToolChangeResult ───────►│
                                │
GCode Generator ◄───────────────┤
    │                           │
    └─► GCode with tags ────────►│
                                │
GCodeProcessor ◄────────────────┘
    │
    └─► Statistics + Time
```

---

## 📊 Fluxo de Dados

### Entrada → Saída

```
┌──────────────────┐
│  Cores (RGBA)    │
│  - Filamento 1   │
│  - Filamento 2   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ FlushVolCalc     │
│ calc_flush_vol() │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ purge_volume     │
│ (700-1200mm³)    │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ WipeTower        │
│ plan_toolchange()│
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ flush_count      │
│ = vol / 135mm³   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ GCode Generator  │
│ N × flush_unit   │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ GCode Output     │
│ + Tags           │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ GCodeProcessor   │
│ + Statistics     │
└────────┬─────────┘
         │
         ▼
┌──────────────────┐
│ Final Output     │
│ - GCode file     │
│ - Time estimate  │
│ - Material used  │
└──────────────────┘
```

---

## 🎨 Exemplo de Fluxo Completo

### Cenário: Imprimir objeto com 2 cores (Branco e Preto)

```
1. CONFIGURAÇÃO
   ├─► Impressora: K2 Plus
   ├─► Nozzle: 0.4mm (volume: 183mm³)
   ├─► Filamento 1: Branco (255,255,255)
   └─► Filamento 2: Preto (0,0,0)

2. SLICE
   ├─► Camada 1: T0 (Branco)
   ├─► Camada 2: T1 (Preto) ← Troca necessária
   └─► Camada 3: T0 (Branco) ← Troca necessária

3. CÁLCULO DE PURGE
   ├─► Branco → Preto:
   │   └─► calc_flush_vol() = 1200mm³
   └─► Preto → Branco:
       └─► calc_flush_vol() = 700mm³

4. PLANEJAMENTO
   ├─► Camada 2 (Z=0.4mm):
   │   └─► plan_toolchange(0.4, 0.2, T0, T1, 0, 1200)
   └─► Camada 3 (Z=0.6mm):
       └─► plan_toolchange(0.6, 0.2, T1, T0, 0, 700)

5. GERAÇÃO DE GCODE
   ├─► Troca 1 (Branco → Preto):
   │   ├─► flush_count = 1200 / 135 = 9 flushes
   │   └─► 9 × (Extrude 15.5mm + Retrai 0.8mm)
   └─► Troca 2 (Preto → Branco):
       ├─► flush_count = 700 / 135 = 6 flushes
       └─► 6 × (Extrude 15.5mm + Retrai 0.8mm)

6. PROCESSAMENTO
   ├─► total_filamentchanges = 2
   ├─► flush_time = 2 × 86s = 172s
   └─► total_time = base_time + 172s

7. OUTPUT
   └─► GCode file com:
       ├─► Comandos de troca
       ├─► Tags de processamento
       └─► Estimativa de tempo correta
```

---

**Versão**: 1.0  
**Data**: 2025-10-07

