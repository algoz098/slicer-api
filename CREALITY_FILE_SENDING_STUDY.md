# Estudo: Sistema de Envio de Arquivos - OrcaSlicer vs CrealityPrint

## 📋 Resumo Executivo

Este documento analisa como o **OrcaSlicer** e o **CrealityPrint** enviam arquivos para impressoras, focando nas diferenças entre:

1. **Sistema Bambu Lab** (usado pelo OrcaSlicer)
2. **Sistema Klipper/OctoPrint** (usado pelo CrealityPrint para K2 Plus)

---

## 🏗️ Arquitetura Geral

### OrcaSlicer (Bambu Lab)

```
┌─────────────────────────────────────────────────────────┐
│                    ORCASLICER                            │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│              NetworkAgent (Bambu)                        │
│  - Biblioteca proprietária: bambu_networking.dll/.so    │
│  - Protocolo proprietário Bambu                         │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ├─► MQTT (Mensagens de controle)
                  ├─► FTP/FTPS (Upload de arquivos)
                  └─► HTTP/HTTPS (API Cloud)
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│           Impressora Bambu Lab (X1, P1P, etc)           │
│  - Servidor MQTT interno                                │
│  - Servidor FTP interno                                 │
│  - Conectada à Bambu Cloud                              │
└─────────────────────────────────────────────────────────┘
```

---

### CrealityPrint (Klipper/OctoPrint)

```
┌─────────────────────────────────────────────────────────┐
│                  CREALITYPRINT                           │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│              PrintHost (Interface)                       │
│  - OctoPrint                                            │
│  - PrusaLink                                            │
│  - Moonraker (Klipper)                                  │
│  - Outros...                                            │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ├─► HTTP/HTTPS (API REST)
                  ├─► WebSocket (Moonraker)
                  └─► Multipart Upload (Arquivos)
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│         Impressora Creality K2 Plus (Klipper)           │
│  - Moonraker (API HTTP)                                 │
│  - Klipper (Firmware)                                   │
│  - OctoPrint (Opcional)                                 │
└─────────────────────────────────────────────────────────┘
```

---

## 🔍 Diferenças Fundamentais

### 1. Protocolo de Comunicação

| Aspecto | Bambu Lab (OrcaSlicer) | Klipper (CrealityPrint) |
|---------|------------------------|-------------------------|
| **Protocolo Principal** | MQTT + FTP proprietário | HTTP REST API |
| **Upload de Arquivo** | FTP/FTPS | HTTP Multipart |
| **Controle** | MQTT (JSON) | HTTP POST/GET |
| **Streaming** | MQTT | WebSocket (Moonraker) |
| **Descoberta** | SSDP + Bambu Cloud | mDNS/Bonjour |
| **Autenticação** | Token Bambu + Senha | API Key / Digest Auth |

---

### 2. Configuração de Impressora

#### Bambu Lab (OrcaSlicer)

**Não usa `host_type`** - Sistema proprietário integrado

```json
{
    "printer_technology": "FFF",
    "printer_model": "Bambu Lab X1 Carbon",
    // Sem host_type - usa NetworkAgent diretamente
    "bbl_use_printhost": "0"
}
```

---

#### Creality K2 Plus (CrealityPrint)

**Usa `host_type: octoprint`** - Compatível com Klipper/Moonraker

```json
{
    "printer_technology": "FFF",
    "printer_model": "Creality K2 Plus",
    "host_type": "octoprint",  // ← Usa API OctoPrint/Moonraker
    "gcode_flavor": "klipper",
    "printhost_authorization_type": "key"
}
```

**Arquivo**: `resources/profiles/Creality/machine/Creality K2 Plus 0.4 nozzle.json:33`

---

## 📡 Sistema Bambu Lab (OrcaSlicer)

### NetworkAgent - Biblioteca Proprietária

**Arquivo**: `src/slic3r/Utils/NetworkAgent.hpp`

```cpp
class NetworkAgent
{
public:
    // Funções principais
    int start_print(PrintParams params, 
                   OnUpdateStatusFn update_fn, 
                   WasCancelledFn cancel_fn, 
                   OnWaitFn wait_fn);
    
    int start_send_gcode_to_sdcard(PrintParams params, 
                                   OnUpdateStatusFn update_fn, 
                                   WasCancelledFn cancel_fn, 
                                   OnWaitFn wait_fn);
    
    int connect_printer(std::string dev_id, 
                       std::string dev_ip, 
                       std::string username, 
                       std::string password, 
                       bool use_ssl);
    
    int send_message_to_printer(std::string dev_id, 
                               std::string json_str, 
                               int qos);
};
```

**Características**:
- Biblioteca binária proprietária (`bambu_networking.dll` / `.so`)
- Protocolo fechado da Bambu Lab
- Integração com Bambu Cloud
- MQTT para mensagens de controle
- FTP para upload de arquivos

---

### Fluxo de Envio Bambu

```
1. DESCOBERTA
   ├─► SSDP (Local Network Discovery)
   ├─► Bambu Cloud API (Lista de impressoras)
   └─► Retorna: dev_id, dev_ip, status

2. CONEXÃO
   ├─► connect_printer(dev_id, dev_ip, user, pass, ssl)
   ├─► Estabelece conexão MQTT
   └─► Autentica com token Bambu

3. UPLOAD
   ├─► start_print(params, callbacks)
   ├─► Upload via FTP/FTPS
   ├─► Arquivo: .3mf ou .gcode
   └─► Progresso via callback

4. CONTROLE
   ├─► send_message_to_printer(dev_id, json, qos)
   ├─► Comandos via MQTT
   └─► Exemplos: start, pause, stop, temperatura

5. MONITORAMENTO
   ├─► Recebe mensagens MQTT
   ├─► Status: temperatura, progresso, erros
   └─► Callbacks: OnMessageFn, OnUpdateStatusFn
```

---

## 🌐 Sistema Klipper/OctoPrint (CrealityPrint)

### PrintHost - Interface Genérica

**Arquivo**: `src/slic3r/Utils/PrintHost.hpp`

```cpp
class PrintHost
{
public:
    virtual ~PrintHost();

    typedef Http::ProgressFn ProgressFn;
    typedef std::function<void(wxString /* error */)> ErrorFn;
    typedef std::function<void(wxString /* tag */, wxString /* status */)> InfoFn;

    virtual const char* get_name() const = 0;
    
    virtual bool test(wxString &curl_msg) const = 0;
    
    virtual bool upload(PrintHostUpload upload_data, 
                       ProgressFn prorgess_fn, 
                       ErrorFn error_fn, 
                       InfoFn info_fn) const = 0;
    
    virtual bool has_auto_discovery() const = 0;
    virtual bool can_test() const = 0;
    virtual PrintHostPostUploadActions get_post_upload_actions() const = 0;
    virtual std::string get_host() const = 0;
};
```

---

### Implementações Disponíveis

**Arquivo**: `src/slic3r/Utils/PrintHost.cpp`

```cpp
PrintHost* PrintHost::get_print_host(DynamicPrintConfig *config)
{
    const auto host_type = config->option<ConfigOptionEnum<PrintHostType>>("host_type");

    switch (host_type->value) {
        case htOctoPrint:    return new OctoPrint(config);
        case htDuet:         return new Duet(config);
        case htFlashAir:     return new FlashAir(config);
        case htAstroBox:     return new AstroBox(config);
        case htRepetier:     return new Repetier(config);
        case htPrusaLink:    return new PrusaLink(config);
        case htPrusaConnect: return new PrusaConnect(config);
        case htMKS:          return new MKS(config);
        case htESP3D:        return new ESP3D(config);
        case htObico:        return new Obico(config);
        case htFlashforge:   return new Flashforge(config);
        case htSimplyPrint:  return new SimplyPrint(config);
        default:             return nullptr;
    }
}
```

**K2 Plus usa**: `htOctoPrint` (compatível com Moonraker/Klipper)

---

### OctoPrint/Moonraker Implementation

**Arquivo**: `src/slic3r/Utils/OctoPrint.hpp`

```cpp
class OctoPrint : public PrintHost
{
public:
    OctoPrint(DynamicPrintConfig *config);
    
    const char* get_name() const override { return "OctoPrint"; }
    
    bool test(wxString &curl_msg) const override;
    
    bool upload(PrintHostUpload upload_data, 
               ProgressFn prorgess_fn, 
               ErrorFn error_fn, 
               InfoFn info_fn) const override;
    
    bool has_auto_discovery() const override { return true; }
    
    PrintHostPostUploadActions get_post_upload_actions() const override { 
        return PrintHostPostUploadAction::StartPrint; 
    }
    
    std::string get_host() const override { return m_host; }

protected:
    std::string m_host;      // http://192.168.1.100:7125
    std::string m_apikey;    // API Key para autenticação
    std::string m_cafile;    // Certificado SSL (opcional)
    bool m_ssl_revoke_best_effort;
    
    virtual void set_auth(Http &http) const;
    std::string make_url(const std::string &path) const;
};
```

---

### Fluxo de Envio Klipper/OctoPrint

```
1. DESCOBERTA
   ├─► mDNS/Bonjour (*.local)
   ├─► Resolve hostname → IP
   └─► Exemplo: k2plus.local → 192.168.1.100

2. TESTE DE CONEXÃO
   ├─► GET http://192.168.1.100:7125/api/version
   ├─► Headers: X-Api-Key: <api_key>
   └─► Valida resposta JSON

3. UPLOAD
   ├─► POST http://192.168.1.100:7125/api/files/local
   ├─► Content-Type: multipart/form-data
   ├─► Campos:
   │   ├─► file: <gcode_data>
   │   ├─► filename: "model.gcode"
   │   └─► select: true (auto-select)
   └─► Progresso via callback

4. INICIAR IMPRESSÃO (Opcional)
   ├─► POST http://192.168.1.100:7125/api/job
   ├─► Body: {"command": "start"}
   └─► Headers: X-Api-Key: <api_key>

5. MONITORAMENTO
   ├─► GET http://192.168.1.100:7125/api/printer
   ├─► GET http://192.168.1.100:7125/api/job
   └─► WebSocket: ws://192.168.1.100:7125/websocket
```

---

## 🔐 Autenticação

### Bambu Lab

```cpp
// Token proprietário + Senha
int connect_printer(
    std::string dev_id,        // ID único da impressora
    std::string dev_ip,        // IP local
    std::string username,      // Usuário (geralmente "bblp")
    std::string password,      // Senha de acesso
    bool use_ssl              // Usar SSL/TLS
);
```

**Características**:
- Token obtido via Bambu Cloud
- Senha configurada na impressora
- SSL/TLS obrigatório para cloud
- Autenticação MQTT

---

### Klipper/OctoPrint

```cpp
// API Key
class OctoPrint {
protected:
    std::string m_host;      // "http://192.168.1.100:7125"
    std::string m_apikey;    // "ABC123DEF456..."
    std::string m_cafile;    // Certificado SSL (opcional)
    
    virtual void set_auth(Http &http) const {
        http.header("X-Api-Key", m_apikey);
    }
};
```

**Características**:
- API Key gerada no Moonraker/OctoPrint
- Enviada via HTTP Header
- Opcional: HTTP Digest Authentication
- SSL/TLS opcional

---

## 📤 Upload de Arquivos

### Bambu Lab - FTP

```cpp
int start_print(PrintParams params, 
               OnUpdateStatusFn update_fn, 
               WasCancelledFn cancel_fn, 
               OnWaitFn wait_fn)
{
    // 1. Conecta via FTP
    // 2. Upload do arquivo .3mf ou .gcode
    // 3. Envia comando MQTT para iniciar
    // 4. Monitora progresso
}
```

**Protocolo**:
- FTP/FTPS (porta 990 ou custom)
- Arquivo completo enviado antes de iniciar
- Suporta .3mf (preferido) ou .gcode

---

### Klipper - HTTP Multipart

```cpp
bool OctoPrint::upload(PrintHostUpload upload_data, 
                      ProgressFn prorgess_fn, 
                      ErrorFn error_fn, 
                      InfoFn info_fn) const
{
    // URL: http://host:7125/api/files/local
    std::string url = make_url("api/files/local");
    
    // Multipart form data
    Http http = Http::post(url);
    http.header("X-Api-Key", m_apikey);
    http.form_add_file("file", upload_data.source_path.string(), 
                       upload_data.upload_path.filename().string());
    
    if (upload_data.post_action == PrintHostPostUploadAction::StartPrint) {
        http.form_add("select", "true");
        http.form_add("print", "true");
    }
    
    http.on_progress(prorgess_fn);
    http.on_error(error_fn);
    
    return http.perform();
}
```

**Protocolo**:
- HTTP POST com multipart/form-data
- Streaming de arquivo
- Progresso em tempo real
- Suporta apenas .gcode

---

## 🔄 Comparação de APIs

### Moonraker (Klipper) - API REST

**Endpoints principais**:

```
GET  /api/version                    # Versão do Moonraker
GET  /api/printer                    # Status da impressora
GET  /api/printer/objects/list       # Lista de objetos disponíveis
GET  /api/printer/objects/query      # Query de objetos específicos
POST /api/files/local                # Upload de arquivo
POST /api/printer/print/start        # Iniciar impressão
POST /api/printer/print/pause        # Pausar impressão
POST /api/printer/print/cancel       # Cancelar impressão
GET  /api/job_queue/status           # Status da fila
WS   /websocket                      # WebSocket para updates
```

**Exemplo de Upload**:
```bash
curl -X POST http://192.168.1.100:7125/api/files/local \
  -H "X-Api-Key: YOUR_API_KEY" \
  -F "file=@model.gcode" \
  -F "filename=model.gcode" \
  -F "select=true" \
  -F "print=true"
```

---

### Bambu Lab - MQTT + FTP

**Tópicos MQTT**:

```
device/{dev_id}/request    # Enviar comandos
device/{dev_id}/report     # Receber status
```

**Exemplo de Comando** (JSON via MQTT):
```json
{
    "print": {
        "command": "start",
        "param": "model.3mf",
        "sequence_id": "12345"
    }
}
```

**FTP Upload**:
```
ftp://192.168.1.100:990/model.3mf
User: bblp
Pass: <access_code>
```

---

## 📊 Tabela Comparativa Completa

| Característica | Bambu Lab (OrcaSlicer) | Klipper (CrealityPrint) |
|----------------|------------------------|-------------------------|
| **Protocolo** | MQTT + FTP | HTTP REST |
| **Descoberta** | SSDP + Cloud | mDNS/Bonjour |
| **Autenticação** | Token + Senha | API Key |
| **Upload** | FTP/FTPS | HTTP Multipart |
| **Formato** | .3mf (preferido) / .gcode | .gcode apenas |
| **Controle** | MQTT (JSON) | HTTP POST |
| **Streaming** | MQTT | WebSocket |
| **SSL/TLS** | Obrigatório (cloud) | Opcional |
| **Biblioteca** | Proprietária (binária) | Open Source (libcurl) |
| **Porta Padrão** | 990 (FTP), 1883 (MQTT) | 7125 (Moonraker) |
| **Cloud** | Bambu Cloud integrado | Opcional (Obico, etc) |
| **Offline** | Limitado | Completo |
| **Open Source** | ❌ Não | ✅ Sim |

---

## 🛠️ Implementação no CrealityPrint

### Estrutura de Arquivos

```
src/slic3r/Utils/
├── PrintHost.hpp              # Interface base
├── PrintHost.cpp              # Factory method
├── OctoPrint.hpp              # Implementação OctoPrint/Moonraker
├── OctoPrint.cpp              # ~1200 linhas
├── PrusaLink.hpp              # Variante PrusaLink
├── SimplyPrint.hpp            # Cloud service
├── Http.hpp                   # Cliente HTTP
├── Http.cpp                   # Implementação libcurl
└── NetworkAgent.hpp           # Bambu (proprietário)
```

---

### Configuração K2 Plus

**Arquivo**: `Creality K2 Plus 0.4 nozzle.json`

```json
{
    "host_type": "octoprint",
    "print_host": "",
    "printhost_apikey": "",
    "printhost_port": "7125",
    "printhost_authorization_type": "key",
    "printhost_ssl_ignore_revoke": "0"
}
```

**Usuário configura**:
1. IP/Hostname: `k2plus.local` ou `192.168.1.100`
2. Porta: `7125` (Moonraker) ou `80` (OctoPrint)
3. API Key: Gerada no Moonraker/OctoPrint

---

## 🚀 Fluxo Completo de Envio

### CrealityPrint → K2 Plus (Klipper)

```
┌─────────────────────────────────────────────────────────┐
│ 1. USUÁRIO CLICA "SEND TO PRINTER"                      │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 2. SendToPrinterDialog::on_ok()                         │
│    - Valida configurações                               │
│    - Cria PrintHostJob                                  │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 3. PrintHost::get_print_host(config)                    │
│    - Lê host_type = "octoprint"                         │
│    - return new OctoPrint(config)                       │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 4. OctoPrint::test()                                    │
│    - GET http://k2plus.local:7125/api/version           │
│    - Valida resposta                                    │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 5. OctoPrint::upload()                                  │
│    - POST http://k2plus.local:7125/api/files/local      │
│    - Multipart: file=model.gcode                        │
│    - Header: X-Api-Key: <key>                           │
│    - Callback: progresso (0-100%)                       │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 6. Moonraker recebe arquivo                             │
│    - Salva em ~/printer_data/gcodes/                    │
│    - Retorna: {"result": "success"}                     │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 7. (Opcional) Iniciar impressão                         │
│    - POST http://k2plus.local:7125/api/printer/print/start │
│    - Body: {"filename": "model.gcode"}                  │
└─────────────────┬───────────────────────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────────────────────┐
│ 8. Klipper inicia impressão                             │
│    - Lê GCode do arquivo                                │
│    - Executa comandos                                   │
└─────────────────────────────────────────────────────────┘
```

---

## 📝 Código de Exemplo

### Upload para K2 Plus

```cpp
// Configuração
DynamicPrintConfig config;
config.set_key_value("host_type", new ConfigOptionEnum<PrintHostType>(htOctoPrint));
config.set_key_value("print_host", new ConfigOptionString("http://k2plus.local:7125"));
config.set_key_value("printhost_apikey", new ConfigOptionString("YOUR_API_KEY"));

// Criar PrintHost
PrintHost* host = PrintHost::get_print_host(&config);

// Dados de upload
PrintHostUpload upload_data;
upload_data.source_path = "/path/to/model.gcode";
upload_data.upload_path = "model.gcode";
upload_data.post_action = PrintHostPostUploadAction::StartPrint;

// Callbacks
auto progress_fn = [](Http::Progress progress, bool& cancel) {
    std::cout << "Progress: " << progress.ultotal << "/" << progress.ulnow << std::endl;
};

auto error_fn = [](wxString error) {
    std::cerr << "Error: " << error << std::endl;
};

auto info_fn = [](wxString tag, wxString status) {
    std::cout << tag << ": " << status << std::endl;
};

// Upload
bool success = host->upload(upload_data, progress_fn, error_fn, info_fn);
```

---

## 🎯 Principais Diferenças

### 1. **Complexidade**
- **Bambu**: Sistema complexo, proprietário, integrado
- **Klipper**: Sistema simples, open source, padrão HTTP

### 2. **Dependências**
- **Bambu**: Biblioteca binária proprietária
- **Klipper**: libcurl (padrão)

### 3. **Flexibilidade**
- **Bambu**: Limitado ao ecossistema Bambu
- **Klipper**: Funciona com qualquer impressora compatível

### 4. **Debugging**
- **Bambu**: Difícil (código fechado)
- **Klipper**: Fácil (logs HTTP, código aberto)

### 5. **Portabilidade**
- **Bambu**: Apenas Bambu Lab
- **Klipper**: Creality, Voron, Prusa, etc.

---

---

## 🔧 Como Adaptar OrcaSlicer para K2 Plus

### Opção 1: Adicionar Suporte Klipper/Moonraker

O OrcaSlicer atualmente **não tem** suporte nativo para Klipper/Moonraker. Para adicionar:

#### 1. Portar Classes do CrealityPrint

```cpp
// Copiar de CrealityPrint para OrcaSlicer:
src/slic3r/Utils/
├── PrintHost.hpp              # Interface base
├── PrintHost.cpp              # Factory
├── OctoPrint.hpp              # Implementação Moonraker
├── OctoPrint.cpp              # ~1200 linhas
└── Http.hpp/cpp               # Cliente HTTP (se não existir)
```

#### 2. Adicionar Configuração

```json
// Em perfil de impressora K2 Plus
{
    "host_type": "octoprint",
    "print_host": "http://k2plus.local:7125",
    "printhost_apikey": "",
    "printhost_port": "7125",
    "printhost_authorization_type": "key"
}
```

#### 3. Modificar UI

```cpp
// Adicionar opção "Send to Printer" para Klipper
if (printer_host_type == "octoprint") {
    // Usar OctoPrint::upload()
} else if (printer_host_type == "bambu") {
    // Usar NetworkAgent::start_print()
}
```

---

### Opção 2: Usar Bambu Connect (Novo)

**Atualização 2025**: Bambu Lab lançou **Bambu Connect** - protocolo aberto para integração.

**Características**:
- API HTTP/REST (similar ao Moonraker)
- Autenticação via token
- Documentação pública
- Compatível com OrcaSlicer

**Problema**: K2 Plus **não suporta** Bambu Connect (é Creality, não Bambu).

---

### Opção 3: Proxy/Bridge

Criar um serviço intermediário que traduz protocolos:

```
OrcaSlicer (Bambu Protocol)
    ↓
Proxy/Bridge Service
    ↓
Moonraker (Klipper Protocol)
    ↓
K2 Plus
```

**Vantagens**:
- Não modifica OrcaSlicer
- Reutiliza código existente

**Desvantagens**:
- Complexidade adicional
- Latência extra
- Ponto único de falha

---

## 🎓 Lições Aprendidas

### 1. **Protocolos Proprietários vs Abertos**

| Aspecto | Bambu (Proprietário) | Klipper (Aberto) |
|---------|---------------------|------------------|
| **Documentação** | Limitada/Inexistente | Completa |
| **Debugging** | Difícil | Fácil |
| **Extensibilidade** | Impossível | Total |
| **Vendor Lock-in** | Alto | Nenhum |
| **Segurança** | Obscuridade | Transparência |

---

### 2. **Arquitetura Modular**

CrealityPrint usa **interface `PrintHost`** que permite:
- Adicionar novos protocolos facilmente
- Testar independentemente
- Reutilizar código HTTP

OrcaSlicer usa **biblioteca proprietária** que:
- Funciona apenas com Bambu
- Não pode ser estendida
- Dificulta manutenção

---

### 3. **Padrões da Indústria**

**Klipper/Moonraker** se tornou padrão de fato para impressoras open source:
- Creality K1, K2
- Voron
- Prusa (via PrusaLink)
- Ratrig
- Etc.

**Bambu Lab** permanece isolado com protocolo proprietário.

---

## 📚 Referências

### Documentação Oficial

1. **Moonraker API**: https://moonraker.readthedocs.io/en/latest/web_api/
2. **Klipper**: https://www.klipper3d.org/
3. **OctoPrint API**: https://docs.octoprint.org/en/master/api/
4. **Bambu Connect**: https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/

### Código Fonte

1. **CrealityPrint**: https://github.com/CrealityOfficial/CrealityPrint
2. **OrcaSlicer**: https://github.com/SoftFever/OrcaSlicer
3. **Moonraker**: https://github.com/Arksine/moonraker
4. **Klipper**: https://github.com/Klipper3d/klipper

---

## 🚨 Problemas Conhecidos

### Bambu Lab - Mudanças Recentes (2025)

**Problema**: Bambu Lab está **bloqueando** acesso direto via MQTT/FTP em modo LAN.

**Solução Temporária**: "Developer Mode" que mantém MQTT/FTP abertos.

**Impacto no OrcaSlicer**:
- Pode parar de funcionar em futuras atualizações
- Usuários precisam habilitar "Developer Mode"
- Incerteza sobre suporte futuro

**Fonte**:
- https://blog.bambulab.com/updates-and-third-party-integration-with-bambu-connect/
- https://www.reddit.com/r/BambuLab/comments/1i4vp5i/

---

### Creality K2 Plus - Limitações

**Problema**: Moonraker/Klipper **não suporta** arquivos .3mf.

**Solução**: Sempre enviar .gcode (não .3mf).

**Impacto**:
- Perde metadados do .3mf
- Perde thumbnails embutidos
- Perde informações de filamento

---

## 🎯 Recomendações

### Para Implementar no OrcaSlicer

1. **Adicionar suporte Klipper/Moonraker**
   - Portar código do CrealityPrint
   - Criar interface `PrintHost` similar
   - Adicionar opção `host_type` nos perfis

2. **Manter compatibilidade Bambu**
   - Não remover código existente
   - Adicionar como alternativa
   - Detectar automaticamente tipo de impressora

3. **Testar extensivamente**
   - K2 Plus com Moonraker
   - Outras impressoras Klipper
   - Bambu Lab (regressão)

4. **Documentar**
   - Como configurar K2 Plus
   - Como obter API Key do Moonraker
   - Troubleshooting comum

---

### Para Usuários (Workaround Atual)

**Enquanto OrcaSlicer não suporta K2 Plus diretamente**:

#### Opção A: Usar CrealityPrint
- Suporte nativo K2 Plus
- Sistema de multicor otimizado
- Envio direto via Moonraker

#### Opção B: Upload Manual
1. Slice no OrcaSlicer
2. Salvar .gcode
3. Upload via Mainsail/Fluidd (interface web do Klipper)

#### Opção C: Script de Upload
```bash
#!/bin/bash
# upload_to_k2.sh

PRINTER_IP="k2plus.local"
PRINTER_PORT="7125"
API_KEY="YOUR_API_KEY"
GCODE_FILE="$1"

curl -X POST "http://${PRINTER_IP}:${PRINTER_PORT}/api/files/local" \
  -H "X-Api-Key: ${API_KEY}" \
  -F "file=@${GCODE_FILE}" \
  -F "select=true" \
  -F "print=false"

echo "Upload complete!"
```

**Uso**:
```bash
./upload_to_k2.sh model.gcode
```

---

## 📊 Resumo Final

### Principais Diferenças

| Característica | Bambu Lab | Creality K2 Plus |
|----------------|-----------|------------------|
| **Protocolo** | MQTT + FTP (proprietário) | HTTP REST (Moonraker) |
| **Biblioteca** | bambu_networking (binária) | libcurl (open source) |
| **Formato** | .3mf preferido | .gcode apenas |
| **Descoberta** | SSDP + Cloud | mDNS/Bonjour |
| **Autenticação** | Token + Senha | API Key |
| **Documentação** | Limitada | Completa |
| **Extensibilidade** | Impossível | Total |
| **Suporte OrcaSlicer** | ✅ Nativo | ❌ Não existe |
| **Suporte CrealityPrint** | ❌ Não | ✅ Nativo |

---

### Conclusão

1. **OrcaSlicer** usa protocolo proprietário Bambu Lab (MQTT + FTP)
2. **CrealityPrint** usa protocolo aberto Klipper/Moonraker (HTTP REST)
3. **K2 Plus** requer Moonraker, não compatível com protocolo Bambu
4. **Solução**: Adicionar suporte Moonraker ao OrcaSlicer (portar de CrealityPrint)

---

**Versão**: 1.1
**Data**: 2025-10-07
**Autor**: Análise automatizada via Augment AI
**Atualização**: Adicionadas recomendações e workarounds

