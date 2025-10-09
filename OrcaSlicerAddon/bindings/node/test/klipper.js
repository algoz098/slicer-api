/**
 * Test: Klipper integration
 * 
 * Tests KlipperClient and SliceAndSend functionality
 */

'use strict';

const assert = require('assert');
const path = require('path');
const fs = require('fs');

// Test configuration
const TEST_HOST = process.env.KLIPPER_TEST_HOST || 'k2plus.local';
const TEST_PORT = parseInt(process.env.KLIPPER_TEST_PORT || '7125');
const TEST_API_KEY = process.env.KLIPPER_TEST_API_KEY || '';
const SKIP_LIVE_TESTS = process.env.SKIP_KLIPPER_LIVE_TESTS === '1';

console.log('Klipper Integration Tests');
console.log('=========================\n');

// Test 1: Module loading
console.log('Test 1: Module loading');
try {
  const KlipperClient = require('../lib/klipper-client');
  assert(typeof KlipperClient === 'function', 'KlipperClient should be a constructor');
  console.log('✓ KlipperClient loaded\n');
} catch (err) {
  console.error('✗ Failed to load KlipperClient:', err.message);
  process.exit(1);
}

try {
  const SliceAndSend = require('../lib/slice-and-send');
  assert(typeof SliceAndSend === 'function', 'SliceAndSend should be a constructor');
  console.log('✓ SliceAndSend loaded\n');
} catch (err) {
  console.error('✗ Failed to load SliceAndSend:', err.message);
  process.exit(1);
}

// Test 2: KlipperClient instantiation
console.log('Test 2: KlipperClient instantiation');
try {
  const KlipperClient = require('../lib/klipper-client');
  
  // Should throw without host
  try {
    new KlipperClient({});
    assert.fail('Should throw without host');
  } catch (err) {
    assert(err.message.includes('host is required'), 'Should require host');
  }
  
  // Should succeed with host
  const client = new KlipperClient({
    host: TEST_HOST,
    port: TEST_PORT,
    apiKey: TEST_API_KEY
  });
  
  assert(client.host === TEST_HOST, 'Host should be set');
  assert(client.port === TEST_PORT, 'Port should be set');
  assert(client.baseURL.includes(TEST_HOST), 'Base URL should include host');
  
  console.log('✓ KlipperClient instantiation works\n');
} catch (err) {
  console.error('✗ KlipperClient instantiation failed:', err.message);
  process.exit(1);
}

// Test 3: SliceAndSend instantiation
console.log('Test 3: SliceAndSend instantiation');
try {
  const SliceAndSend = require('../lib/slice-and-send');
  
  // Should throw without addon
  try {
    new SliceAndSend(null);
    assert.fail('Should throw without addon');
  } catch (err) {
    assert(err.message.includes('OrcaSlicerAddon'), 'Should require addon');
  }
  
  // Mock addon
  const mockAddon = {
    slice: async () => ({ success: true })
  };
  
  const manager = new SliceAndSend(mockAddon);
  assert(manager.addon === mockAddon, 'Addon should be set');
  assert(manager.printers instanceof Map, 'Printers should be a Map');
  
  console.log('✓ SliceAndSend instantiation works\n');
} catch (err) {
  console.error('✗ SliceAndSend instantiation failed:', err.message);
  process.exit(1);
}

// Test 4: SliceAndSend printer management
console.log('Test 4: SliceAndSend printer management');
try {
  const SliceAndSend = require('../lib/slice-and-send');
  const mockAddon = { slice: async () => ({ success: true }) };
  const manager = new SliceAndSend(mockAddon);
  
  // Add printer
  const client = manager.addPrinter('test', {
    host: 'test.local',
    port: 7125
  });
  
  assert(client !== null, 'Should return client');
  assert(manager.listPrinters().includes('test'), 'Should list printer');
  
  // Get printer
  const retrieved = manager.getPrinter('test');
  assert(retrieved === client, 'Should retrieve same client');
  
  // Remove printer
  const removed = manager.removePrinter('test');
  assert(removed === true, 'Should remove printer');
  assert(!manager.listPrinters().includes('test'), 'Should not list removed printer');
  
  console.log('✓ Printer management works\n');
} catch (err) {
  console.error('✗ Printer management failed:', err.message);
  process.exit(1);
}

// Live tests (only if not skipped and printer is available)
if (!SKIP_LIVE_TESTS) {
  console.log('Live Tests (requires actual printer)');
  console.log('=====================================\n');
  console.log(`Target: ${TEST_HOST}:${TEST_PORT}`);
  console.log('Set SKIP_KLIPPER_LIVE_TESTS=1 to skip\n');
  
  // Test 5: Connection test
  (async () => {
    console.log('Test 5: Connection test');
    try {
      const KlipperClient = require('../lib/klipper-client');
      const client = new KlipperClient({
        host: TEST_HOST,
        port: TEST_PORT,
        apiKey: TEST_API_KEY,
        timeout: 5000
      });
      
      const result = await client.test();
      
      if (result.success) {
        console.log('✓ Connection successful');
        console.log(`  Version: ${result.version}`);
        console.log(`  Klippy connected: ${result.klippy_connected}`);
        console.log(`  Klippy state: ${result.klippy_state}\n`);
      } else {
        console.log('✗ Connection failed:', result.error);
        console.log('  (This is expected if printer is not available)\n');
      }
    } catch (err) {
      console.error('✗ Connection test error:', err.message, '\n');
    }
    
    // Test 6: Status query
    console.log('Test 6: Status query');
    try {
      const KlipperClient = require('../lib/klipper-client');
      const client = new KlipperClient({
        host: TEST_HOST,
        port: TEST_PORT,
        apiKey: TEST_API_KEY,
        timeout: 5000
      });
      
      const result = await client.getStatus();
      
      if (result.success) {
        console.log('✓ Status query successful');
        console.log(`  Bed: ${result.bed.temperature}°C / ${result.bed.target}°C`);
        console.log(`  Extruder: ${result.extruder.temperature}°C / ${result.extruder.target}°C`);
        console.log(`  Print state: ${result.print.state}\n`);
      } else {
        console.log('✗ Status query failed:', result.error);
        console.log('  (This is expected if printer is not available)\n');
      }
    } catch (err) {
      console.error('✗ Status query error:', err.message, '\n');
    }
    
    // Test 7: File list
    console.log('Test 7: File list');
    try {
      const KlipperClient = require('../lib/klipper-client');
      const client = new KlipperClient({
        host: TEST_HOST,
        port: TEST_PORT,
        apiKey: TEST_API_KEY,
        timeout: 5000
      });
      
      const result = await client.listFiles();
      
      if (result.success) {
        console.log('✓ File list successful');
        console.log(`  Found ${result.files.length} files`);
        if (result.files.length > 0) {
          console.log(`  First file: ${result.files[0].path}\n`);
        }
      } else {
        console.log('✗ File list failed:', result.error);
        console.log('  (This is expected if printer is not available)\n');
      }
    } catch (err) {
      console.error('✗ File list error:', err.message, '\n');
    }
    
    console.log('All tests completed!');
  })();
} else {
  console.log('Live tests skipped (SKIP_KLIPPER_LIVE_TESTS=1)\n');
  console.log('All tests completed!');
}

