// Direct test of the compiled addon
const path = require('path')

try {
  // Try to load the addon directly
  const addonPath = path.join(__dirname, '../native/orcaslicer_core/build/Release/orcaslicer_core.node')
  console.log('Trying to load addon from:', addonPath)
  
  const addon = require(addonPath)
  console.log('Addon loaded successfully!')
  console.log('Available functions:', Object.keys(addon))
  
  if (addon.slice_to_gcode) {
    const result = addon.slice_to_gcode('/tmp/test.3mf', '{}')
    console.log('slice_to_gcode result:')
    console.log(result.split('\n').slice(0, 10))
  }
} catch (error) {
  console.error('Failed to load addon:', error.message)
  
  // Try with bindings
  try {
    const bindings = require('bindings')
    const addon = bindings('orcaslicer_core')
    console.log('Addon loaded via bindings!')
    console.log('Available functions:', Object.keys(addon))
  } catch (bindingsError) {
    console.error('Failed to load via bindings:', bindingsError.message)
  }
}
