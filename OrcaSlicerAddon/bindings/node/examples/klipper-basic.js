/**
 * Example: Basic Klipper/Moonraker usage
 * 
 * Demonstrates how to connect to a Klipper printer and perform basic operations
 */

const { KlipperClient } = require('orcaslicer-addon');

async function main() {
  // Create client for K2 Plus printer
  const printer = new KlipperClient({
    host: 'k2plus.local',  // or IP: '192.168.1.100'
    port: 7125,            // default Moonraker port
    apiKey: '',            // optional API key
    ssl: false,            // use HTTPS
    timeout: 30000         // 30 seconds
  });

  console.log('Testing connection...');
  const testResult = await printer.test();
  
  if (!testResult.success) {
    console.error('Connection failed:', testResult.error);
    return;
  }

  console.log('✓ Connected to Moonraker');
  console.log('  Version:', testResult.version);
  console.log('  Klippy connected:', testResult.klippy_connected);
  console.log('  Klippy state:', testResult.klippy_state);

  // Get printer status
  console.log('\nGetting printer status...');
  const status = await printer.getStatus();
  
  if (status.success) {
    console.log('✓ Printer status:');
    console.log('  Bed:', status.bed.temperature, '°C /', status.bed.target, '°C');
    console.log('  Extruder:', status.extruder.temperature, '°C /', status.extruder.target, '°C');
    console.log('  Print state:', status.print.state);
    console.log('  Position: X:', status.position.x, 'Y:', status.position.y, 'Z:', status.position.z);
  }

  // List files
  console.log('\nListing files...');
  const files = await printer.listFiles();
  
  if (files.success) {
    console.log(`✓ Found ${files.files.length} files:`);
    files.files.slice(0, 5).forEach(file => {
      console.log(`  - ${file.path} (${(file.size / 1024).toFixed(2)} KB)`);
    });
  }

  // Upload a file (example)
  const gcodeFile = './test.gcode';
  console.log(`\nUploading ${gcodeFile}...`);
  
  const uploadResult = await printer.uploadFile(gcodeFile, {
    filename: 'test_upload.gcode',
    select: false,
    print: false,
    onProgress: (progress) => {
      process.stdout.write(`\r  Progress: ${progress.percentage}%`);
    }
  });

  if (uploadResult.success) {
    console.log('\n✓ Upload successful:', uploadResult.filename);
    
    // Get metadata
    const metadata = await printer.getMetadata(uploadResult.filename);
    if (metadata.success) {
      console.log('  Metadata:');
      console.log('    Slicer:', metadata.metadata.slicer);
      console.log('    Filament:', metadata.metadata.filament_type);
      console.log('    Estimated time:', metadata.metadata.estimated_time, 'seconds');
    }
  } else {
    console.error('✗ Upload failed:', uploadResult.error);
  }
}

main().catch(console.error);

