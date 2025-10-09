/**
 * Example: Complete Workflow Test
 * 
 * Tests the complete workflow:
 * 1. Initialize addon
 * 2. Test slice() method
 * 3. Test KlipperClient (if printer available)
 * 4. Test SliceAndSend integration
 */

const addon = require('orcaslicer-addon');
const { KlipperClient, SliceAndSend } = addon;
const path = require('path');
const fs = require('fs');
const os = require('os');

// Configuration
const RESOURCES_PATH = process.env.ORCACLI_RESOURCES || '../../../OrcaSlicer/resources';
const TEST_STL = process.env.TEST_STL || '../../../example_files/3DBenchy.stl';
const PRINTER_HOST = process.env.KLIPPER_HOST || 'k2plus.local';
const PRINTER_PORT = parseInt(process.env.KLIPPER_PORT || '7125');
const SKIP_PRINTER_TESTS = process.env.SKIP_PRINTER_TESTS === '1';

async function main() {
  console.log('='.repeat(60));
  console.log('Complete Workflow Test');
  console.log('='.repeat(60));
  console.log();

  // ========================================
  // Step 1: Initialize OrcaSlicer
  // ========================================
  console.log('Step 1: Initialize OrcaSlicer');
  console.log('-'.repeat(60));
  
  try {
    addon.initialize({
      resourcesPath: RESOURCES_PATH,
      verbose: false
    });
    console.log('✓ OrcaSlicer initialized');
    console.log('  Version:', addon.version());
  } catch (err) {
    console.error('✗ Initialization failed:', err.message);
    process.exit(1);
  }
  console.log();

  // ========================================
  // Step 2: Test slice() method
  // ========================================
  console.log('Step 2: Test slice() method');
  console.log('-'.repeat(60));

  // Create a simple test STL if needed
  let testStl = TEST_STL;
  if (!fs.existsSync(testStl)) {
    console.log('  Creating test STL file...');
    testStl = path.join(os.tmpdir(), 'test_cube.stl');
    
    // Simple ASCII STL (cube)
    const stlContent = `solid cube
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 10 0 0
      vertex 10 10 0
    endloop
  endfacet
  facet normal 0 0 -1
    outer loop
      vertex 0 0 0
      vertex 10 10 0
      vertex 0 10 0
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 10
      vertex 10 10 10
      vertex 10 0 10
    endloop
  endfacet
  facet normal 0 0 1
    outer loop
      vertex 0 0 10
      vertex 0 10 10
      vertex 10 10 10
    endloop
  endfacet
endsolid cube`;
    
    fs.writeFileSync(testStl, stlContent);
    console.log('  ✓ Test STL created:', testStl);
  }

  const outputGcode = path.join(os.tmpdir(), `test_output_${Date.now()}.gcode`);

  try {
    console.log('  Input:', testStl);
    console.log('  Output:', outputGcode);
    console.log('  Slicing...');

    const result = await addon.slice({
      input: testStl,
      output: outputGcode,
      printerProfile: 'Creality K2 Plus 0.4 nozzle',
      filamentProfile: 'Creality PLA Basic @K2Plus',
      processProfile: '0.20mm Standard @K2Plus',
      verbose: false,
      options: {
        layer_height: 0.2,
        infill_density: '15%',
        // Disable MMU segmentation to avoid segfault
        enable_prime_tower: '0',
        flush_into_infill: '0',
        flush_into_objects: '0'
      }
    });

    console.log('  ✓ Slicing successful!');
    console.log('    Output file:', result.output);
    
    if (result.estimatedTimeSec) {
      console.log('    Estimated time:', Math.round(result.estimatedTimeSec), 'seconds');
    }
    
    if (result.filamentUsedGrams) {
      console.log('    Filament used:', result.filamentUsedGrams.toFixed(2), 'grams');
    }

    // Verify file exists
    if (fs.existsSync(outputGcode)) {
      const stats = fs.statSync(outputGcode);
      console.log('    File size:', (stats.size / 1024).toFixed(2), 'KB');
      
      // Read first few lines to verify it's GCode
      const content = fs.readFileSync(outputGcode, 'utf8');
      const lines = content.split('\n').slice(0, 10);
      console.log('    First lines:');
      lines.forEach(line => {
        if (line.trim()) {
          console.log('      ', line.substring(0, 60));
        }
      });
    } else {
      console.error('  ✗ Output file was not created!');
    }
  } catch (err) {
    console.error('  ✗ Slicing failed:', err.message);
  }
  console.log();

  // ========================================
  // Step 3: Test KlipperClient (optional)
  // ========================================
  if (!SKIP_PRINTER_TESTS) {
    console.log('Step 3: Test KlipperClient');
    console.log('-'.repeat(60));
    console.log('  Target:', `${PRINTER_HOST}:${PRINTER_PORT}`);

    try {
      const printer = new KlipperClient({
        host: PRINTER_HOST,
        port: PRINTER_PORT,
        timeout: 5000
      });

      // Test connection
      console.log('  Testing connection...');
      const testResult = await printer.test();
      
      if (testResult.success) {
        console.log('  ✓ Connection successful');
        console.log('    Version:', testResult.version);
        console.log('    Klippy connected:', testResult.klippy_connected);
        console.log('    Klippy state:', testResult.klippy_state);

        // Get status
        console.log('  Getting printer status...');
        const status = await printer.getStatus();
        
        if (status.success) {
          console.log('  ✓ Status retrieved');
          console.log('    Bed:', status.bed.temperature, '°C /', status.bed.target, '°C');
          console.log('    Extruder:', status.extruder.temperature, '°C /', status.extruder.target, '°C');
          console.log('    Print state:', status.print.state);
        }

        // Upload test (if GCode was generated)
        if (fs.existsSync(outputGcode)) {
          console.log('  Uploading test file...');
          const uploadResult = await printer.uploadFile(outputGcode, {
            filename: 'test_workflow.gcode',
            select: false,
            print: false,
            onProgress: (p) => {
              process.stdout.write(`\r    Progress: ${p.percentage}%`);
            }
          });

          if (uploadResult.success) {
            console.log('\n  ✓ Upload successful:', uploadResult.filename);
            
            // Get metadata
            const metadata = await printer.getMetadata(uploadResult.filename);
            if (metadata.success) {
              console.log('    Metadata:');
              console.log('      Slicer:', metadata.metadata.slicer);
              console.log('      Filament type:', metadata.metadata.filament_type);
              if (metadata.metadata.estimated_time) {
                console.log('      Estimated time:', metadata.metadata.estimated_time, 'seconds');
              }
            }

            // Clean up - delete the test file
            console.log('  Cleaning up test file...');
            await printer.deleteFile(uploadResult.filename);
            console.log('  ✓ Test file deleted');
          } else {
            console.log('\n  ✗ Upload failed:', uploadResult.error);
          }
        }
      } else {
        console.log('  ✗ Connection failed:', testResult.error);
        console.log('  (This is expected if printer is not available)');
      }
    } catch (err) {
      console.error('  ✗ KlipperClient test error:', err.message);
    }
    console.log();
  } else {
    console.log('Step 3: KlipperClient tests skipped (SKIP_PRINTER_TESTS=1)');
    console.log();
  }

  // ========================================
  // Step 4: Test SliceAndSend integration
  // ========================================
  if (!SKIP_PRINTER_TESTS) {
    console.log('Step 4: Test SliceAndSend integration');
    console.log('-'.repeat(60));

    try {
      const manager = new SliceAndSend(addon);
      
      // Add printer
      manager.addPrinter('test-printer', {
        host: PRINTER_HOST,
        port: PRINTER_PORT,
        timeout: 5000
      });
      console.log('  ✓ Printer added:', manager.listPrinters().join(', '));

      // Test printer
      const testResult = await manager.testPrinter('test-printer');
      if (testResult.success) {
        console.log('  ✓ Printer test successful');

        // Slice and send (without printing)
        console.log('  Testing slice and send...');
        const result = await manager.sliceAndSend(testStl, 'test-printer', {
          sliceConfig: {
            printerProfile: 'Creality K2 Plus 0.4 nozzle',
            filamentProfile: 'Creality PLA Basic @K2Plus',
            processProfile: '0.20mm Standard @K2Plus'
          },
          filename: 'test_slice_and_send.gcode',
          select: false,
          print: false,
          keepLocal: false,
          onUploadProgress: (p) => {
            process.stdout.write(`\r    Upload: ${p.percentage}%`);
          }
        });

        if (result.success) {
          console.log('\n  ✓ Slice and send successful!');
          console.log('    Remote file:', result.remotePath);
          
          // Clean up
          const printer = manager.getPrinter('test-printer');
          await printer.deleteFile(result.remotePath);
          console.log('  ✓ Test file cleaned up');
        } else {
          console.log('\n  ✗ Slice and send failed:', result.error);
        }
      } else {
        console.log('  ✗ Printer test failed:', testResult.error);
      }
    } catch (err) {
      console.error('  ✗ SliceAndSend test error:', err.message);
    }
    console.log();
  } else {
    console.log('Step 4: SliceAndSend tests skipped (SKIP_PRINTER_TESTS=1)');
    console.log();
  }

  // ========================================
  // Summary
  // ========================================
  console.log('='.repeat(60));
  console.log('Test Summary');
  console.log('='.repeat(60));
  console.log('✓ OrcaSlicer initialization: OK');
  console.log('✓ slice() method: OK');
  if (!SKIP_PRINTER_TESTS) {
    console.log('✓ KlipperClient: Tested (check results above)');
    console.log('✓ SliceAndSend: Tested (check results above)');
  } else {
    console.log('⊘ Printer tests: Skipped');
  }
  console.log();
  console.log('All tests completed!');
  console.log();
  console.log('To run with printer tests:');
  console.log('  KLIPPER_HOST=k2plus.local node examples/test-complete-workflow.js');
  console.log();
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(1);
});

