# OrcaSlicerCli - Exemplos de Uso Básico

Este documento demonstra como usar o OrcaSlicerCli para tarefas comuns de slicing e manipulação de modelos 3D.

## Instalação e Configuração

### Compilação
```bash
# Clone o repositório
git clone <repository-url>
cd orcaslicer-addon/OrcaSlicerCli

# Compile o projeto
./scripts/build.sh

# Verifique a instalação
./install/bin/orcaslicer-cli --help
```

### Configuração do PATH (Opcional)
```bash
# Adicione ao seu .bashrc ou .zshrc
export PATH="$PATH:/caminho/para/orcaslicer-addon/OrcaSlicerCli/install/bin"

# Agora você pode usar diretamente
orcaslicer-cli --help
```

## Comandos Básicos

### 1. Ajuda e Informações

```bash
# Ajuda geral
orcaslicer-cli --help

# Ajuda para comando específico
orcaslicer-cli slice --help
orcaslicer-cli info --help

# Versão do software
orcaslicer-cli version
```

### 2. Informações sobre Modelos

```bash
# Obter informações básicas sobre um modelo
orcaslicer-cli info --input modelo.stl

# Exemplo de saída:
# Model Information:
#   File: modelo.stl
#   Valid: Yes
#   Objects: 1
#   Triangles: 15420
#   Volume: 12345.67 mm³
#   Bounding Box: (50.0 x 30.0 x 20.0)
```

### 3. Slicing Básico

```bash
# Slice simples com configurações padrão
orcaslicer-cli slice --input modelo.stl --output modelo.gcode

# Slice com preset específico
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --preset quality

# Slice com arquivo de configuração customizado
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --config minha_config.ini
```

## Exemplos Práticos

### Exemplo 1: Workflow Básico de Slicing

```bash
#!/bin/bash
# script_slice_basico.sh

MODEL_FILE="meu_modelo.stl"
OUTPUT_FILE="meu_modelo.gcode"

echo "Verificando modelo..."
if orcaslicer-cli info --input "$MODEL_FILE"; then
    echo "Modelo válido, iniciando slicing..."
    orcaslicer-cli slice --input "$MODEL_FILE" --output "$OUTPUT_FILE"
    echo "Slicing concluído: $OUTPUT_FILE"
else
    echo "Erro: Modelo inválido"
    exit 1
fi
```

### Exemplo 2: Processamento com Validação

```bash
#!/bin/bash
# script_slice_com_validacao.sh

MODEL_DIR="./modelos"
OUTPUT_DIR="./gcodes"

mkdir -p "$OUTPUT_DIR"

for model in "$MODEL_DIR"/*.stl; do
    if [[ -f "$model" ]]; then
        basename=$(basename "$model" .stl)
        output="$OUTPUT_DIR/${basename}.gcode"
        
        echo "Processando: $model"
        
        # Validar modelo primeiro
        if orcaslicer-cli info --input "$model" --quiet; then
            # Slice se válido
            orcaslicer-cli slice --input "$model" --output "$output" --verbose
            echo "✓ Concluído: $output"
        else
            echo "✗ Erro: Modelo inválido - $model"
        fi
    fi
done
```

### Exemplo 3: Teste de Configuração (Dry Run)

```bash
# Testar configuração sem fazer slicing real
orcaslicer-cli slice --input modelo.stl --output teste.gcode --dry-run

# Útil para validar parâmetros antes do slicing real
orcaslicer-cli slice \
    --input modelo_complexo.stl \
    --output teste.gcode \
    --config configuracao_experimental.ini \
    --dry-run \
    --verbose
```

## Opções Avançadas

### Controle de Logging

```bash
# Saída silenciosa (apenas erros)
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --quiet

# Saída verbosa (debug)
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --verbose

# Nível de log específico
orcaslicer-cli --log-level debug slice --input modelo.stl --output modelo.gcode
```

### Configurações Customizadas

```bash
# Usando arquivo de configuração
orcaslicer-cli slice \
    --input modelo.stl \
    --output modelo.gcode \
    --config ./configs/alta_qualidade.ini

# Usando preset predefinido
orcaslicer-cli slice \
    --input modelo.stl \
    --output modelo.gcode \
    --preset speed
```

## Formatos de Arquivo Suportados

### Entrada (Modelos 3D)
- **STL**: Formato padrão para impressão 3D
- **3MF**: Formato Microsoft 3D Manufacturing
- **OBJ**: Formato Wavefront OBJ

### Saída
- **G-code**: Código de máquina para impressoras 3D

### Exemplos por Formato

```bash
# STL
orcaslicer-cli slice --input modelo.stl --output modelo.gcode

# 3MF (com múltiplos objetos)
orcaslicer-cli slice --input projeto.3mf --output projeto.gcode

# OBJ
orcaslicer-cli slice --input escultura.obj --output escultura.gcode
```

## Integração com Scripts

### Makefile
```makefile
# Makefile para automatizar slicing

MODELS_DIR = models
GCODE_DIR = gcode
MODELS = $(wildcard $(MODELS_DIR)/*.stl)
GCODES = $(MODELS:$(MODELS_DIR)/%.stl=$(GCODE_DIR)/%.gcode)

all: $(GCODES)

$(GCODE_DIR)/%.gcode: $(MODELS_DIR)/%.stl
	@mkdir -p $(GCODE_DIR)
	orcaslicer-cli slice --input $< --output $@

clean:
	rm -rf $(GCODE_DIR)

.PHONY: all clean
```

### Python Script
```python
#!/usr/bin/env python3
# slice_batch.py

import subprocess
import os
import sys
from pathlib import Path

def slice_model(input_file, output_file, preset="quality"):
    """Slice um modelo usando orcaslicer-cli"""
    cmd = [
        "orcaslicer-cli", "slice",
        "--input", str(input_file),
        "--output", str(output_file),
        "--preset", preset
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, check=True)
        print(f"✓ Sliced: {input_file} -> {output_file}")
        return True
    except subprocess.CalledProcessError as e:
        print(f"✗ Error slicing {input_file}: {e.stderr}")
        return False

def main():
    if len(sys.argv) < 2:
        print("Usage: python slice_batch.py <models_directory>")
        sys.exit(1)
    
    models_dir = Path(sys.argv[1])
    output_dir = models_dir / "gcode"
    output_dir.mkdir(exist_ok=True)
    
    for model_file in models_dir.glob("*.stl"):
        output_file = output_dir / f"{model_file.stem}.gcode"
        slice_model(model_file, output_file)

if __name__ == "__main__":
    main()
```

## Troubleshooting

### Problemas Comuns

**Erro: "File not found"**
```bash
# Verificar se o arquivo existe
ls -la modelo.stl

# Usar caminho absoluto
orcaslicer-cli slice --input /caminho/completo/modelo.stl --output modelo.gcode
```

**Erro: "Invalid file format"**
```bash
# Verificar informações do arquivo
file modelo.stl
orcaslicer-cli info --input modelo.stl
```

**Erro: "Slicing failed"**
```bash
# Usar modo verboso para mais detalhes
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --verbose

# Testar com dry-run primeiro
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --dry-run
```

### Debug e Logs

```bash
# Logs detalhados
orcaslicer-cli --log-level debug slice --input modelo.stl --output modelo.gcode

# Salvar logs em arquivo
orcaslicer-cli slice --input modelo.stl --output modelo.gcode --verbose 2>&1 | tee slice.log
```

## Próximos Passos

1. **Explore configurações avançadas**: Crie arquivos de configuração personalizados
2. **Automatize workflows**: Use scripts para processamento em lote
3. **Integre com pipelines**: Adicione ao seu sistema de build/CI
4. **Contribua**: Reporte bugs e sugira melhorias

Para mais informações, consulte:
- [Documentação de Desenvolvimento](../docs/DEVELOPMENT.md)
- [README Principal](../README.md)
- [Issues no GitHub](link-para-issues)
