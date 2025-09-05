import * as yauzl from 'yauzl'
import type { PrinterProfileInfo, ThreeMFMetadata, ProcessingConfig } from '../types/common'
import { ErrorFactory } from '../errors/custom-errors'
import { logger, loggerHelpers } from '../logger'

export interface ProcessingOptions {
  candidateFiles?: string[]
  patterns?: RegExp[]
  processingTimeout?: number
}

export class ThreeMFProcessor {
  private readonly defaultCandidateFiles = [
    'Metadata/model_settings.config',
    'Metadata/print_profile.config',
    'Metadata/project_settings.config',
    'Metadata/process_settings_0.config'
  ]

  private readonly defaultPatterns = [
    /"ProfileTitle"\s*:\s*"([^"]+)"/i,
    /ProfileTitle\s*[:=]\s*['\"]?([^'\"\n\r]+)['\"]?/i,
    /<ProfileTitle>([^<]+)<\/ProfileTitle>/i,
    /"DesignProfileId"\s*:\s*"([^"]+)"/i,
    /DesignProfileId\s*[:=]\s*['\"]?([^'\"\n\r]+)['\"]?/i,
    /"print_settings_id"\s*:\s*"([^"]+)"/i,
    /print_settings_id\s*[:=]\s*['\"]?([^'\"\n\r]+)['\"]?/i,
    /"printer_settings_id"\s*:\s*"([^"]+)"/i,
    /printer_settings_id\s*[:=]\s*['\"]?([^'\"\n\r]+)['\"]?/i,
    /"printer_model"\s*:\s*"([^"]+)"/i,
    /printer_model\s*[:=]\s*['\"]?([^'\"\n\r]+)['\"]?/i,
    /"nozzle_diameter"\s*:\s*\[([^\]]+)\]/i,
    /nozzle_diameter\s*[:=]\s*\[([^\]]+)\]/i,
    /"default_filament_profile"\s*:\s*\[\s*"([^"]+)"/i,
    /"default_print_profile"\s*:\s*"([^"]+)"/i,
    /"print_compatible_printers"\s*:\s*\[\s*"([^"]+)"/i
  ]

  constructor(private readonly options: ProcessingOptions = {}) {}

  /**
   * Extracts profile information from a 3MF file
   */
  async extractProfileInfo(filePath: string): Promise<PrinterProfileInfo | null> {
    const logContext = {
      service: 'ThreeMFProcessor',
      metadata: { filePath }
    }

    loggerHelpers.logFileProcessing(filePath, '3MF profile extraction', logContext)

    return new Promise((resolve, reject) => {
      yauzl.open(filePath, { lazyEntries: true }, (err, zipfile) => {
        if (err) {
          loggerHelpers.logError('Failed to open 3MF file', err, logContext)
          return reject(ErrorFactory.processing.threeMFCorrupted(filePath, logContext.metadata))
        }

        if (!zipfile) {
          loggerHelpers.logError('Invalid 3MF file: zipfile is null', undefined, logContext)
          return reject(ErrorFactory.processing.threeMFCorrupted(filePath, logContext.metadata))
        }

        const profileExtractor = new ProfileExtractor(
          zipfile,
          this.options.candidateFiles || this.defaultCandidateFiles,
          this.options.patterns || this.defaultPatterns
        )

        profileExtractor
          .extract()
          .then(resolve)
          .catch(reject)
      })
    })
  }
}

class ProfileExtractor {
  private found = false
  private profileName: string | null = null
  private printerProfile: string | null = null
  private nozzleDiameter: string | null = null
  private printerModel: string | null = null
  private technicalName: string | null = null
  private processedFiles = 0

  constructor(
    private readonly zipfile: yauzl.ZipFile,
    private readonly candidateFiles: string[],
    private readonly patterns: RegExp[]
  ) {}

  async extract(): Promise<PrinterProfileInfo | null> {
    return new Promise((resolve, reject) => {
      this.zipfile.readEntry()

      this.zipfile.on('entry', (entry: yauzl.Entry) => {
        if (this.candidateFiles.includes(entry.fileName)) {
          this.found = true
          this.processEntry(entry, resolve, reject)
        } else {
          this.zipfile.readEntry()
        }
      })

      this.zipfile.on('end', () => {
        this.zipfile.close()
        const result = this.buildResult()
        resolve(result)
      })

      this.zipfile.on('error', (error) => {
        this.zipfile.close()
        reject(new BadRequest(`Failed to read 3MF file: ${error.message}`))
      })
    })
  }

  private processEntry(
    entry: yauzl.Entry,
    resolve: (value: PrinterProfileInfo | null) => void,
    reject: (reason: any) => void
  ): void {
    this.zipfile.openReadStream(entry, (err, readStream) => {
      if (err) {
        this.zipfile.close()
        return reject(new BadRequest(`Failed to read metadata: ${err.message}`))
      }

      if (!readStream) {
        this.zipfile.close()
        return reject(new BadRequest('Failed to create read stream'))
      }

      const chunks: Buffer[] = []
      
      readStream.on('data', (chunk: Buffer) => chunks.push(chunk))
      
      readStream.on('end', () => {
        try {
          const content = Buffer.concat(chunks).toString('utf8')
          this.parseContent(content, entry.fileName)
          
          this.processedFiles++
          if (this.processedFiles < this.candidateFiles.length) {
            this.zipfile.readEntry()
          } else {
            this.zipfile.close()
          }
        } catch (error) {
          this.zipfile.close()
          reject(new BadRequest(`Failed to process metadata: ${error instanceof Error ? error.message : 'Unknown error'}`))
        }
      })
      
      readStream.on('error', (error) => {
        this.zipfile.close()
        reject(new BadRequest(`Failed to read metadata stream: ${error.message}`))
      })
    })
  }

  private parseContent(content: string, fileName: string): void {
    const extractedValues = this.extractValues(content)
    
    extractedValues.forEach(value => {
      this.categorizeValue(value)
    })
  }

  private extractValues(content: string): string[] {
    const results: string[] = []
    
    for (const pattern of this.patterns) {
      const match = content.match(pattern)
      if (match && match[1]) {
        const value = match[1].trim()
        if (value) {
          results.push(value)
        }
      }
    }
    
    return results
  }

  private categorizeValue(value: string): void {
    if (/^"?0\.\d+"?$/.test(value)) {
      // Pure nozzle diameter (e.g., "0.4" or 0.4)
      this.nozzleDiameter = value.replace(/"/g, '')
    } else if (value.includes('@BBL') || value.includes('@')) {
      // Technical name like "Bambu PLA Basic @BBL X1C" or "0.20mm Standard @BBL X1C"
      if (!this.technicalName) {
        this.technicalName = value
      }
    } else if (value.includes('nozzle') && !this.printerProfile) {
      // Printer profile with nozzle info
      this.printerProfile = value
    } else if (value.includes('Bambu Lab') || value.includes('X1') || value.includes('P1')) {
      // Printer model
      if (!this.printerModel) {
        this.printerModel = value
      }
    } else if ((value.includes('0.20mm') || value.includes('Standard')) && !this.profileName) {
      // Print profile
      this.profileName = value
    }
  }

  private buildResult(): PrinterProfileInfo | null {
    const result: PrinterProfileInfo = {
      printer: this.printerModel || this.printerProfile || undefined,
      nozzle: this.nozzleDiameter || undefined,
      profile: this.profileName || undefined,
      technicalName: this.technicalName || undefined
    }

    // Return null if no meaningful data was extracted
    const hasData = result.printer || result.nozzle || result.profile || result.technicalName
    return hasData ? result : null
  }
}
