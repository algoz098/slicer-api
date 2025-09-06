/**
 * Filament Configuration Types and Validation
 * 
 * This file contains type definitions and validation logic for filament configuration
 * parameters, based on the OrcaSlicer source code definitions.
 * 
 * Source: OrcaSlicer/src/libslic3r/PrintConfig.cpp
 * Last updated from OrcaSlicer commit: [TO BE FILLED]
 * 
 * The types are extracted from the ConfigOptionDef definitions in PrintConfig.cpp
 * to ensure compatibility with OrcaSlicer's configuration system.
 */

export enum ConfigOptionType {
  // Basic types
  coFloat = 'float',
  coFloats = 'floats',
  coInt = 'int',
  coInts = 'ints',
  coString = 'string',
  coStrings = 'strings',
  coBool = 'bool',
  coBools = 'bools',
  
  // Special types
  coPercent = 'percent',
  coPercents = 'percents',
  coFloatOrPercent = 'floatOrPercent',
  coFloatsOrPercents = 'floatsOrPercents',
  
  // Complex types
  coPoint = 'point',
  coPoints = 'points',
  coEnum = 'enum',
  coEnums = 'enums'
}

export interface ConfigOptionDef {
  type: ConfigOptionType
  label: string
  tooltip?: string
  sidetext?: string
  min?: number
  max?: number
  mode?: string
  category?: string
  enum_values?: string[]
  enum_labels?: string[]
  default_value?: any
  nullable?: boolean
  multiline?: boolean
  gui_type?: string
}

/**
 * Filament configuration field definitions extracted from OrcaSlicer PrintConfig.cpp
 * 
 * Each field definition includes:
 * - type: The ConfigOption type from OrcaSlicer
 * - validation rules (min, max, etc.)
 * - metadata (label, tooltip, etc.)
 * 
 * Source references are included for each field to track where in the OrcaSlicer
 * source code the definition comes from.
 */
export const FILAMENT_CONFIG_DEFINITIONS: Record<string, ConfigOptionDef> = {
  // Basic filament properties
  filament_type: {
    type: ConfigOptionType.coStrings,
    label: 'Type',
    tooltip: 'The material type of filament',
    enum_values: [
      'ABS', 'ABS-GF', 'ASA', 'HIPS', 'PA', 'PA-CF', 'PA-GF', 'PA6-CF', 'PC', 
      'PETG', 'PLA', 'PLA-AERO', 'PLA-CF', 'PLA-GF', 'PLA-Marble', 'PLA-Metal', 
      'PLA-Pearl', 'PLA-Silk', 'PLA-Wood', 'PP', 'PVA', 'PVDF', 'SBS', 'TPU'
    ],
    default_value: ['PLA'],
    // Source: PrintConfig.cpp:2210-2254
  },

  filament_diameter: {
    type: ConfigOptionType.coFloats,
    label: 'Diameter',
    tooltip: 'Filament diameter is used to calculate extrusion in gcode, so it\'s important and should be accurate',
    sidetext: 'mm',
    min: 0,
    default_value: [1.75],
    // Source: PrintConfig.cpp:2014-2019
  },

  filament_density: {
    type: ConfigOptionType.coFloats,
    label: 'Density',
    tooltip: 'Filament density. For statistics only',
    sidetext: 'g/cm³',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp:2202-2208
  },

  filament_cost: {
    type: ConfigOptionType.coFloats,
    label: 'Price',
    tooltip: 'Filament price. For statistics only',
    sidetext: 'money/kg',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp:2276-2282
  },

  // Temperature settings
  nozzle_temperature: {
    type: ConfigOptionType.coInts,
    label: 'Nozzle temperature',
    tooltip: 'Nozzle temperature for this filament',
    sidetext: '°C',
    min: 0,
    max: 500,
    default_value: [200],
    // Source: PrintConfig.cpp (temperature definitions)
  },

  nozzle_temperature_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Initial layer',
    tooltip: 'Nozzle temperature to print initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 500,
    default_value: [200],
    // Source: PrintConfig.cpp:2664-2671
  },

  nozzle_temperature_range_low: {
    type: ConfigOptionType.coInts,
    label: 'Min',
    tooltip: 'Minimum nozzle temperature for this filament',
    sidetext: '°C',
    min: 0,
    max: 500,
    default_value: [190],
    // Source: PrintConfig.cpp:5126-5132
  },

  nozzle_temperature_range_high: {
    type: ConfigOptionType.coInts,
    label: 'Max',
    tooltip: 'Maximum nozzle temperature for this filament',
    sidetext: '°C',
    min: 0,
    max: 500,
    default_value: [250],
    // Source: PrintConfig.cpp:5134-5139
  },

  bed_temperature: {
    type: ConfigOptionType.coInts,
    label: 'Bed temperature',
    tooltip: 'Bed temperature for this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (bed temperature definitions)
  },

  bed_temperature_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Initial layer bed temperature',
    tooltip: 'Bed temperature for the initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (bed temperature definitions)
  },

  // Plate temperature settings (Bambu Lab specific)
  hot_plate_temp: {
    type: ConfigOptionType.coInts,
    label: 'Hot plate temperature',
    tooltip: 'Temperature for the hot plate when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (hot plate temperature definitions)
  },

  hot_plate_temp_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Hot plate initial layer temperature',
    tooltip: 'Temperature for the hot plate on initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (hot plate temperature definitions)
  },

  cool_plate_temp: {
    type: ConfigOptionType.coInts,
    label: 'Cool plate temperature',
    tooltip: 'Temperature for the cool plate when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [35],
    // Source: PrintConfig.cpp (cool plate temperature definitions)
  },

  cool_plate_temp_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Cool plate initial layer temperature',
    tooltip: 'Temperature for the cool plate on initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [35],
    // Source: PrintConfig.cpp (cool plate temperature definitions)
  },

  textured_plate_temp: {
    type: ConfigOptionType.coInts,
    label: 'Textured plate temperature',
    tooltip: 'Temperature for the textured plate when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [50],
    // Source: PrintConfig.cpp (textured plate temperature definitions)
  },

  textured_plate_temp_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Textured plate initial layer temperature',
    tooltip: 'Temperature for the textured plate on initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [50],
    // Source: PrintConfig.cpp (textured plate temperature definitions)
  },

  eng_plate_temp: {
    type: ConfigOptionType.coInts,
    label: 'Engineering plate temperature',
    tooltip: 'Temperature for the engineering plate when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (engineering plate temperature definitions)
  },

  eng_plate_temp_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Engineering plate initial layer temperature',
    tooltip: 'Temperature for the engineering plate on initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [60],
    // Source: PrintConfig.cpp (engineering plate temperature definitions)
  },

  supertack_plate_temp: {
    type: ConfigOptionType.coInts,
    label: 'Supertack plate temperature',
    tooltip: 'Temperature for the supertack plate when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [0],
    // Source: PrintConfig.cpp (supertack plate temperature definitions)
  },

  supertack_plate_temp_initial_layer: {
    type: ConfigOptionType.coInts,
    label: 'Supertack plate initial layer temperature',
    tooltip: 'Temperature for the supertack plate on initial layer when using this filament',
    sidetext: '°C',
    min: 0,
    max: 300,
    default_value: [0],
    // Source: PrintConfig.cpp (supertack plate temperature definitions)
  },

  // Flow and extrusion settings
  filament_flow_ratio: {
    type: ConfigOptionType.coFloats,
    label: 'Flow ratio',
    tooltip: 'The material may have volumetric change after switching between molten state and crystalline state. This setting changes all extrusion flow of this filament in gcode proportionally. Recommended value range is between 0.95 and 1.05.',
    min: 0.5,
    max: 2.0,
    default_value: [1.0],
    // Source: PrintConfig.cpp:1818-1825
  },

  filament_max_volumetric_speed: {
    type: ConfigOptionType.coFloats,
    label: 'Max volumetric speed',
    tooltip: 'This setting stands for how much volume of filament can be melted and extruded per second. Printing speed is limited by max volumetric speed, in case of too high and unreasonable speed setting. Can\'t be zero',
    sidetext: 'mm³/s',
    min: 0,
    default_value: [2.0],
    // Source: PrintConfig.cpp:1979-1987
  },

  // Retraction settings
  retraction_length: {
    type: ConfigOptionType.coFloats,
    label: 'Retraction length',
    tooltip: 'Length of filament retraction',
    sidetext: 'mm',
    min: 0,
    default_value: [0.8],
    // Source: PrintConfig.cpp (retraction definitions)
  },

  retraction_speed: {
    type: ConfigOptionType.coFloats,
    label: 'Retraction speed',
    tooltip: 'Speed for retracting filament',
    sidetext: 'mm/s',
    min: 0,
    default_value: [30],
    // Source: PrintConfig.cpp (retraction definitions)
  },

  filament_retraction_length: {
    type: ConfigOptionType.coFloats,
    label: 'Filament retraction length',
    tooltip: 'Length of filament retraction specific to this filament',
    sidetext: 'mm',
    min: 0,
    default_value: [0.8],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retraction_speed: {
    type: ConfigOptionType.coFloats,
    label: 'Filament retraction speed',
    tooltip: 'Speed for retracting this specific filament',
    sidetext: 'mm/s',
    min: 0,
    default_value: [40],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_deretraction_speed: {
    type: ConfigOptionType.coFloats,
    label: 'Filament deretraction speed',
    tooltip: 'Speed for unretracting this specific filament',
    sidetext: 'mm/s',
    min: 0,
    default_value: [40],
    // Source: PrintConfig.cpp (filament deretraction definitions)
  },

  filament_retract_restart_extra: {
    type: ConfigOptionType.coFloats,
    label: 'Extra length on restart',
    tooltip: 'Extra length of filament to extrude after retraction',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retract_before_wipe: {
    type: ConfigOptionType.coPercents,
    label: 'Retract amount before wipe',
    tooltip: 'Percentage of retraction length to perform before wiping',
    sidetext: '%',
    min: 0,
    max: 100,
    default_value: [70],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retraction_minimum_travel: {
    type: ConfigOptionType.coFloats,
    label: 'Minimum travel after retraction',
    tooltip: 'Minimum travel distance to trigger retraction',
    sidetext: 'mm',
    min: 0,
    default_value: [1],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retract_when_changing_layer: {
    type: ConfigOptionType.coBools,
    label: 'Retract when changing layer',
    tooltip: 'Force retraction when changing layer for this filament',
    default_value: [false],
    // Source: PrintConfig.cpp:6659 (ConfigOptionBoolsNullable)
  },

  filament_retract_lift: {
    type: ConfigOptionType.coFloats,
    label: 'Lift Z',
    tooltip: 'Z lift distance during retraction',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retract_lift_above: {
    type: ConfigOptionType.coFloats,
    label: 'Above Z',
    tooltip: 'Only lift Z above this height',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_retract_lift_below: {
    type: ConfigOptionType.coFloats,
    label: 'Below Z',
    tooltip: 'Only lift Z below this height',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_long_retractions_when_cut: {
    type: ConfigOptionType.coFloats,
    label: 'Long retractions when cut',
    tooltip: 'Length of retraction when cutting filament',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament retraction definitions)
  },

  filament_wipe: {
    type: ConfigOptionType.coBools,
    label: 'Wipe while retracting',
    tooltip: 'Wipe the nozzle while retracting',
    default_value: [false],
    // Source: PrintConfig.cpp (filament wipe definitions)
  },

  filament_wipe_distance: {
    type: ConfigOptionType.coFloats,
    label: 'Wipe distance',
    tooltip: 'Distance to wipe the nozzle',
    sidetext: 'mm',
    min: 0,
    default_value: [0],
    // Source: PrintConfig.cpp (filament wipe definitions)
  },

  // Material properties
  filament_soluble: {
    type: ConfigOptionType.coBools,
    label: 'Soluble material',
    tooltip: 'Soluble material is commonly used to print support and support interface',
    default_value: [false],
    // Source: PrintConfig.cpp:2256-2260
  },

  filament_is_support: {
    type: ConfigOptionType.coBools,
    label: 'Support material',
    tooltip: 'Support material is commonly used to print support and support interface',
    default_value: [false],
    // Source: PrintConfig.cpp:2262-2266
  },

  // Fan and cooling settings
  fan_max_speed: {
    type: ConfigOptionType.coPercents,
    label: 'Max fan speed',
    tooltip: 'Maximum fan speed for this filament',
    sidetext: '%',
    min: 0,
    max: 100,
    default_value: [100],
    // Source: PrintConfig.cpp (fan speed definitions)
  },

  fan_min_speed: {
    type: ConfigOptionType.coPercents,
    label: 'Min fan speed',
    tooltip: 'Minimum fan speed for this filament',
    sidetext: '%',
    min: 0,
    max: 100,
    default_value: [35],
    // Source: PrintConfig.cpp (fan speed definitions)
  },

  overhang_fan_threshold: {
    type: ConfigOptionType.coPercents,
    label: 'Overhang cooling activation threshold',
    tooltip: 'When the overhang exceeds this specified threshold, force the cooling fan to run at the \'Overhang Fan Speed\' set below. This threshold is expressed as a percentage, indicating the portion of each line\'s width that is unsupported by the layer beneath it.',
    sidetext: '%',
    min: 0,
    max: 100,
    default_value: [95],
    // Source: PrintConfig.cpp:940-945
  },

  overhang_fan_speed: {
    type: ConfigOptionType.coPercents,
    label: 'Overhang fan speed',
    tooltip: 'Fan speed to use when printing overhangs',
    sidetext: '%',
    min: 0,
    max: 100,
    default_value: [100],
    // Source: PrintConfig.cpp (overhang fan speed definitions)
  },

  fan_cooling_layer_time: {
    type: ConfigOptionType.coInts,
    label: 'Fan cooling layer time',
    tooltip: 'Layer time threshold for fan cooling',
    sidetext: 's',
    min: 0,
    default_value: [60],
    // Source: PrintConfig.cpp (fan cooling definitions)
  },

  chamber_temperatures: {
    type: ConfigOptionType.coInts,
    label: 'Chamber temperature',
    tooltip: 'Chamber temperature for this filament',
    sidetext: '°C',
    min: 0,
    max: 100,
    default_value: [0],
    // Source: PrintConfig.cpp (chamber temperature definitions)
  },

  activate_air_filtration: {
    type: ConfigOptionType.coBools,
    label: 'Activate air filtration',
    tooltip: 'Activate air filtration when printing this filament',
    default_value: [false],
    // Source: PrintConfig.cpp (air filtration definitions)
  },

  // Advanced settings
  filament_shrink: {
    type: ConfigOptionType.coPercents,
    label: 'Shrinkage (XY)',
    tooltip: 'Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm). The part will be scaled in xy to compensate.',
    sidetext: '%',
    min: 10,
    default_value: [100],
    // Source: PrintConfig.cpp:2053-2064
  },

  filament_shrinkage_compensation_z: {
    type: ConfigOptionType.coPercents,
    label: 'Shrinkage (Z)',
    tooltip: 'Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm). The part will be scaled in Z to compensate.',
    sidetext: '%',
    min: 10,
    default_value: [100],
    // Source: PrintConfig.cpp:2066-2075
  },

  // Vendor and identification
  filament_vendor: {
    type: ConfigOptionType.coStrings,
    label: 'Vendor',
    tooltip: 'Vendor of filament. For show only',
    default_value: ['(Undefined)'],
    // Source: PrintConfig.cpp:2293-2298
  },

  filament_colour: {
    type: ConfigOptionType.coStrings,
    label: 'Color',
    tooltip: 'Only used as a visual help on UI',
    gui_type: 'color',
    default_value: ['#F2754E'],
    // Source: PrintConfig.cpp:1953-1958
  },

  // G-code settings
  filament_start_gcode: {
    type: ConfigOptionType.coStrings,
    label: 'Start G-code',
    tooltip: 'Start G-code when start the printing of this filament',
    multiline: true,
    default_value: [' '],
    // Source: PrintConfig.cpp:4511-4518
  },

  filament_end_gcode: {
    type: ConfigOptionType.coStrings,
    label: 'End G-code',
    tooltip: 'End G-code when finish the printing of this filament',
    multiline: true,
    default_value: [' '],
    // Source: PrintConfig.cpp:1563-1570
  },

  // Notes and documentation
  filament_notes: {
    type: ConfigOptionType.coStrings,
    label: 'Filament notes',
    tooltip: 'You can put your notes regarding the filament here.',
    multiline: true,
    default_value: [''],
    // Source: PrintConfig.cpp:1961-1968
  }
}
