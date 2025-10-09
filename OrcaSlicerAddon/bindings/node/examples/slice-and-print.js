/**
 * Example: Slice and Print (one-shot)
 * 
 * Simplest workflow: slice a model and immediately start printing
 */

const addon = require('orcaslicer-addon');
const { SliceAndSend } = addon;

async function main() {
  // Initialize
  addon.initialize({
    resourcesPath: process.env.ORCACLI_RESOURCES || '../../../OrcaSlicer/resources'
  });

  // Create manager and add printer
  const manager = new SliceAndSend(addon);
  manager.addPrinter('k2plus', {
    host: 'k2plus.local',
    port: 7125
  });

  // Slice and print in one call
  console.log('Slicing and printing...');
  
  const result = await manager.sliceAndPrint('./models/benchy.stl', 'k2plus', {
    sliceConfig: {
      printerProfile: 'Creality K2 Plus 0.4 nozzle',
      filamentProfile: 'Creality PLA Basic @K2Plus',
      processProfile: '0.20mm Standard @K2Plus'
    },
    filename: 'benchy.gcode',
    onUploadProgress: (p) => process.stdout.write(`\rUploading: ${p.percentage}%`)
  });

  if (result.success) {
    console.log('\n✓ Print started!');
    console.log('  File:', result.remotePath);
  } else {
    console.error('\n✗ Failed:', result.error);
  }
}

main().catch(console.error);

