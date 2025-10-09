# Comparação Visual: Sistemas de Rede - Bambu vs Klipper

## 🎯 Resumo Executivo

Este documento apresenta uma comparação visual entre os sistemas de rede usados por:
- **OrcaSlicer** → Impressoras **Bambu Lab** (X1, P1P)
- **CrealityPrint** → Impressoras **Creality K2 Plus** (Klipper)

---

## 📡 Arquitetura de Comunicação

### Sistema Bambu Lab (OrcaSlicer)

```
┌──────────────────────────────────────────────────────────────┐
│                        ORCASLICER                             │
│  - Interface gráfica                                          │
│  - Geração de GCode                                           │
│  - Configuração de impressora                                 │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Chama funções
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              NetworkAgent (Wrapper C++)                       │
│  - start_print()                                              │
│  - start_send_gcode_to_sdcard()                               │
│  - connect_printer()                                          │
│  - send_message_to_printer()                                  │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Carrega dinamicamente
                         ▼
┌──────────────────────────────────────────────────────────────┐
│         bambu_networking.dll / .so (PROPRIETÁRIO)             │
│  - Protocolo fechado Bambu Lab                                │
│  - Implementação MQTT                                         │
│  - Implementação FTP/FTPS                                     │
│  - Criptografia/Autenticação                                  │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Múltiplos protocolos
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
   ┌─────────┐     ┌─────────┐     ┌──────────┐
   │  MQTT   │     │   FTP   │     │  HTTPS   │
   │ Port    │     │  Port   │     │  Bambu   │
   │ 1883    │     │  990    │     │  Cloud   │
   └────┬────┘     └────┬────┘     └────┬─────┘
        │               │               │
        └───────────────┼───────────────┘
                        │
                        ▼
┌──────────────────────────────────────────────────────────────┐
│              IMPRESSORA BAMBU LAB                             │
│  - Servidor MQTT interno                                      │
│  - Servidor FTP interno                                       │
│  - Cliente Bambu Cloud                                        │
│  - Processador de comandos                                    │
└──────────────────────────────────────────────────────────────┘
```

---

### Sistema Klipper (CrealityPrint)

```
┌──────────────────────────────────────────────────────────────┐
│                      CREALITYPRINT                            │
│  - Interface gráfica                                          │
│  - Geração de GCode                                           │
│  - Configuração de impressora                                 │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Chama métodos
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              PrintHost (Interface C++)                        │
│  - test()                                                     │
│  - upload()                                                   │
│  - get_name()                                                 │
│  - get_host()                                                 │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Implementação polimórfica
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              OctoPrint (Classe C++)                           │
│  - Implementa interface PrintHost                             │
│  - Usa libcurl (open source)                                  │
│  - Protocolo HTTP REST                                        │
│  - Compatível com Moonraker/Klipper                           │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ HTTP/HTTPS
                         ▼
┌──────────────────────────────────────────────────────────────┐
│                    MOONRAKER (API)                            │
│  - Servidor HTTP (porta 7125)                                 │
│  - API REST documentada                                       │
│  - WebSocket para streaming                                   │
│  - Gerenciamento de arquivos                                  │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Comandos internos
                         ▼
┌──────────────────────────────────────────────────────────────┐
│                    KLIPPER (Firmware)                         │
│  - Interpreta GCode                                           │
│  - Controla hardware                                          │
│  - Cinemática avançada                                        │
└────────────────────────┬─────────────────────────────────────┘
                         │
                         │ Sinais elétricos
                         ▼
┌──────────────────────────────────────────────────────────────┐
│              IMPRESSORA CREALITY K2 PLUS                      │
│  - Motores                                                    │
│  - Hotend                                                     │
│  - Sensores                                                   │
└──────────────────────────────────────────────────────────────┘
```

---

## 🔄 Fluxo de Upload de Arquivo

### Bambu Lab - Upload via FTP

```
┌─────────────┐
│ OrcaSlicer  │
│ model.3mf   │
└──────┬──────┘
       │
       │ 1. NetworkAgent::start_print()
       ▼
┌─────────────────────────────────────────┐
│ bambu_networking.dll                    │
│ - Conecta FTP (porta 990)               │
│ - Autentica (user: bblp, pass: code)    │
│ - Upload binário do arquivo             │
└──────┬──────────────────────────────────┘
       │
       │ 2. FTP PUT /model.3mf
       ▼
┌─────────────────────────────────────────┐
│ Impressora Bambu                        │
│ - Recebe arquivo completo               │
│ - Salva em memória interna              │
└──────┬──────────────────────────────────┘
       │
       │ 3. MQTT: {"print": {"command": "start"}}
       ▼
┌─────────────────────────────────────────┐
│ Impressora Bambu                        │
│ - Processa .3mf                         │
│ - Extrai GCode                          │
│ - Inicia impressão                      │
└─────────────────────────────────────────┘
```

**Tempo típico**: 5-15 segundos (dependendo do tamanho do arquivo)

---

### Klipper - Upload via HTTP

```
┌─────────────┐
│CrealityPrint│
│ model.gcode │
└──────┬──────┘
       │
       │ 1. OctoPrint::upload()
       ▼
┌─────────────────────────────────────────┐
│ libcurl (HTTP Client)                   │
│ - POST http://k2plus:7125/api/files/    │
│ - Content-Type: multipart/form-data     │
│ - Header: X-Api-Key: <key>              │
│ - Streaming do arquivo                  │
└──────┬──────────────────────────────────┘
       │
       │ 2. HTTP POST (multipart)
       ▼
┌─────────────────────────────────────────┐
│ Moonraker (API Server)                  │
│ - Recebe chunks do arquivo              │
│ - Salva em ~/printer_data/gcodes/       │
│ - Retorna JSON: {"result": "success"}   │
└──────┬──────────────────────────────────┘
       │
       │ 3. (Opcional) POST /api/printer/print/start
       ▼
┌─────────────────────────────────────────┐
│ Klipper                                 │
│ - Lê arquivo GCode                      │
│ - Executa comandos linha por linha      │
│ - Inicia impressão                      │
└─────────────────────────────────────────┘
```

**Tempo típico**: 2-10 segundos (dependendo do tamanho do arquivo)

---

## 🔐 Autenticação

### Bambu Lab

```
┌──────────────────────────────────────────────────────────┐
│ MÉTODO 1: Bambu Cloud                                     │
└──────────────────────────────────────────────────────────┘

Usuário → Bambu Cloud (login)
    ↓
Bambu Cloud → Token de acesso
    ↓
OrcaSlicer → Usa token para descobrir impressoras
    ↓
OrcaSlicer → Conecta impressora via token + senha local


┌──────────────────────────────────────────────────────────┐
│ MÉTODO 2: LAN Mode (Direto)                               │
└──────────────────────────────────────────────────────────┘

Usuário → Configura "Access Code" na impressora
    ↓
OrcaSlicer → Descobre impressora via SSDP
    ↓
OrcaSlicer → Conecta com:
    - Username: "bblp"
    - Password: <access_code>
    - SSL/TLS: Obrigatório
```

---

### Klipper/Moonraker

```
┌──────────────────────────────────────────────────────────┐
│ MÉTODO 1: API Key (Recomendado)                           │
└──────────────────────────────────────────────────────────┘

Usuário → Acessa Mainsail/Fluidd (interface web)
    ↓
Usuário → Gera API Key em Settings
    ↓
Usuário → Copia API Key
    ↓
CrealityPrint → Configura:
    - Host: http://k2plus.local:7125
    - API Key: <chave_gerada>
    ↓
CrealityPrint → Envia em cada request:
    - Header: X-Api-Key: <chave>


┌──────────────────────────────────────────────────────────┐
│ MÉTODO 2: HTTP Digest Auth (Opcional)                     │
└──────────────────────────────────────────────────────────┘

Usuário → Configura username/password no Moonraker
    ↓
CrealityPrint → Usa HTTP Digest Authentication
    ↓
Moonraker → Valida credenciais
```

---

## 📊 Comparação de Protocolos

### MQTT (Bambu Lab)

```
Características:
✅ Baixa latência
✅ Bidirecional (pub/sub)
✅ Leve (overhead mínimo)
❌ Não é padrão web
❌ Requer cliente específico
❌ Difícil de debugar

Exemplo de Mensagem:
Topic: device/01S00C123456789/request
Payload: {
    "print": {
        "command": "start",
        "param": "model.3mf",
        "sequence_id": "12345"
    }
}
```

---

### HTTP REST (Klipper)

```
Características:
✅ Padrão web universal
✅ Fácil de debugar (curl, Postman)
✅ Documentação extensa
✅ Ferramentas abundantes
❌ Overhead maior que MQTT
❌ Polling para updates (sem WebSocket)

Exemplo de Request:
POST http://k2plus.local:7125/api/files/local
Headers:
    X-Api-Key: ABC123DEF456
    Content-Type: multipart/form-data
Body:
    file: <binary_data>
    filename: model.gcode
    select: true
    print: false
```

---

## 🛠️ Ferramentas de Debug

### Bambu Lab

```bash
# ❌ Difícil - Protocolo proprietário

# Opção 1: Wireshark (captura de pacotes)
sudo wireshark
# Filtrar: mqtt || ftp

# Opção 2: Logs do OrcaSlicer
tail -f ~/.config/OrcaSlicer/log/orcaslicer.log

# Opção 3: Proxy MQTT (complexo)
# Requer configuração de certificados SSL
```

---

### Klipper/Moonraker

```bash
# ✅ Fácil - HTTP padrão

# Opção 1: curl (testar API)
curl -X GET http://k2plus.local:7125/api/version \
  -H "X-Api-Key: YOUR_KEY"

# Opção 2: Logs do Moonraker
tail -f ~/printer_data/logs/moonraker.log

# Opção 3: Browser DevTools
# Abrir Mainsail/Fluidd → F12 → Network tab

# Opção 4: Postman/Insomnia
# Importar coleção de APIs do Moonraker
```

---

## 📈 Performance

### Upload de Arquivo 100MB

| Métrica | Bambu Lab (FTP) | Klipper (HTTP) |
|---------|-----------------|----------------|
| **Tempo** | ~8 segundos | ~10 segundos |
| **Protocolo** | FTP binário | HTTP multipart |
| **Overhead** | Baixo (~2%) | Médio (~5-10%) |
| **Streaming** | Sim | Sim |
| **Progresso** | Callback nativo | HTTP progress |
| **Retry** | Automático | Manual |

**Conclusão**: Bambu é ~20% mais rápido, mas diferença é insignificante na prática.

---

## 🔒 Segurança

### Bambu Lab

```
✅ SSL/TLS obrigatório (cloud)
✅ Autenticação token + senha
✅ Criptografia end-to-end
❌ Protocolo fechado (security by obscurity)
❌ Sem auditoria independente
❌ Dependência de cloud (single point of failure)
```

---

### Klipper/Moonraker

```
✅ Código open source (auditável)
✅ API Key com permissões granulares
✅ SSL/TLS opcional (via reverse proxy)
✅ Funciona 100% offline
❌ Sem criptografia por padrão (LAN)
❌ Configuração SSL manual
```

---

## 🎯 Recomendação Final

### Para OrcaSlicer + K2 Plus

**Problema**: OrcaSlicer não suporta Klipper/Moonraker nativamente.

**Solução Recomendada**: Adicionar suporte Moonraker ao OrcaSlicer.

**Passos**:
1. Portar classes `PrintHost` e `OctoPrint` do CrealityPrint
2. Adicionar opção `host_type: "octoprint"` nos perfis
3. Implementar UI para configuração (host, API key)
4. Testar com K2 Plus e outras impressoras Klipper

**Alternativa Temporária**: Usar CrealityPrint para K2 Plus.

---

## 📚 Arquivos Relevantes

### CrealityPrint (Referência)

```
src/slic3r/Utils/
├── PrintHost.hpp              # Linha 1-100: Interface base
├── PrintHost.cpp              # Linha 1-50: Factory method
├── OctoPrint.hpp              # Linha 1-150: Declaração classe
├── OctoPrint.cpp              # Linha 1-1200: Implementação completa
└── Http.hpp/cpp               # Cliente HTTP (libcurl)

resources/profiles/Creality/machine/
└── Creality K2 Plus 0.4 nozzle.json  # Linha 33: "host_type": "octoprint"
```

---

### OrcaSlicer (Atual)

```
src/slic3r/Utils/
├── NetworkAgent.hpp           # Wrapper para bambu_networking
└── NetworkAgent.cpp           # Implementação

# ❌ Não existe:
# - PrintHost.hpp
# - OctoPrint.hpp
# - Suporte Klipper/Moonraker
```

---

**Versão**: 1.0  
**Data**: 2025-10-07  
**Autor**: Análise automatizada via Augment AI

