# Filament Data Normalization System

## Overview

The Filament Data Normalization System provides dynamic type conversion and validation for filament configuration data based on OrcaSlicer's configuration definitions. This ensures that data returned by the API is properly typed and validated while maintaining compatibility with OrcaSlicer's configuration system.

## Key Features

- **Dynamic Type Detection**: Automatically detects and converts string values to appropriate types (numbers, booleans, arrays, percentages)
- **OrcaSlicer Compatibility**: Based on actual OrcaSlicer source code definitions from `PrintConfig.cpp`
- **Robust Validation**: Validates constraints (min/max values, enum values) and provides detailed error reporting
- **Graceful Error Handling**: Continues processing even when some fields have validation errors
- **Comprehensive Logging**: Provides detailed logs for debugging and monitoring

## How It Works

### 1. Type Definitions

The system uses type definitions extracted from OrcaSlicer's source code:

```typescript
// Example field definitions
filament_diameter: {
  type: ConfigOptionType.coFloats,
  label: 'Diameter',
  min: 0,
  default_value: [1.75],
  sidetext: 'mm'
}

filament_shrink: {
  type: ConfigOptionType.coPercents,
  label: 'Shrinkage (XY)',
  min: 10,
  default_value: [100],
  sidetext: '%'
}
```

### 2. Automatic Normalization

Raw JSON values are automatically converted to proper types:

**Input (from JSON file):**
```json
{
  "filament_diameter": ["1.75"],
  "nozzle_temperature": ["200"],
  "filament_shrink": ["95%"],
  "filament_soluble": ["0"]
}
```

**Output (normalized):**
```json
{
  "filament_diameter": [1.75],
  "nozzle_temperature": [200],
  "filament_shrink": [95],
  "filament_soluble": [false]
}
```

### 3. Validation and Error Reporting

The system validates data against OrcaSlicer constraints:

```json
{
  "success": false,
  "errors": [
    {
      "field": "filament_type",
      "errors": ["Invalid enum value \"CUSTOM\" for field filament_type[0]. Valid values: ABS, PLA, PETG, ..."]
    }
  ],
  "warnings": [
    {
      "field": "filament_diameter",
      "warnings": ["Single value converted to array for field filament_diameter"]
    }
  ]
}
```

## Supported Data Types

### Basic Types
- **Float**: `1.75` → `1.75`
- **Integer**: `"200"` → `200`
- **String**: `"PLA"` → `"PLA"`
- **Boolean**: `"1"`, `"true"`, `"yes"` → `true`

### Array Types
- **Float Array**: `["1.75", "2.0"]` → `[1.75, 2.0]`
- **Single to Array**: `"1.75"` → `[1.75]`

### Special Types
- **Percentage**: `"95%"` → `95`
- **Float or Percent**: Handles both numeric and percentage values

### Enum Validation
- **Filament Types**: Validates against OrcaSlicer's supported materials
- **Custom Enums**: Supports any enum defined in the configuration

## Configuration Sources

All type definitions are extracted from OrcaSlicer source code:

- **Source File**: `OrcaSlicer/src/libslic3r/PrintConfig.cpp`
- **Reference Lines**: Each field definition includes source line references
- **Update Process**: Documented process for updating definitions when OrcaSlicer changes

## API Integration

The normalization is automatically applied to the `fileContent` field in filament profile responses:

```bash
# Request
curl "http://localhost:3030/filaments-profile/filament_-_fdm_filament_common.json"

# Response includes normalized fileContent
{
  "id": "filament_-_fdm_filament_common.json",
  "name": "fdm_filament_common",
  "fileContent": {
    "filament_diameter": [1.75],    // Normalized from "1.75"
    "nozzle_temperature": [200],    // Normalized from "200"
    "filament_flow_ratio": [1.0],   // Normalized from "1"
    // ... other normalized fields
  }
}
```

## Error Handling

### Validation Errors
- **Invalid Enum Values**: Reports valid options
- **Out of Range**: Reports min/max constraints
- **Type Conversion**: Reports conversion failures

### Unknown Fields
- **Preservation**: Unknown fields are preserved as-is
- **Logging**: Unknown fields are logged for review
- **Future Compatibility**: Allows for new fields without breaking

### Graceful Degradation
- **Partial Success**: Processes valid fields even if some fail
- **Fallback Values**: Uses default values when appropriate
- **Error Isolation**: One field's error doesn't affect others

## Monitoring and Debugging

### Log Levels
- **Info**: Successful normalizations
- **Warn**: Validation errors and unknown fields
- **Debug**: Detailed normalization warnings

### Example Logs
```
2025-09-06T19:36:00.033Z [warn]: Filament profile normalization errors {
  "service": "FilamentsProfileService",
  "metadata": {
    "errors": [{
      "field": "filament_type",
      "errors": ["Invalid enum value \"COMMON\" for field filament_type[0]"]
    }],
    "errorCount": 1
  }
}
```

## Maintenance

### Updating Type Definitions

When OrcaSlicer updates its configuration system:

1. **Check Source**: Review changes in `PrintConfig.cpp`
2. **Update Definitions**: Modify `filament-config-types.ts`
3. **Document Changes**: Update source references and commit hashes
4. **Test Validation**: Ensure existing profiles still validate correctly

### Adding New Fields

To add support for new filament configuration fields:

1. **Find Definition**: Locate the field in OrcaSlicer source
2. **Add Type Definition**: Add to `FILAMENT_CONFIG_DEFINITIONS`
3. **Include Source Reference**: Document where the definition comes from
4. **Test Normalization**: Verify the field normalizes correctly

## Benefits

1. **Type Safety**: Ensures consistent data types across the API
2. **Validation**: Catches invalid configurations early
3. **Compatibility**: Maintains compatibility with OrcaSlicer
4. **Debugging**: Provides detailed error information
5. **Maintainability**: Clear documentation of data sources and types
6. **Extensibility**: Easy to add new fields and types

## Future Enhancements

- **Schema Generation**: Auto-generate TypeScript interfaces from definitions
- **Configuration Validation**: Validate entire printer configurations
- **Profile Comparison**: Compare profiles and highlight differences
- **Migration Tools**: Help migrate profiles between OrcaSlicer versions
