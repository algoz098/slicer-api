# 🔧 OrcaSlicer CLI - Guia de Perfis

## Scripts de Teste Disponíveis

### 1. `test_simple.sh` - Script Simples
Script básico para teste rápido com perfis.

**Uso padrão (usa perfis do 3DBenchy.gcode de referência):**
```bash
./test_simple.sh
```

**Uso com perfis customizados:**
```bash
./test_simple.sh "PRINTER" "FILAMENT" "PROCESS"
```

**Exemplos:**
```bash
# Usar perfis padrão (mesmos do arquivo de referência)
./test_simple.sh

# Usar PLA Basic em vez de Matte
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Basic @BBL X1C" "0.20mm Standard @BBL X1C"

# Usar qualidade Fine
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Matte @BBL X1C" "0.15mm Fine @BBL X1C"

# Usar ABS
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu ABS @BBL X1C" "0.20mm Standard @BBL X1C"
```

### 2. `test_slice.sh` - Script Completo com Comparação
Script completo que inclui comparação detalhada com arquivo de referência.

**Uso:**
```bash
./test_slice.sh [PRINTER] [FILAMENT] [PROCESS]
```

## Perfis Padrão

Os scripts usam por padrão os **mesmos perfis** do arquivo `3DBenchy.gcode` de referência:

- **Printer**: `Bambu Lab X1 Carbon 0.4 nozzle`
- **Filament**: `Bambu PLA Matte @BBL X1C`
- **Process**: `0.20mm Standard @BBL X1C`

## Perfis Disponíveis

### 🖨️ Printers Comuns:
- `Bambu Lab X1 Carbon 0.4 nozzle`
- `Bambu Lab X1 Carbon`
- `Bambu Lab X1 0.4 nozzle`
- `Bambu Lab P1S 0.4 nozzle`

### 🧵 Filaments Comuns:
- `Bambu PLA Matte @BBL X1C`
- `Bambu PLA Basic @BBL X1C`
- `Bambu ABS @BBL X1C`
- `Bambu PETG Basic @BBL X1C`

### ⚙️ Processes Comuns:
- `0.20mm Standard @BBL X1C`
- `0.15mm Fine @BBL X1C`
- `0.28mm Draft @BBL X1C`
- `0.10mm Extra Fine @BBL X1C`

## Listar Perfis Disponíveis

```bash
# Listar todos os printers
cd build && ./bin/orcaslicer-cli list-profiles --type printer

# Listar todos os filaments
cd build && ./bin/orcaslicer-cli list-profiles --type filament

# Listar todos os processes
cd build && ./bin/orcaslicer-cli list-profiles --type process
```

## Arquivos de Saída

- **test_simple.sh**: `../output_files/test_slice.gcode`
- **test_slice.sh**: `../output_files/test_with_profiles.gcode`

## Exemplo de Teste Completo

```bash
# 1. Teste com perfis padrão (recomendado)
./test_simple.sh

# 2. Teste com comparação detalhada
./test_slice.sh

# 3. Teste com perfil diferente
./test_simple.sh "Bambu Lab X1 Carbon 0.4 nozzle" "Bambu PLA Basic @BBL X1C" "0.15mm Fine @BBL X1C"
```
