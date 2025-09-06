/**
 * Filament Data Normalizer and Validator
 * 
 * This module provides dynamic normalization and validation of filament configuration data
 * based on the OrcaSlicer type definitions. It handles the complex task of converting
 * string values from JSON files into properly typed values while preserving semantic meaning.
 * 
 * Key features:
 * - Dynamic type detection and conversion
 * - Percentage vs numeric value handling
 * - Array vs single value normalization
 * - Validation against OrcaSlicer constraints
 * - Detailed error reporting with context
 */

import { ConfigOptionType, ConfigOptionDef, FILAMENT_CONFIG_DEFINITIONS } from './filament-config-types'

export interface NormalizationResult {
  success: boolean
  value?: any
  originalValue?: any
  type?: ConfigOptionType
  errors?: string[]
  warnings?: string[]
}

export interface ValidationContext {
  fieldName: string
  originalValue: any
  filePath?: string
  lineNumber?: number
}

export interface NormalizationOptions {
  preserveOriginalStructure?: boolean // Preserve original array vs single value structure
  normalizeArrayElements?: boolean // Normalize elements within arrays
}

/**
 * Normalizes a raw value from filament configuration based on its expected type
 */
export class FilamentDataNormalizer {
  
  /**
   * Normalizes a single field value based on its configuration definition
   */
  static normalizeField(
    fieldName: string,
    rawValue: any,
    context?: Partial<ValidationContext>,
    options?: NormalizationOptions
  ): NormalizationResult {
    const definition = FILAMENT_CONFIG_DEFINITIONS[fieldName]
    
    if (!definition) {
      return {
        success: false,
        originalValue: rawValue,
        errors: [`Unknown field: ${fieldName}. This field is not defined in OrcaSlicer configuration.`],
        warnings: [`Consider checking if this is a custom field or if the field name has changed in newer OrcaSlicer versions.`]
      }
    }

    const fullContext: ValidationContext = {
      fieldName,
      originalValue: rawValue,
      ...context
    }

    try {
      // Determine if we should preserve the original structure
      const preserveStructure = options?.preserveOriginalStructure ?? true
      const normalizeElements = options?.normalizeArrayElements ?? true

      let result: NormalizationResult

      if (preserveStructure && Array.isArray(rawValue) && normalizeElements) {
        // Original was array - preserve array structure but normalize elements
        result = this.normalizeArrayElements(rawValue, definition, fullContext)
      } else if (preserveStructure && !Array.isArray(rawValue)) {
        // Original was not array - normalize as single value
        result = this.normalizeSingleValue(rawValue, definition, fullContext)
      } else {
        // Use the original type-based normalization
        result = this.normalizeByType(definition.type, rawValue, definition, fullContext)
      }

      // Validate constraints if normalization succeeded
      if (result.success && result.value !== undefined) {
        const validationResult = this.validateConstraints(result.value, definition, fullContext)
        if (!validationResult.success) {
          return {
            ...result,
            success: false,
            errors: [...(result.errors || []), ...(validationResult.errors || [])],
            warnings: [...(result.warnings || []), ...(validationResult.warnings || [])]
          }
        }
      }

      return result
    } catch (error) {
      return {
        success: false,
        originalValue: rawValue,
        type: definition.type,
        errors: [`Unexpected error normalizing ${fieldName}: ${(error as Error).message}`]
      }
    }
  }

  /**
   * Normalizes array elements while preserving array structure
   */
  private static normalizeArrayElements(
    rawArray: any[],
    definition: ConfigOptionDef,
    context: ValidationContext
  ): NormalizationResult {
    const results: any[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawArray.length; i++) {
      const element = rawArray[i]
      const elementResult = this.normalizeSingleElementByType(definition.type, element, context, i)

      if (elementResult.success) {
        results.push(elementResult.value)
        if (elementResult.warnings) {
          warnings.push(...elementResult.warnings)
        }
      } else {
        if (elementResult.errors) {
          errors.push(...elementResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawArray,
        type: definition.type,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawArray,
      type: definition.type,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes a single value (not an array)
   */
  private static normalizeSingleValue(
    rawValue: any,
    definition: ConfigOptionDef,
    context: ValidationContext
  ): NormalizationResult {
    return this.normalizeSingleElementByType(definition.type, rawValue, context)
  }

  /**
   * Normalizes a single element based on the expected type
   */
  private static normalizeSingleElementByType(
    type: ConfigOptionType,
    rawValue: any,
    context: ValidationContext,
    arrayIndex?: number
  ): NormalizationResult {
    const fieldRef = arrayIndex !== undefined ? `${context.fieldName}[${arrayIndex}]` : context.fieldName
    const elementContext = { ...context, fieldName: fieldRef }

    // Map array types to their single element equivalents
    const elementType = this.getElementType(type)

    switch (elementType) {
      case ConfigOptionType.coFloat:
        return this.normalizeFloat(rawValue, elementContext)

      case ConfigOptionType.coInt:
        return this.normalizeInt(rawValue, elementContext)

      case ConfigOptionType.coString:
        return this.normalizeString(rawValue, elementContext)

      case ConfigOptionType.coBool:
        return this.normalizeBool(rawValue, elementContext)

      case ConfigOptionType.coPercent:
        return this.normalizePercent(rawValue, elementContext)

      case ConfigOptionType.coFloatOrPercent:
        return this.normalizeFloatOrPercent(rawValue, elementContext)

      default:
        return {
          success: false,
          originalValue: rawValue,
          type: elementType,
          errors: [`Unsupported element type: ${elementType} for field ${fieldRef}`]
        }
    }
  }

  /**
   * Maps array types to their single element types
   */
  private static getElementType(type: ConfigOptionType): ConfigOptionType {
    switch (type) {
      case ConfigOptionType.coFloats:
        return ConfigOptionType.coFloat
      case ConfigOptionType.coInts:
        return ConfigOptionType.coInt
      case ConfigOptionType.coStrings:
        return ConfigOptionType.coString
      case ConfigOptionType.coBools:
        return ConfigOptionType.coBool
      case ConfigOptionType.coPercents:
        return ConfigOptionType.coPercent
      case ConfigOptionType.coFloatsOrPercents:
        return ConfigOptionType.coFloatOrPercent
      default:
        return type // For single types, return as-is
    }
  }

  /**
   * Normalizes a value based on its ConfigOption type
   */
  private static normalizeByType(
    type: ConfigOptionType, 
    rawValue: any, 
    definition: ConfigOptionDef,
    context: ValidationContext
  ): NormalizationResult {
    
    // Handle null/undefined values
    if (rawValue === null || rawValue === undefined) {
      if (definition.nullable) {
        return { success: true, value: null, originalValue: rawValue, type }
      }
      return {
        success: true,
        value: definition.default_value,
        originalValue: rawValue,
        type,
        warnings: [`Field ${context.fieldName} is null/undefined, using default value`]
      }
    }

    switch (type) {
      case ConfigOptionType.coFloat:
        return this.normalizeFloat(rawValue, context)
      
      case ConfigOptionType.coFloats:
        return this.normalizeFloatArray(rawValue, context)
      
      case ConfigOptionType.coInt:
        return this.normalizeInt(rawValue, context)
      
      case ConfigOptionType.coInts:
        return this.normalizeIntArray(rawValue, context)
      
      case ConfigOptionType.coString:
        return this.normalizeString(rawValue, context)
      
      case ConfigOptionType.coStrings:
        return this.normalizeStringArray(rawValue, context)
      
      case ConfigOptionType.coBool:
        return this.normalizeBool(rawValue, context)
      
      case ConfigOptionType.coBools:
        return this.normalizeBoolArray(rawValue, context)
      
      case ConfigOptionType.coPercent:
        return this.normalizePercent(rawValue, context)
      
      case ConfigOptionType.coPercents:
        return this.normalizePercentArray(rawValue, context)
      
      case ConfigOptionType.coFloatOrPercent:
        return this.normalizeFloatOrPercent(rawValue, context)
      
      case ConfigOptionType.coFloatsOrPercents:
        return this.normalizeFloatOrPercentArray(rawValue, context)
      
      default:
        return {
          success: false,
          originalValue: rawValue,
          type,
          errors: [`Unsupported type: ${type} for field ${context.fieldName}`]
        }
    }
  }

  /**
   * Normalizes a float value, handling string representations
   */
  private static normalizeFloat(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'number') {
      return { success: true, value: rawValue, originalValue: rawValue, type: ConfigOptionType.coFloat }
    }
    
    if (typeof rawValue === 'string') {
      const trimmed = rawValue.trim()
      if (trimmed === '') {
        return {
          success: false,
          originalValue: rawValue,
          type: ConfigOptionType.coFloat,
          errors: [`Empty string cannot be converted to float for field ${context.fieldName}`]
        }
      }
      
      const parsed = parseFloat(trimmed)
      if (isNaN(parsed)) {
        return {
          success: false,
          originalValue: rawValue,
          type: ConfigOptionType.coFloat,
          errors: [`Cannot convert "${rawValue}" to float for field ${context.fieldName}`]
        }
      }
      
      return { success: true, value: parsed, originalValue: rawValue, type: ConfigOptionType.coFloat }
    }
    
    return {
      success: false,
      originalValue: rawValue,
      type: ConfigOptionType.coFloat,
      errors: [`Invalid type for float field ${context.fieldName}: expected number or string, got ${typeof rawValue}`]
    }
  }

  /**
   * Normalizes a float array, handling both arrays and single values
   */
  private static normalizeFloatArray(rawValue: any, context: ValidationContext): NormalizationResult {
    // Handle single values by converting to array
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizeFloat(rawValue, context)
      if (singleResult.success) {
        return {
          ...singleResult,
          value: [singleResult.value],
          type: ConfigOptionType.coFloats,
          warnings: [`Single value converted to array for field ${context.fieldName}`]
        }
      }
      return { ...singleResult, type: ConfigOptionType.coFloats }
    }

    const results: number[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizeFloat(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })
      
      if (itemResult.success) {
        results.push(itemResult.value)
        if (itemResult.warnings) {
          warnings.push(...itemResult.warnings)
        }
      } else {
        if (itemResult.errors) {
          errors.push(...itemResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coFloats,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coFloats,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes an integer value
   */
  private static normalizeInt(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'number') {
      const intValue = Math.round(rawValue)
      const warnings = intValue !== rawValue ? [`Float value ${rawValue} rounded to integer ${intValue} for field ${context.fieldName}`] : undefined
      return { success: true, value: intValue, originalValue: rawValue, type: ConfigOptionType.coInt, warnings }
    }
    
    if (typeof rawValue === 'string') {
      const trimmed = rawValue.trim()
      if (trimmed === '') {
        return {
          success: false,
          originalValue: rawValue,
          type: ConfigOptionType.coInt,
          errors: [`Empty string cannot be converted to integer for field ${context.fieldName}`]
        }
      }
      
      const parsed = parseInt(trimmed, 10)
      if (isNaN(parsed)) {
        return {
          success: false,
          originalValue: rawValue,
          type: ConfigOptionType.coInt,
          errors: [`Cannot convert "${rawValue}" to integer for field ${context.fieldName}`]
        }
      }
      
      return { success: true, value: parsed, originalValue: rawValue, type: ConfigOptionType.coInt }
    }
    
    return {
      success: false,
      originalValue: rawValue,
      type: ConfigOptionType.coInt,
      errors: [`Invalid type for integer field ${context.fieldName}: expected number or string, got ${typeof rawValue}`]
    }
  }

  /**
   * Normalizes an integer array
   */
  private static normalizeIntArray(rawValue: any, context: ValidationContext): NormalizationResult {
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizeInt(rawValue, context)
      if (singleResult.success) {
        return {
          ...singleResult,
          value: [singleResult.value],
          type: ConfigOptionType.coInts,
          warnings: [`Single value converted to array for field ${context.fieldName}`]
        }
      }
      return { ...singleResult, type: ConfigOptionType.coInts }
    }

    const results: number[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizeInt(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })
      
      if (itemResult.success) {
        results.push(itemResult.value)
        if (itemResult.warnings) {
          warnings.push(...itemResult.warnings)
        }
      } else {
        if (itemResult.errors) {
          errors.push(...itemResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coInts,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coInts,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes a string value
   */
  private static normalizeString(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'string') {
      return { success: true, value: rawValue, originalValue: rawValue, type: ConfigOptionType.coString }
    }

    if (rawValue === null || rawValue === undefined) {
      return { success: true, value: '', originalValue: rawValue, type: ConfigOptionType.coString }
    }

    // Convert other types to string
    const stringValue = String(rawValue)
    return {
      success: true,
      value: stringValue,
      originalValue: rawValue,
      type: ConfigOptionType.coString,
      warnings: [`Value converted to string for field ${context.fieldName}: ${rawValue} -> "${stringValue}"`]
    }
  }

  /**
   * Normalizes a string array
   */
  private static normalizeStringArray(rawValue: any, context: ValidationContext): NormalizationResult {
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizeString(rawValue, context)
      return {
        ...singleResult,
        value: [singleResult.value],
        type: ConfigOptionType.coStrings,
        warnings: [`Single value converted to array for field ${context.fieldName}`]
      }
    }

    const results: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizeString(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })

      results.push(itemResult.value)
      if (itemResult.warnings) {
        warnings.push(...itemResult.warnings)
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coStrings,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes a boolean value
   */
  private static normalizeBool(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'boolean') {
      return { success: true, value: rawValue, originalValue: rawValue, type: ConfigOptionType.coBool }
    }

    if (typeof rawValue === 'string') {
      const trimmed = rawValue.trim().toLowerCase()

      // Handle common boolean string representations
      if (trimmed === 'true' || trimmed === '1' || trimmed === 'yes' || trimmed === 'on' || trimmed === 'enabled') {
        return { success: true, value: true, originalValue: rawValue, type: ConfigOptionType.coBool }
      }

      if (trimmed === 'false' || trimmed === '0' || trimmed === 'no' || trimmed === 'off' || trimmed === 'disabled') {
        return { success: true, value: false, originalValue: rawValue, type: ConfigOptionType.coBool }
      }

      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coBool,
        errors: [`Cannot convert "${rawValue}" to boolean for field ${context.fieldName}. Expected: true/false, 1/0, yes/no, on/off, enabled/disabled`]
      }
    }

    if (typeof rawValue === 'number') {
      const boolValue = rawValue !== 0
      return {
        success: true,
        value: boolValue,
        originalValue: rawValue,
        type: ConfigOptionType.coBool,
        warnings: [`Number ${rawValue} converted to boolean ${boolValue} for field ${context.fieldName}`]
      }
    }

    return {
      success: false,
      originalValue: rawValue,
      type: ConfigOptionType.coBool,
      errors: [`Invalid type for boolean field ${context.fieldName}: expected boolean, string, or number, got ${typeof rawValue}`]
    }
  }

  /**
   * Normalizes a boolean array
   */
  private static normalizeBoolArray(rawValue: any, context: ValidationContext): NormalizationResult {
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizeBool(rawValue, context)
      if (singleResult.success) {
        return {
          ...singleResult,
          value: [singleResult.value],
          type: ConfigOptionType.coBools,
          warnings: [`Single value converted to array for field ${context.fieldName}`]
        }
      }
      return { ...singleResult, type: ConfigOptionType.coBools }
    }

    const results: boolean[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizeBool(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })

      if (itemResult.success) {
        results.push(itemResult.value)
        if (itemResult.warnings) {
          warnings.push(...itemResult.warnings)
        }
      } else {
        if (itemResult.errors) {
          errors.push(...itemResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coBools,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coBools,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes a percentage value (handles both "50%" and "50" formats)
   */
  private static normalizePercent(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'number') {
      return { success: true, value: rawValue, originalValue: rawValue, type: ConfigOptionType.coPercent }
    }

    if (typeof rawValue === 'string') {
      const trimmed = rawValue.trim()

      // Handle percentage format "50%"
      if (trimmed.endsWith('%')) {
        const numericPart = trimmed.slice(0, -1).trim()
        const parsed = parseFloat(numericPart)

        if (isNaN(parsed)) {
          return {
            success: false,
            originalValue: rawValue,
            type: ConfigOptionType.coPercent,
            errors: [`Cannot convert percentage "${rawValue}" to number for field ${context.fieldName}`]
          }
        }

        return {
          success: true,
          value: parsed,
          originalValue: rawValue,
          type: ConfigOptionType.coPercent,
          warnings: [`Percentage value "${rawValue}" normalized to ${parsed} for field ${context.fieldName}`]
        }
      }

      // Handle numeric string format "50"
      const parsed = parseFloat(trimmed)
      if (isNaN(parsed)) {
        return {
          success: false,
          originalValue: rawValue,
          type: ConfigOptionType.coPercent,
          errors: [`Cannot convert "${rawValue}" to percentage for field ${context.fieldName}`]
        }
      }

      return { success: true, value: parsed, originalValue: rawValue, type: ConfigOptionType.coPercent }
    }

    return {
      success: false,
      originalValue: rawValue,
      type: ConfigOptionType.coPercent,
      errors: [`Invalid type for percentage field ${context.fieldName}: expected number or string, got ${typeof rawValue}`]
    }
  }

  /**
   * Normalizes a percentage array
   */
  private static normalizePercentArray(rawValue: any, context: ValidationContext): NormalizationResult {
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizePercent(rawValue, context)
      if (singleResult.success) {
        return {
          ...singleResult,
          value: [singleResult.value],
          type: ConfigOptionType.coPercents,
          warnings: [`Single value converted to array for field ${context.fieldName}`]
        }
      }
      return { ...singleResult, type: ConfigOptionType.coPercents }
    }

    const results: number[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizePercent(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })

      if (itemResult.success) {
        results.push(itemResult.value)
        if (itemResult.warnings) {
          warnings.push(...itemResult.warnings)
        }
      } else {
        if (itemResult.errors) {
          errors.push(...itemResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coPercents,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coPercents,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes a float or percentage value (complex type that can be either)
   */
  private static normalizeFloatOrPercent(rawValue: any, context: ValidationContext): NormalizationResult {
    if (typeof rawValue === 'object' && rawValue !== null && 'value' in rawValue && 'percent' in rawValue) {
      // Already in the correct format
      return {
        success: true,
        value: rawValue,
        originalValue: rawValue,
        type: ConfigOptionType.coFloatOrPercent
      }
    }

    if (typeof rawValue === 'string' && rawValue.trim().endsWith('%')) {
      // It's a percentage
      const percentResult = this.normalizePercent(rawValue, context)
      if (percentResult.success) {
        return {
          success: true,
          value: { value: percentResult.value, percent: true },
          originalValue: rawValue,
          type: ConfigOptionType.coFloatOrPercent
        }
      }
      return { ...percentResult, type: ConfigOptionType.coFloatOrPercent }
    }

    // It's a float
    const floatResult = this.normalizeFloat(rawValue, context)
    if (floatResult.success) {
      return {
        success: true,
        value: { value: floatResult.value, percent: false },
        originalValue: rawValue,
        type: ConfigOptionType.coFloatOrPercent
      }
    }
    return { ...floatResult, type: ConfigOptionType.coFloatOrPercent }
  }

  /**
   * Normalizes a float or percentage array
   */
  private static normalizeFloatOrPercentArray(rawValue: any, context: ValidationContext): NormalizationResult {
    if (!Array.isArray(rawValue)) {
      const singleResult = this.normalizeFloatOrPercent(rawValue, context)
      if (singleResult.success) {
        return {
          ...singleResult,
          value: [singleResult.value],
          type: ConfigOptionType.coFloatsOrPercents,
          warnings: [`Single value converted to array for field ${context.fieldName}`]
        }
      }
      return { ...singleResult, type: ConfigOptionType.coFloatsOrPercents }
    }

    const results: any[] = []
    const errors: string[] = []
    const warnings: string[] = []

    for (let i = 0; i < rawValue.length; i++) {
      const itemResult = this.normalizeFloatOrPercent(rawValue[i], {
        ...context,
        fieldName: `${context.fieldName}[${i}]`
      })

      if (itemResult.success) {
        results.push(itemResult.value)
        if (itemResult.warnings) {
          warnings.push(...itemResult.warnings)
        }
      } else {
        if (itemResult.errors) {
          errors.push(...itemResult.errors)
        }
      }
    }

    if (errors.length > 0) {
      return {
        success: false,
        originalValue: rawValue,
        type: ConfigOptionType.coFloatsOrPercents,
        errors
      }
    }

    return {
      success: true,
      value: results,
      originalValue: rawValue,
      type: ConfigOptionType.coFloatsOrPercents,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Validates constraints (min, max, enum values) for a normalized value
   */
  private static validateConstraints(
    value: any,
    definition: ConfigOptionDef,
    context: ValidationContext
  ): NormalizationResult {
    const errors: string[] = []
    const warnings: string[] = []

    // Validate numeric constraints (min/max)
    if (typeof value === 'number' && (definition.min !== undefined || definition.max !== undefined)) {
      if (definition.min !== undefined && value < definition.min) {
        errors.push(`Value ${value} is below minimum ${definition.min} for field ${context.fieldName}`)
      }
      if (definition.max !== undefined && value > definition.max) {
        errors.push(`Value ${value} is above maximum ${definition.max} for field ${context.fieldName}`)
      }
    }

    // Validate array constraints
    if (Array.isArray(value)) {
      for (let i = 0; i < value.length; i++) {
        const item = value[i]
        if (typeof item === 'number' && (definition.min !== undefined || definition.max !== undefined)) {
          if (definition.min !== undefined && item < definition.min) {
            errors.push(`Array item ${i} value ${item} is below minimum ${definition.min} for field ${context.fieldName}`)
          }
          if (definition.max !== undefined && item > definition.max) {
            errors.push(`Array item ${i} value ${item} is above maximum ${definition.max} for field ${context.fieldName}`)
          }
        }
      }
    }

    // Validate enum values
    if (definition.enum_values && definition.enum_values.length > 0) {
      const validateEnumValue = (val: any, index?: number) => {
        if (typeof val === 'string' && !definition.enum_values!.includes(val)) {
          const fieldRef = index !== undefined ? `${context.fieldName}[${index}]` : context.fieldName
          errors.push(`Invalid enum value "${val}" for field ${fieldRef}. Valid values: ${definition.enum_values!.join(', ')}`)
        }
      }

      if (Array.isArray(value)) {
        value.forEach((item, index) => validateEnumValue(item, index))
      } else {
        validateEnumValue(value)
      }
    }

    return {
      success: errors.length === 0,
      value,
      originalValue: context.originalValue,
      type: definition.type,
      errors: errors.length > 0 ? errors : undefined,
      warnings: warnings.length > 0 ? warnings : undefined
    }
  }

  /**
   * Normalizes an entire filament configuration object
   */
  static normalizeFilamentConfig(
    rawConfig: Record<string, any>,
    context?: { filePath?: string },
    options?: NormalizationOptions
  ): {
    success: boolean
    normalizedConfig: Record<string, any>
    errors: Array<{ field: string; errors: string[] }>
    warnings: Array<{ field: string; warnings: string[] }>
    unknownFields: string[]
  } {
    const defaultOptions: NormalizationOptions = {
      preserveOriginalStructure: true,
      normalizeArrayElements: true,
      ...options
    }
    const normalizedConfig: Record<string, any> = {}
    const errors: Array<{ field: string; errors: string[] }> = []
    const warnings: Array<{ field: string; warnings: string[] }> = []
    const unknownFields: string[] = []

    for (const [fieldName, rawValue] of Object.entries(rawConfig)) {
      const result = this.normalizeField(fieldName, rawValue, {
        fieldName,
        originalValue: rawValue,
        filePath: context?.filePath
      }, defaultOptions)

      if (result.success) {
        normalizedConfig[fieldName] = result.value
        if (result.warnings && result.warnings.length > 0) {
          warnings.push({ field: fieldName, warnings: result.warnings })
        }
      } else {
        if (result.errors && result.errors.some(err => err.includes('Unknown field'))) {
          unknownFields.push(fieldName)
          // Still include unknown fields in the normalized config as-is
          normalizedConfig[fieldName] = rawValue
        } else {
          if (result.errors && result.errors.length > 0) {
            errors.push({ field: fieldName, errors: result.errors })
          }
        }
      }
    }

    return {
      success: errors.length === 0,
      normalizedConfig,
      errors,
      warnings,
      unknownFields
    }
  }

  /**
   * Gets the expected type for a field
   */
  static getFieldType(fieldName: string): ConfigOptionType | null {
    const definition = FILAMENT_CONFIG_DEFINITIONS[fieldName]
    return definition ? definition.type : null
  }

  /**
   * Gets the full definition for a field
   */
  static getFieldDefinition(fieldName: string): ConfigOptionDef | null {
    return FILAMENT_CONFIG_DEFINITIONS[fieldName] || null
  }

  /**
   * Lists all known field names
   */
  static getKnownFields(): string[] {
    return Object.keys(FILAMENT_CONFIG_DEFINITIONS)
  }

  /**
   * Checks if a field is known in the configuration definitions
   */
  static isKnownField(fieldName: string): boolean {
    return fieldName in FILAMENT_CONFIG_DEFINITIONS
  }

  /**
   * Checks if a field should preserve arrays for multi-extruder support
   * These fields genuinely need arrays because they can have different values per extruder
   */
  private static isMultiExtruderField(fieldName: string): boolean {
    const multiExtruderFields = [
      // Temperature fields that can vary per extruder
      'nozzle_temperature',
      'nozzle_temperature_initial_layer',
      'nozzle_temperature_range_low',
      'nozzle_temperature_range_high',

      // Filament-specific settings that can vary per extruder
      'filament_diameter',
      'filament_flow_ratio',
      'filament_max_volumetric_speed',

      // Retraction settings that can vary per extruder
      'filament_retraction_length',
      'filament_retraction_speed',
      'filament_deretraction_speed',

      // Material properties that can vary per extruder
      'filament_type',
      'filament_vendor',
      'filament_colour',
      'filament_cost',
      'filament_density',

      // Advanced per-extruder settings
      'filament_soluble',
      'filament_is_support'
    ]

    return multiExtruderFields.includes(fieldName)
  }
}
