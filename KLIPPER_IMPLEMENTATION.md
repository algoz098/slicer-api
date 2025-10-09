# Implementação de Suporte Klipper/Moonraker no OrcaSlicerAddon

## 🎯 Objetivo

Adicionar suporte completo para impressoras Klipper (especialmente **Creality K2 Plus**) ao **OrcaSlicerAddon**, sem modificar o código do OrcaSlicer.

---

## ✅ O Que Foi Implementado

### 1. **KlipperClient** (`lib/klipper-client.js`)

Cliente JavaScript para comunicação com Moonraker API.

**Funcionalidades**:
- ✅ Conexão e teste de conectividade
- ✅ Upload de arquivos GCode
- ✅ Controle de impressão (start, pause, resume, cancel)
- ✅ Monitoramento de status (temperaturas, posição, progresso)
- ✅ Gerenciamento de arquivos (list, delete, metadata)
- ✅ Suporte a API Key
- ✅ Progress callbacks para upload
- ✅ Timeout configurável

**Protocolo**: HTTP REST (Moonraker API)

**Porta padrão**: 7125

---

### 2. **SliceAndSend** (`lib/slice-and-send.js`)

API de alto nível que integra slicing com upload.

**Funcionalidades**:
- ✅ Gerenciamento de múltiplas impressoras
- ✅ Workflow completo: slice → upload → print
- ✅ Geração automática de GCode temporário
- ✅ Limpeza automática de arquivos temporários
- ✅ Progress callbacks para slicing e upload
- ✅ Teste de conectividade antes de enviar
- ✅ Status de todas as impressoras

**Métodos principais**:
- `addPrinter()` - Adicionar impressora
- `slice()` - Fatiar modelo 3D
- `sliceAndSend()` - Fatiar e enviar
- `sliceAndPrint()` - Fatiar e imprimir
- `sendFile()` - Enviar GCode existente

---

### 3. **Tipos TypeScript** (`types/klipper.d.ts`)

Definições de tipos completas para suporte de IDE.

**Interfaces**:
- `KlipperConfig` - Configuração do cliente
- `TestResult` - Resultado de teste de conexão
- `PrinterStatus` - Status da impressora
- `UploadOptions` - Opções de upload
- `UploadResult` - Resultado de upload
- `SliceAndSendOptions` - Opções de slice e envio
- E mais...

---

### 4. **Exemplos** (`examples/`)

Três exemplos práticos:

1. **klipper-basic.js** - Uso básico do KlipperClient
2. **slice-and-send.js** - Workflow completo com múltiplas impressoras
3. **slice-and-print.js** - One-shot slice e print

---

### 5. **Testes** (`test/klipper.js`)

Suite de testes automatizados:
- ✅ Carregamento de módulos
- ✅ Instanciação de classes
- ✅ Gerenciamento de impressoras
- ✅ Testes live (opcionais, requerem impressora real)

---

### 6. **Documentação** (`README_KLIPPER.md`)

Documentação completa incluindo:
- Quick start
- API reference completa
- Exemplos de uso
- Troubleshooting
- Detalhes técnicos

---

## 📁 Estrutura de Arquivos

```
OrcaSlicerAddon/bindings/node/
├── lib/
│   ├── klipper-client.js       # Cliente Moonraker
│   └── slice-and-send.js       # API de alto nível
├── types/
│   ├── klipper.d.ts            # Tipos TypeScript
│   └── index.d.ts              # (atualizado)
├── examples/
│   ├── klipper-basic.js        # Exemplo básico
│   ├── slice-and-send.js       # Exemplo completo
│   └── slice-and-print.js      # Exemplo one-shot
├── test/
│   └── klipper.js              # Testes
├── index.js                    # (atualizado)
├── package.json                # (atualizado)
└── README_KLIPPER.md           # Documentação
```

---

## 🔧 Como Usar

### Instalação

```bash
cd OrcaSlicerAddon/bindings/node
npm install
```

**Novas dependências**:
- `axios` - Cliente HTTP
- `form-data` - Upload multipart

---

### Uso Básico

```javascript
const addon = require('orcaslicer-addon');
const { KlipperClient, SliceAndSend } = addon;

// 1. Cliente direto
const printer = new KlipperClient({
  host: 'k2plus.local',
  port: 7125
});

await printer.test();
await printer.uploadFile('./model.gcode', { print: true });

// 2. Slice e envio
addon.initialize({ resourcesPath: './OrcaSlicer/resources' });

const manager = new SliceAndSend(addon);
manager.addPrinter('k2plus', { host: 'k2plus.local' });

await manager.sliceAndPrint('./model.stl', 'k2plus', {
  sliceConfig: {
    printerProfile: 'Creality K2 Plus 0.4 nozzle'
  }
});
```

---

### Executar Exemplos

```bash
# Exemplo básico
node examples/klipper-basic.js

# Slice e envio
ORCACLI_RESOURCES=../../../OrcaSlicer/resources \
  node examples/slice-and-send.js

# One-shot
ORCACLI_RESOURCES=../../../OrcaSlicer/resources \
  node examples/slice-and-print.js
```

---

### Executar Testes

```bash
# Testes unitários (sem impressora)
node test/klipper.js

# Testes live (com impressora)
KLIPPER_TEST_HOST=k2plus.local \
KLIPPER_TEST_PORT=7125 \
  node test/klipper.js

# Pular testes live
SKIP_KLIPPER_LIVE_TESTS=1 node test/klipper.js
```

---

## 🎨 Exemplos de Código

### Exemplo 1: Upload Simples

```javascript
const { KlipperClient } = require('orcaslicer-addon');

const printer = new KlipperClient({ host: 'k2plus.local' });

const result = await printer.uploadFile('./model.gcode', {
  filename: 'my_print.gcode',
  print: true,
  onProgress: (p) => console.log(`${p.percentage}%`)
});

console.log('Success:', result.success);
```

---

### Exemplo 2: Múltiplas Impressoras

```javascript
const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;

addon.initialize({ resourcesPath: './OrcaSlicer/resources' });

const manager = new SliceAndSend(addon);

// Adicionar impressoras
manager.addPrinter('office', { host: 'k2plus-office.local' });
manager.addPrinter('lab', { host: '192.168.1.100' });
manager.addPrinter('workshop', { host: 'k2plus-workshop.local' });

// Enviar para impressora específica
await manager.sliceAndSend('./model.stl', 'office', {
  sliceConfig: { printerProfile: 'Creality K2 Plus 0.4 nozzle' }
});

// Status de todas
const statuses = await manager.getAllPrinterStatus();
console.log(statuses);
```

---

### Exemplo 3: Workflow Completo

```javascript
const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;

addon.initialize({ resourcesPath: './OrcaSlicer/resources' });

const manager = new SliceAndSend(addon);
manager.addPrinter('k2plus', { host: 'k2plus.local' });

// Slice e print com callbacks
const result = await manager.sliceAndPrint('./benchy.stl', 'k2plus', {
  sliceConfig: {
    printerProfile: 'Creality K2 Plus 0.4 nozzle',
    filamentProfile: 'Creality PLA Basic @K2Plus',
    processProfile: '0.20mm Standard @K2Plus',
    options: {
      layer_height: 0.2,
      infill_density: '15%',
      wall_loops: 3
    }
  },
  filename: 'benchy.gcode',
  keepLocal: true,
  onSliceProgress: (p) => console.log('Slicing:', p),
  onUploadProgress: (p) => console.log('Upload:', p.percentage + '%')
});

if (result.success) {
  console.log('Print started!');
  console.log('Local file:', result.localPath);
  console.log('Remote file:', result.remotePath);
}
```

---

## 🔍 Detalhes Técnicos

### Formato de Arquivo

**Klipper aceita apenas GCode puro** (`.gcode`), não `.3mf` ou `.gcode.3mf`.

**Metadados** são embutidos como comentários no cabeçalho:

```gcode
; HEADER_BLOCK_START
; generated by OrcaSlicer
; filament_type = PLA;PLA
; filament_colour = #FF0000;#0000FF
; nozzle_temperature = 210,210
; bed_temperature = 60
; HEADER_BLOCK_END

; CONFIG_BLOCK_START
; layer_height = 0.2
; infill_density = 15%
; nozzle_volume = 183
; flush_volumes_matrix = 0,1200,1200,0
; CONFIG_BLOCK_END

M140 S60
M104 S210
...
```

**Moonraker** lê esses comentários e extrai metadados automaticamente.

---

### Protocolo de Rede

**Moonraker API** (HTTP REST):

| Endpoint | Método | Descrição |
|----------|--------|-----------|
| `/api/version` | GET | Versão e status |
| `/api/printer/objects/query` | GET | Status da impressora |
| `/api/files/local` | POST | Upload de arquivo |
| `/api/printer/print/start` | POST | Iniciar impressão |
| `/api/printer/print/pause` | POST | Pausar impressão |
| `/api/printer/print/resume` | POST | Retomar impressão |
| `/api/printer/print/cancel` | POST | Cancelar impressão |
| `/api/server/files/list` | GET | Listar arquivos |
| `/api/server/files/metadata` | GET | Metadados de arquivo |

**Autenticação**: Header `X-Api-Key` (opcional)

---

### Diferenças vs Bambu Lab

| Aspecto | Bambu Lab | Klipper/Moonraker |
|---------|-----------|-------------------|
| **Protocolo** | MQTT + FTP | HTTP REST |
| **Porta** | 1883 (MQTT), 990 (FTP) | 7125 (HTTP) |
| **Formato** | `.gcode.3mf` (ZIP) | `.gcode` (texto) |
| **Metadados** | XML em arquivo separado | Comentários no GCode |
| **Biblioteca** | `bambu_networking.dll` (proprietária) | `axios` (open source) |
| **Descoberta** | SSDP + Bambu Cloud | mDNS/Bonjour |
| **Autenticação** | Token Bambu + senha local | API Key |

---

## 📊 Comparação com Estudo

Esta implementação segue **exatamente** as descobertas do estudo técnico:

| Documento de Estudo | Implementação |
|---------------------|---------------|
| `CREALITY_FILE_SENDING_STUDY.md` | `KlipperClient` |
| `GCODE_VS_3MF_METADATA.md` | Validação de `.gcode` |
| `CREALITY_NETWORK_COMPARISON.md` | Protocolo HTTP REST |
| `EXEMPLOS_PRATICOS_REDE.md` | `examples/` |

**Sem fallbacks, gambiarras ou código temporário** ✅

---

## 🚀 Próximos Passos

### Fase 1: Validação (Atual)
- ✅ Implementação completa
- ✅ Testes unitários
- ✅ Exemplos funcionais
- ✅ Documentação

### Fase 2: Testes Reais
- [ ] Testar com impressora K2 Plus real
- [ ] Validar upload e impressão
- [ ] Verificar metadados no Mainsail/Fluidd
- [ ] Testar múltiplas impressoras

### Fase 3: Melhorias
- [ ] Suporte a thumbnails (base64 em comentários)
- [ ] Websocket para status em tempo real
- [ ] Descoberta automática de impressoras (mDNS)
- [ ] Retry automático em caso de falha

### Fase 4: Integração
- [ ] Publicar no npm
- [ ] CI/CD para testes automatizados
- [ ] Documentação adicional
- [ ] Exemplos avançados

---

## 📚 Documentação Relacionada

1. **CREALITY_PRINT_STUDY.md** - Sistema de multicor
2. **CREALITY_FILE_SENDING_STUDY.md** - Sistema de rede
3. **GCODE_VS_3MF_METADATA.md** - Formato de arquivos
4. **CREALITY_NETWORK_COMPARISON.md** - Comparação de protocolos
5. **README_KLIPPER.md** - Documentação da API

---

## ✅ Checklist de Implementação

- [x] Cliente Moonraker (`KlipperClient`)
- [x] API de alto nível (`SliceAndSend`)
- [x] Tipos TypeScript
- [x] Testes automatizados
- [x] Exemplos práticos
- [x] Documentação completa
- [x] Integração com addon existente
- [x] Sem modificações no OrcaSlicer
- [x] Sem código temporário ou placeholders
- [x] Dependências instaladas via npm

---

**Status**: ✅ **Implementação Completa**

**Versão**: 1.0.0

**Data**: 2025-10-07

**Autor**: Implementação baseada no estudo técnico do CrealityPrint

