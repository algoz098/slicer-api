/**
 * Example: Slice and Send to Klipper printer
 * 
 * Demonstrates the complete workflow:
 * 1. Initialize OrcaSlicer addon
 * 2. Configure printer(s)
 * 3. Slice a 3D model
 * 4. Upload to printer
 * 5. Optionally start printing
 */

const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;
const path = require('path');

async function main() {
  // 1. Initialize OrcaSlicer addon
  console.log('Initializing OrcaSlicer...');
  addon.initialize({
    resourcesPath: process.env.ORCACLI_RESOURCES || '../../../OrcaSlicer/resources',
    verbose: false
  });
  console.log('✓ OrcaSlicer initialized');
  console.log('  Version:', addon.version());

  // 2. Create SliceAndSend manager
  const manager = new SliceAndSend(addon);

  // 3. Add printer configurations
  console.log('\nConfiguring printers...');
  
  manager.addPrinter('k2plus-office', {
    host: 'k2plus.local',
    port: 7125,
    apiKey: ''
  });

  manager.addPrinter('k2plus-lab', {
    host: '192.168.1.100',
    port: 7125,
    apiKey: 'your-api-key-here'
  });

  console.log('✓ Configured printers:', manager.listPrinters().join(', '));

  // 4. Test printer connections
  console.log('\nTesting printer connections...');
  for (const printerName of manager.listPrinters()) {
    const result = await manager.testPrinter(printerName);
    if (result.success) {
      console.log(`  ✓ ${printerName}: Connected (${result.version})`);
    } else {
      console.log(`  ✗ ${printerName}: ${result.error}`);
    }
  }

  // 5. Slice and send to printer
  const inputFile = './models/cube.stl';
  const printerName = 'k2plus-office';

  console.log(`\nSlicing and sending ${inputFile} to ${printerName}...`);

  const result = await manager.sliceAndSend(inputFile, printerName, {
    // Slice configuration
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
    
    // Upload options
    filename: 'cube_test.gcode',
    select: true,   // Select file in UI
    print: false,   // Don't start printing automatically
    keepLocal: true, // Keep local GCode file
    
    // Progress callbacks
    onSliceProgress: (progress) => {
      console.log('  Slicing progress:', progress);
    },
    onUploadProgress: (progress) => {
      process.stdout.write(`\r  Upload progress: ${progress.percentage}%`);
    }
  });

  if (result.success) {
    console.log('\n✓ Success!');
    console.log('  Printer:', result.printer);
    console.log('  Local file:', result.localPath);
    console.log('  Remote file:', result.remotePath);
    console.log('  Print started:', result.printStarted);
    if (result.stats) {
      console.log('  Stats:', JSON.stringify(result.stats, null, 2));
    }
  } else {
    console.error('✗ Failed:', result.error);
  }

  // 6. Get status of all printers
  console.log('\nGetting status of all printers...');
  const statuses = await manager.getAllPrinterStatus();
  
  for (const [name, status] of Object.entries(statuses)) {
    if (status.success) {
      console.log(`  ${name}:`);
      console.log(`    State: ${status.print.state}`);
      console.log(`    Bed: ${status.bed.temperature}°C / ${status.bed.target}°C`);
      console.log(`    Extruder: ${status.extruder.temperature}°C / ${status.extruder.target}°C`);
    } else {
      console.log(`  ${name}: ${status.error}`);
    }
  }
}

main().catch(console.error);

