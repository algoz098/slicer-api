# Klipper/Moonraker Integration for OrcaSlicerAddon

## 🎯 Overview

This addon extends OrcaSlicerAddon with **native Klipper/Moonraker support**, enabling seamless integration with Klipper-based printers like **Creality K2 Plus**.

### Features

- ✅ **Direct upload** to Klipper printers via Moonraker API
- ✅ **Slice and send** workflow (slice → upload → print)
- ✅ **Printer management** (multiple printers, status monitoring)
- ✅ **Progress tracking** (slicing and upload progress)
- ✅ **File management** (list, upload, delete, metadata)
- ✅ **Print control** (start, pause, resume, cancel)
- ✅ **TypeScript support** (full type definitions)
- ✅ **No external dependencies** on OrcaSlicer modifications

---

## 📦 Installation

```bash
npm install orcaslicer-addon
# or
yarn add orcaslicer-addon
```

**Dependencies** (automatically installed):
- `axios` - HTTP client
- `form-data` - Multipart form uploads

---

## 🚀 Quick Start

### 1. Basic Klipper Client

```javascript
const { KlipperClient } = require('orcaslicer-addon');

const printer = new KlipperClient({
  host: 'k2plus.local',  // or IP: '192.168.1.100'
  port: 7125,            // default Moonraker port
  apiKey: ''             // optional
});

// Test connection
const result = await printer.test();
console.log('Connected:', result.success);

// Get status
const status = await printer.getStatus();
console.log('Bed temp:', status.bed.temperature);

// Upload file
await printer.uploadFile('./model.gcode', {
  filename: 'my_print.gcode',
  print: true  // start printing immediately
});
```

---

### 2. Slice and Send

```javascript
const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;

// Initialize OrcaSlicer
addon.initialize({
  resourcesPath: './OrcaSlicer/resources'
});

// Create manager
const manager = new SliceAndSend(addon);

// Add printer
manager.addPrinter('k2plus', {
  host: 'k2plus.local',
  port: 7125
});

// Slice and send
const result = await manager.sliceAndSend('./model.stl', 'k2plus', {
  sliceConfig: {
    printerProfile: 'Creality K2 Plus 0.4 nozzle',
    filamentProfile: 'Creality PLA Basic @K2Plus',
    processProfile: '0.20mm Standard @K2Plus'
  },
  print: true  // start printing after upload
});

console.log('Success:', result.success);
```

---

### 3. One-Shot Print

```javascript
const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;

addon.initialize({ resourcesPath: './OrcaSlicer/resources' });

const manager = new SliceAndSend(addon);
manager.addPrinter('k2plus', { host: 'k2plus.local' });

// Slice and print in one call
await manager.sliceAndPrint('./benchy.stl', 'k2plus', {
  sliceConfig: {
    printerProfile: 'Creality K2 Plus 0.4 nozzle'
  }
});
```

---

## 📚 API Reference

### KlipperClient

#### Constructor

```typescript
new KlipperClient(config: KlipperConfig)
```

**Config**:
- `host` (string, required): Printer hostname or IP
- `port` (number, optional): Moonraker port (default: 7125)
- `apiKey` (string, optional): API Key for authentication
- `ssl` (boolean, optional): Use HTTPS (default: false)
- `timeout` (number, optional): Request timeout in ms (default: 30000)

---

#### Methods

##### `test(): Promise<TestResult>`

Test connection to Moonraker.

**Returns**:
```typescript
{
  success: boolean;
  version?: string;
  klippy_connected?: boolean;
  klippy_state?: string;
  error?: string;
}
```

---

##### `getStatus(): Promise<PrinterStatus>`

Get current printer status.

**Returns**:
```typescript
{
  success: boolean;
  bed?: { temperature: number; target: number };
  extruder?: { temperature: number; target: number };
  print?: { state: string; filename: string; duration: number; progress: number };
  position?: { x: number; y: number; z: number };
  error?: string;
}
```

---

##### `uploadFile(filePath: string, options?: UploadOptions): Promise<UploadResult>`

Upload GCode file to printer.

**Options**:
- `filename` (string): Remote filename
- `select` (boolean): Select file after upload
- `print` (boolean): Start printing after upload
- `onProgress` (function): Progress callback

**Returns**:
```typescript
{
  success: boolean;
  filename?: string;
  print_started?: boolean;
  error?: string;
}
```

---

##### `startPrint(filename: string): Promise<SimpleResult>`

Start printing a file.

---

##### `pausePrint(): Promise<SimpleResult>`

Pause current print.

---

##### `resumePrint(): Promise<SimpleResult>`

Resume current print.

---

##### `cancelPrint(): Promise<SimpleResult>`

Cancel current print.

---

##### `deleteFile(filename: string): Promise<SimpleResult>`

Delete a file from printer.

---

##### `listFiles(root?: string): Promise<FileListResult>`

List files on printer.

---

##### `getMetadata(filename: string): Promise<MetadataResult>`

Get file metadata (slicer info, filament type, estimated time, etc.).

---

### SliceAndSend

#### Constructor

```typescript
new SliceAndSend(addon: OrcaSlicerAddon)
```

---

#### Methods

##### `addPrinter(name: string, config: KlipperConfig): KlipperClient`

Add a printer configuration.

---

##### `getPrinter(name: string): KlipperClient | undefined`

Get printer client by name.

---

##### `listPrinters(): string[]`

List all configured printers.

---

##### `testPrinter(name: string): Promise<TestResult>`

Test connection to a printer.

---

##### `slice(inputPath: string, options?: SliceOptions): Promise<SliceResult>`

Slice a 3D model.

**Options**:
- `outputPath` (string): Output GCode path (auto-generated if not provided)
- `config` (object): Slicer configuration overrides
- `onProgress` (function): Progress callback

---

##### `sliceAndSend(inputPath: string, printerName: string, options?: SliceAndSendOptions): Promise<SliceAndSendResult>`

Slice and upload to printer.

**Options**:
- `sliceConfig` (object): Slicer configuration
- `filename` (string): Remote filename
- `select` (boolean): Select file after upload
- `print` (boolean): Start printing after upload
- `keepLocal` (boolean): Keep local GCode file after upload
- `onSliceProgress` (function): Slice progress callback
- `onUploadProgress` (function): Upload progress callback

---

##### `sliceAndPrint(inputPath: string, printerName: string, options?: SliceAndSendOptions): Promise<SliceAndSendResult>`

Slice, upload, and start printing (convenience method).

---

##### `getAllPrinterStatus(): Promise<Record<string, PrinterStatus>>`

Get status of all configured printers.

---

##### `sendFile(gcodeFilePath: string, printerName: string, options?: UploadOptions): Promise<UploadResult>`

Upload existing GCode file to printer.

---

## 🎨 Examples

See `examples/` directory:
- `klipper-basic.js` - Basic Klipper client usage
- `slice-and-send.js` - Complete workflow with multiple printers
- `slice-and-print.js` - One-shot slice and print

---

## 🔧 Configuration

### Printer Discovery

**mDNS/Bonjour** (recommended):
```javascript
const printer = new KlipperClient({
  host: 'k2plus.local'  // .local domain
});
```

**Static IP**:
```javascript
const printer = new KlipperClient({
  host: '192.168.1.100'
});
```

---

### API Key Authentication

Generate API key in Moonraker:
```bash
# SSH into printer
ssh pi@k2plus.local

# Generate key
~/moonraker/scripts/generate-api-key.sh
```

Use in client:
```javascript
const printer = new KlipperClient({
  host: 'k2plus.local',
  apiKey: 'your-api-key-here'
});
```

---

### Multiple Printers

```javascript
const manager = new SliceAndSend(addon);

manager.addPrinter('office', { host: 'k2plus-office.local' });
manager.addPrinter('lab', { host: '192.168.1.100' });
manager.addPrinter('workshop', { host: 'k2plus-workshop.local' });

// Send to specific printer
await manager.sliceAndSend('./model.stl', 'office', { ... });
```

---

## 🐛 Troubleshooting

### Connection Failed

**Error**: `ECONNREFUSED` or `ETIMEDOUT`

**Solutions**:
1. Check printer is powered on and connected to network
2. Verify hostname/IP is correct
3. Ensure Moonraker is running: `systemctl status moonraker`
4. Check firewall allows port 7125

---

### Upload Failed

**Error**: `Only .gcode files are supported`

**Solution**: Klipper only accepts plain GCode files, not `.3mf` or `.gcode.3mf`. Use the `slice()` method first to generate GCode.

---

### Metadata Not Available

**Error**: Metadata returns empty

**Solution**: Ensure GCode file has proper header comments. OrcaSlicerAddon automatically generates these.

---

## 📖 Technical Details

### File Format

Klipper accepts **plain GCode** files (`.gcode`), not `.3mf` or `.gcode.3mf`.

Metadata is embedded as **comments in the GCode header**:

```gcode
; HEADER_BLOCK_START
; generated by OrcaSlicer
; filament_type = PLA;PLA
; filament_colour = #FF0000;#0000FF
; nozzle_temperature = 210,210
; HEADER_BLOCK_END

; CONFIG_BLOCK_START
; layer_height = 0.2
; infill_density = 15%
; CONFIG_BLOCK_END

M140 S60
M104 S210
...
```

Moonraker parses these comments to extract metadata.

---

### Network Protocol

**Moonraker API** (HTTP REST):
- **Upload**: `POST /api/files/local` (multipart/form-data)
- **Status**: `GET /api/printer/objects/query`
- **Control**: `POST /api/printer/print/{start|pause|resume|cancel}`
- **Files**: `GET /api/server/files/list`

**Authentication**: `X-Api-Key` header (optional)

---

## 🔗 Related Documentation

- [CREALITY_FILE_SENDING_STUDY.md](../../../CREALITY_FILE_SENDING_STUDY.md) - Technical analysis
- [GCODE_VS_3MF_METADATA.md](../../../GCODE_VS_3MF_METADATA.md) - File format comparison
- [Moonraker API Documentation](https://moonraker.readthedocs.io/en/latest/web_api/)

---

## 📝 License

AGPL-3.0-only (same as OrcaSlicer)

---

## 🤝 Contributing

Contributions welcome! Please ensure:
- No placeholders or temporary code
- Real implementations only
- Tests pass
- Documentation updated

