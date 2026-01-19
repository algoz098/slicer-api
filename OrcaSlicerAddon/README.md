# OrcaSlicer Addon

Node.js native addon (N-API) for headless 3D model slicing using OrcaSlicer.

## Installation

```bash
npm install orcaslicer-addon
```

## Quick Start

```javascript
const orca = require('orcaslicer-addon')

orca.initialize({
  resourcesPath: '/path/to/OrcaSlicer/resources',
  verbose: false
})

const result = await orca.slice({
  input: 'model.stl',
  output: 'output.gcode',
  options: {
    printer_model: 'Bambu Lab A1',
    nozzle_diameter: 0.4,
    layer_height: 0.2,
    wall_loops: 2,
    sparse_infill_density: 15,
    filament_type: 'PLA',
    nozzle_temperature: 220,
    bed_temperature: 60
  }
})

console.log('Output:', result.output)
console.log('Time:', result.estimatedTimeSec, 'seconds')
console.log('Filament:', result.filamentUsedGrams, 'grams')
```

## API Reference

### initialize(options?)

```javascript
orca.initialize({
  resourcesPath: '/path/to/OrcaSlicer/resources',
  verbose: false,
  vendors: ['BBL']
})
```

### slice(params)

```javascript
const result = await orca.slice({
  input: 'model.stl',
  output: 'output.gcode',
  plate: 1,
  printerProfile: 'Bambu Lab A1 0.4 nozzle',
  filamentProfile: 'Generic PLA @BBL A1',
  processProfile: '0.20mm Standard @BBL A1',
  options: {
    layer_height: 0.2,
    sparse_infill_density: 15,
    curr_bed_type: 'High Temp Plate'
  },
  center: false,
  autoRealignIfNeeded: false,
  verbose: false,
  dryRun: false,
  transferPrinterCustomizations: true,
  transferFilamentCustomizations: true,
  transferProcessCustomizations: true,
  transferProjectOverrides: true
})
```

**Returns:**
```javascript
{
  output: '/path/to/output.gcode',
  usedOptions: ['layer_height', 'wall_loops'],
  ignoredOptions: [],
  estimatedTimeSec: 3600,
  filamentUsedGrams: 25.5
}
```

### getModelInfo(file)

```javascript
const info = await orca.getModelInfo('model.stl')
```

### version()

```javascript
const ver = orca.version()
```

### Lazy Loading

```javascript
orca.loadVendor('BBL')
orca.loadPrinterProfile('Bambu Lab A1 0.4 nozzle')
orca.loadFilamentProfile('Generic PLA @BBL A1')
orca.loadProcessProfile('0.20mm Standard @BBL A1')
```

## Building from Source

```bash
cd OrcaSlicerAddon/bindings/node
npm ci
npm run configure
npm run build
```

## Output Formats

- `.gcode` - Plain G-code file
- `.gcode.3mf` - Production 3MF with embedded G-code

## Configuration Priority

`options` > `profiles` > `3MF embedded` > `defaults`

## License

Same license as OrcaSlicer.
