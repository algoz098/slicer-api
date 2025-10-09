# Exemplos Práticos: Testando Sistemas de Rede

## 🎯 Objetivo

Este documento fornece exemplos práticos para testar e entender como funcionam os sistemas de envio de arquivos para impressoras.

---

## 🧪 Testando Moonraker (K2 Plus)

### 1. Verificar Conexão

```bash
# Testar se Moonraker está acessível
curl -X GET http://k2plus.local:7125/api/version

# Resposta esperada:
{
    "result": {
        "version": "v0.8.0-123-g1234567",
        "klippy_connected": true,
        "klippy_state": "ready",
        "components": ["moonraker", "klipper", "power", "file_manager"]
    }
}
```

---

### 2. Listar Arquivos

```bash
# Listar arquivos GCode disponíveis
curl -X GET http://k2plus.local:7125/api/server/files/list \
  -H "X-Api-Key: YOUR_API_KEY"

# Resposta esperada:
{
    "result": [
        {
            "path": "model1.gcode",
            "modified": 1704672000.0,
            "size": 1234567,
            "permissions": "rw"
        },
        {
            "path": "model2.gcode",
            "modified": 1704658800.0,
            "size": 987654,
            "permissions": "rw"
        }
    ]
}
```

---

### 3. Upload de Arquivo

```bash
# Upload simples (sem iniciar impressão)
curl -X POST http://k2plus.local:7125/api/files/local \
  -H "X-Api-Key: YOUR_API_KEY" \
  -F "file=@/path/to/model.gcode" \
  -F "filename=model.gcode"

# Resposta esperada:
{
    "result": "success",
    "item": {
        "path": "model.gcode",
        "root": "gcodes"
    }
}
```

---

### 4. Upload e Iniciar Impressão

```bash
# Upload e iniciar automaticamente
curl -X POST http://k2plus.local:7125/api/files/local \
  -H "X-Api-Key: YOUR_API_KEY" \
  -F "file=@/path/to/model.gcode" \
  -F "filename=model.gcode" \
  -F "select=true" \
  -F "print=true"

# Resposta esperada:
{
    "result": "success",
    "item": {
        "path": "model.gcode",
        "root": "gcodes"
    },
    "print_started": true
}
```

---

### 5. Monitorar Status

```bash
# Status da impressora
curl -X GET http://k2plus.local:7125/api/printer/objects/query?heater_bed&extruder&print_stats \
  -H "X-Api-Key: YOUR_API_KEY"

# Resposta esperada:
{
    "result": {
        "status": {
            "heater_bed": {
                "temperature": 60.5,
                "target": 60.0,
                "power": 0.5
            },
            "extruder": {
                "temperature": 210.3,
                "target": 210.0,
                "power": 0.3
            },
            "print_stats": {
                "state": "printing",
                "filename": "model.gcode",
                "total_duration": 1234.5,
                "print_duration": 1200.0,
                "filament_used": 1500.0
            }
        }
    }
}
```

---

### 6. Controlar Impressão

```bash
# Pausar impressão
curl -X POST http://k2plus.local:7125/api/printer/print/pause \
  -H "X-Api-Key: YOUR_API_KEY"

# Retomar impressão
curl -X POST http://k2plus.local:7125/api/printer/print/resume \
  -H "X-Api-Key: YOUR_API_KEY"

# Cancelar impressão
curl -X POST http://k2plus.local:7125/api/printer/print/cancel \
  -H "X-Api-Key: YOUR_API_KEY"
```

---

## 🔐 Obtendo API Key

### Via Mainsail/Fluidd

1. Abra navegador: `http://k2plus.local`
2. Vá em **Settings** → **API Key**
3. Clique em **Generate New Key**
4. Copie a chave gerada

---

### Via SSH (Avançado)

```bash
# Conectar via SSH
ssh pi@k2plus.local

# Editar configuração do Moonraker
nano ~/printer_data/config/moonraker.conf

# Adicionar seção:
[authorization]
trusted_clients:
    192.168.1.0/24
    127.0.0.1
cors_domains:
    *

# Reiniciar Moonraker
sudo systemctl restart moonraker

# Gerar API Key via comando
curl -X POST http://localhost:7125/access/api_key \
  -H "Content-Type: application/json" \
  -d '{"username": "my_app"}'
```

---

## 🐛 Debug e Troubleshooting

### 1. Verificar Logs do Moonraker

```bash
# Via SSH
ssh pi@k2plus.local
tail -f ~/printer_data/logs/moonraker.log

# Procurar por erros
grep -i "error\|warning" ~/printer_data/logs/moonraker.log | tail -20
```

---

### 2. Testar Descoberta mDNS

```bash
# macOS/Linux
dns-sd -B _http._tcp local.

# Ou usar avahi-browse
avahi-browse -a -t -r

# Procurar por:
# k2plus.local
# moonraker.local
```

---

### 3. Verificar Conectividade

```bash
# Ping
ping k2plus.local

# Verificar porta
nc -zv k2plus.local 7125

# Ou telnet
telnet k2plus.local 7125
```

---

### 4. Capturar Tráfego HTTP

```bash
# Usar tcpdump
sudo tcpdump -i any -A 'host k2plus.local and port 7125'

# Ou Wireshark
# Filtro: http and ip.addr == 192.168.1.100
```

---

## 📝 Script Completo de Upload

### Bash Script

```bash
#!/bin/bash
# upload_to_k2.sh

# Configuração
PRINTER_HOST="k2plus.local"
PRINTER_PORT="7125"
API_KEY="YOUR_API_KEY_HERE"

# Validar argumentos
if [ $# -eq 0 ]; then
    echo "Uso: $0 <arquivo.gcode> [--print]"
    exit 1
fi

GCODE_FILE="$1"
START_PRINT="false"

if [ "$2" == "--print" ]; then
    START_PRINT="true"
fi

# Validar arquivo
if [ ! -f "$GCODE_FILE" ]; then
    echo "Erro: Arquivo não encontrado: $GCODE_FILE"
    exit 1
fi

# Extrair nome do arquivo
FILENAME=$(basename "$GCODE_FILE")

echo "Enviando $FILENAME para $PRINTER_HOST..."

# Upload
if [ "$START_PRINT" == "true" ]; then
    # Upload e iniciar impressão
    RESPONSE=$(curl -s -X POST "http://${PRINTER_HOST}:${PRINTER_PORT}/api/files/local" \
      -H "X-Api-Key: ${API_KEY}" \
      -F "file=@${GCODE_FILE}" \
      -F "filename=${FILENAME}" \
      -F "select=true" \
      -F "print=true")
    
    echo "Upload completo e impressão iniciada!"
else
    # Apenas upload
    RESPONSE=$(curl -s -X POST "http://${PRINTER_HOST}:${PRINTER_PORT}/api/files/local" \
      -H "X-Api-Key: ${API_KEY}" \
      -F "file=@${GCODE_FILE}" \
      -F "filename=${FILENAME}")
    
    echo "Upload completo!"
fi

# Mostrar resposta
echo "Resposta do servidor:"
echo "$RESPONSE" | jq '.'

# Verificar sucesso
if echo "$RESPONSE" | jq -e '.result == "success"' > /dev/null; then
    echo "✅ Sucesso!"
    exit 0
else
    echo "❌ Erro no upload"
    exit 1
fi
```

**Uso**:
```bash
# Apenas upload
./upload_to_k2.sh model.gcode

# Upload e iniciar impressão
./upload_to_k2.sh model.gcode --print
```

---

### Python Script

```python
#!/usr/bin/env python3
# upload_to_k2.py

import requests
import sys
import os
from pathlib import Path

# Configuração
PRINTER_HOST = "k2plus.local"
PRINTER_PORT = 7125
API_KEY = "YOUR_API_KEY_HERE"

def upload_file(gcode_path, start_print=False):
    """Upload arquivo GCode para K2 Plus"""
    
    # Validar arquivo
    if not os.path.exists(gcode_path):
        print(f"Erro: Arquivo não encontrado: {gcode_path}")
        return False
    
    filename = Path(gcode_path).name
    url = f"http://{PRINTER_HOST}:{PRINTER_PORT}/api/files/local"
    
    headers = {
        "X-Api-Key": API_KEY
    }
    
    files = {
        "file": (filename, open(gcode_path, "rb"), "application/octet-stream")
    }
    
    data = {
        "filename": filename
    }
    
    if start_print:
        data["select"] = "true"
        data["print"] = "true"
    
    print(f"Enviando {filename} para {PRINTER_HOST}...")
    
    try:
        response = requests.post(url, headers=headers, files=files, data=data)
        response.raise_for_status()
        
        result = response.json()
        
        if result.get("result") == "success":
            print("✅ Upload completo!")
            if start_print:
                print("🖨️  Impressão iniciada!")
            return True
        else:
            print(f"❌ Erro: {result}")
            return False
            
    except requests.exceptions.RequestException as e:
        print(f"❌ Erro de conexão: {e}")
        return False

def get_printer_status():
    """Obter status da impressora"""
    
    url = f"http://{PRINTER_HOST}:{PRINTER_PORT}/api/printer/objects/query"
    params = {
        "heater_bed": "",
        "extruder": "",
        "print_stats": ""
    }
    headers = {
        "X-Api-Key": API_KEY
    }
    
    try:
        response = requests.get(url, headers=headers, params=params)
        response.raise_for_status()
        
        result = response.json()
        status = result["result"]["status"]
        
        print("\n📊 Status da Impressora:")
        print(f"  Mesa: {status['heater_bed']['temperature']:.1f}°C / {status['heater_bed']['target']:.1f}°C")
        print(f"  Extrusor: {status['extruder']['temperature']:.1f}°C / {status['extruder']['target']:.1f}°C")
        print(f"  Estado: {status['print_stats']['state']}")
        
        if status['print_stats']['state'] == 'printing':
            print(f"  Arquivo: {status['print_stats']['filename']}")
            print(f"  Duração: {status['print_stats']['print_duration']:.0f}s")
        
        return True
        
    except requests.exceptions.RequestException as e:
        print(f"❌ Erro ao obter status: {e}")
        return False

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Uso: python3 upload_to_k2.py <arquivo.gcode> [--print] [--status]")
        sys.exit(1)
    
    if "--status" in sys.argv:
        get_printer_status()
        sys.exit(0)
    
    gcode_file = sys.argv[1]
    start_print = "--print" in sys.argv
    
    success = upload_file(gcode_file, start_print)
    
    if success:
        get_printer_status()
        sys.exit(0)
    else:
        sys.exit(1)
```

**Uso**:
```bash
# Apenas upload
python3 upload_to_k2.py model.gcode

# Upload e iniciar
python3 upload_to_k2.py model.gcode --print

# Ver status
python3 upload_to_k2.py --status
```

---

## 🔍 Comparação com Bambu Lab

### Bambu Lab (Não Testável Facilmente)

```bash
# ❌ Não é possível testar com curl/scripts simples
# Motivo: Protocolo proprietário (MQTT + FTP)

# Seria necessário:
# 1. Biblioteca bambu_networking.dll
# 2. Token de autenticação Bambu Cloud
# 3. Cliente MQTT configurado
# 4. Cliente FTP com SSL/TLS
# 5. Conhecimento do protocolo interno
```

### Klipper (Fácil de Testar)

```bash
# ✅ Testável com ferramentas padrão
curl -X POST http://k2plus.local:7125/api/files/local \
  -H "X-Api-Key: YOUR_KEY" \
  -F "file=@model.gcode"
```

**Conclusão**: Klipper é muito mais fácil de debugar e testar.

---

## 📚 Recursos Adicionais

### Documentação Moonraker
- **API Reference**: https://moonraker.readthedocs.io/en/latest/web_api/
- **WebSocket**: https://moonraker.readthedocs.io/en/latest/web_api/#websocket-api

### Ferramentas
- **Postman Collection**: Importar endpoints Moonraker
- **Insomnia**: Testar APIs REST
- **curl**: Linha de comando
- **Python requests**: Scripts automatizados

---

**Versão**: 1.0  
**Data**: 2025-10-07  
**Autor**: Análise automatizada via Augment AI

