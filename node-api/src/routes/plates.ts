import Router from '@koa/router'
import * as fs from 'fs'
import * as path from 'path'
import * as os from 'os'
import { PlatesCountService } from '../services/plates/count/count.class'
import type { Application } from '../declarations'

export function createPlatesRouter(app: Application): Router {
  const router = new Router({ prefix: '/api' })

  // POST /api/plates/count - Count plates in uploaded 3MF file
  router.post('/plates/count', async (ctx) => {
    try {
      // Check if file was uploaded
      if (!ctx.request.files?.file) {
        ctx.status = 400
        ctx.body = { error: 'No file provided. Please upload a 3MF, STL, or OBJ file.' }
        return
      }

      const file = ctx.request.files.file
      const fileName = file.originalFilename || file.name || 'uploaded_file'
      const fileExtension = path.extname(fileName).toLowerCase()

      // Create a temporary file
      const tempDir = os.tmpdir()
      const tempFilePath = path.join(tempDir, `temp_${Date.now()}_${fileName}`)

      try {
        // Write the uploaded file to temporary location
        let fileData: Buffer
        
        if (file.filepath) {
          // File is stored on disk
          fileData = await fs.promises.readFile(file.filepath)
        } else if (file.buffer) {
          // File is in memory
          fileData = file.buffer
        } else {
          throw new Error('Unable to read uploaded file')
        }

        await fs.promises.writeFile(tempFilePath, fileData)

        // Create service instance and count plates
        const service = new PlatesCountService({ app })
        let plateCount: number

        if (fileExtension === '.3mf') {
          plateCount = await service.countPlatesIn3MF(tempFilePath)
        } else if (fileExtension === '.stl' || fileExtension === '.obj') {
          plateCount = 1 // STL and OBJ files typically contain single objects
        } else {
          ctx.status = 400
          ctx.body = { 
            error: `Unsupported file format: ${fileExtension}. Supported formats: .3mf, .stl, .obj` 
          }
          return
        }

        ctx.body = {
          count: plateCount,
          fileName: fileName,
          fileSize: fileData.length,
          fileType: fileExtension
        }

      } finally {
        // Clean up temporary file
        try {
          await fs.promises.unlink(tempFilePath)
        } catch (error) {
          // Ignore cleanup errors
        }
      }

    } catch (error) {
      console.error('Error processing plate count:', error)
      ctx.status = 500
      ctx.body = { 
        error: error instanceof Error ? error.message : 'Unknown error occurred' 
      }
    }
  })

  // POST /api/files/info - Extract profile information from uploaded 3MF file
  router.post('/files/info', async (ctx) => {
    try {
      // Check if file was uploaded
      if (!ctx.request.files?.file) {
        ctx.status = 400
        ctx.body = { error: 'No file provided. Please upload a 3MF file.' }
        return
      }

      const file = ctx.request.files.file
      const fileName = file.originalFilename || file.name || 'uploaded_file'
      const fileExtension = path.extname(fileName).toLowerCase()

      // Only support 3MF files for profile extraction
      if (fileExtension !== '.3mf') {
        ctx.status = 400
        ctx.body = {
          error: `Unsupported file format: ${fileExtension}. Only .3mf files are supported for profile extraction.`
        }
        return
      }

      // Create a temporary file
      const tempDir = os.tmpdir()
      const tempFilePath = path.join(tempDir, `temp_${Date.now()}_${fileName}`)

      try {
        // Write the uploaded file to temporary location
        let fileData: Buffer

        if (file.filepath) {
          // File is stored on disk
          fileData = await fs.promises.readFile(file.filepath)
        } else if (file.buffer) {
          // File is in memory
          fileData = file.buffer
        } else {
          throw new Error('Unable to read uploaded file')
        }

        await fs.promises.writeFile(tempFilePath, fileData)

        // Process the 3MF file directly using yauzl
        const yauzl = await import('yauzl')
        const profileData = await new Promise<any>((resolve, reject) => {
          yauzl.open(tempFilePath, { lazyEntries: true }, (err, zipfile) => {
            if (err) {
              return reject(new Error(`Failed to open 3MF file: ${err.message}`))
            }

            const extractor = new (class {
              private extractedData: any = {}

              async extract(): Promise<any> {
                return new Promise((resolve, reject) => {
                  zipfile.readEntry()

                  zipfile.on('entry', (entry: any) => {
                    // Look for metadata files that contain profile information
                    if (entry.fileName.includes('slice_info.config') ||
                        entry.fileName.includes('model_settings.config') ||
                        entry.fileName.includes('Metadata/')) {

                      zipfile.openReadStream(entry, (err: any, readStream: any) => {
                        if (err) {
                          zipfile.readEntry()
                          return
                        }

                        let content = ''
                        readStream.on('data', (chunk: Buffer) => {
                          content += chunk.toString()
                        })

                        readStream.on('end', () => {
                          this.parseContent(content, entry.fileName)
                          zipfile.readEntry()
                        })
                      })
                    } else {
                      zipfile.readEntry()
                    }
                  })

                  zipfile.on('end', () => {
                    zipfile.close()
                    resolve(this.extractedData)
                  })

                  zipfile.on('error', (error: any) => {
                    zipfile.close()
                    reject(new Error(`Error reading 3MF file: ${error.message}`))
                  })
                })
              }

              private parseContent(content: string, fileName: string): void {
                // Basic profile patterns
                const basicPatterns = [
                  /"ProfileTitle"\s*:\s*"([^"]+)"/i,
                  /"printer_model"\s*:\s*"([^"]+)"/i,
                  /"nozzle_diameter"\s*:\s*\[([^\]]+)\]/i,
                  /"default_print_profile"\s*:\s*"([^"]+)"/i
                ]

                // Parse basic profile information
                for (const pattern of basicPatterns) {
                  const match = content.match(pattern)
                  if (match && match[1]) {
                    const value = match[1].trim()

                    if (pattern.source.includes('ProfileTitle') || pattern.source.includes('print_profile')) {
                      this.extractedData.profile = value
                    } else if (pattern.source.includes('printer_model')) {
                      this.extractedData.printer = value
                    } else if (pattern.source.includes('nozzle_diameter')) {
                      this.extractedData.nozzle = value.replace(/["\[\]]/g, '')
                    }
                  }
                }

                // Extract print settings
                if (!this.extractedData.printSettings) {
                  this.extractedData.printSettings = {}
                }

                const settingsPatterns = [
                  /"sparse_infill_density"\s*:\s*"([^"]+)"/i,
                  /"layer_height"\s*:\s*"([^"]+)"/i,
                  /"outer_wall_speed"\s*:\s*"([^"]+)"/i,
                  /"bed_temperature"\s*:\s*"([^"]+)"/i,
                  /"nozzle_temperature"\s*:\s*"([^"]+)"/i
                ]

                for (const pattern of settingsPatterns) {
                  const match = content.match(pattern)
                  if (match && match[1]) {
                    const value = match[1].trim()

                    if (pattern.source.includes('sparse_infill_density')) {
                      const numValue = parseFloat(value.replace('%', ''))
                      if (!isNaN(numValue)) {
                        this.extractedData.printSettings.sparseInfillPercentage = numValue
                      }
                    } else if (pattern.source.includes('layer_height')) {
                      const numValue = parseFloat(value)
                      if (!isNaN(numValue)) {
                        this.extractedData.printSettings.layerHeight = numValue
                      }
                    } else if (pattern.source.includes('speed')) {
                      const numValue = parseFloat(value)
                      if (!isNaN(numValue)) {
                        this.extractedData.printSettings.printSpeed = numValue
                      }
                    }
                  }
                }

                // Extract nozzle profiles
                this.extractNozzleProfiles()
              }

              private extractNozzleProfiles(): void {
                // Determine the selected nozzle (from file or default for printer)
                let selectedNozzle = this.extractedData.nozzle

                // If no nozzle detected, use printer default
                if (!selectedNozzle) {
                  if (this.extractedData.printer?.toLowerCase().includes('bambu')) {
                    selectedNozzle = '0.4' // Bambu default
                  } else if (this.extractedData.printer?.toLowerCase().includes('prusa')) {
                    selectedNozzle = '0.4' // Prusa default
                  } else if (this.extractedData.printer?.toLowerCase().includes('ender') ||
                             this.extractedData.printer?.toLowerCase().includes('creality')) {
                    selectedNozzle = '0.4' // Creality default
                  } else {
                    selectedNozzle = '0.4' // Universal default
                  }

                  // Update the extracted data with the default
                  this.extractedData.nozzle = selectedNozzle
                }

                if (!this.extractedData.nozzleProfiles) {
                  this.extractedData.nozzleProfiles = {
                    currentNozzle: selectedNozzle,
                    layerProfiles: {},
                    currentProfile: null
                  }
                }

                // Common profiles for different nozzle sizes
                const commonProfiles = {
                  '0.2': [
                    { name: 'Ultra Fine', layerHeight: 0.08, description: 'Highest quality, slowest print' },
                    { name: 'Fine', layerHeight: 0.12, description: 'High quality' },
                    { name: 'Standard', layerHeight: 0.16, description: 'Balanced quality and speed' }
                  ],
                  '0.4': [
                    { name: 'Fine', layerHeight: 0.16, description: 'High quality' },
                    { name: 'Standard', layerHeight: 0.2, description: 'Balanced quality and speed' },
                    { name: 'Draft', layerHeight: 0.24, description: 'Fast print, lower quality' },
                    { name: 'Fast', layerHeight: 0.28, description: 'Fastest print' }
                  ],
                  '0.6': [
                    { name: 'Standard', layerHeight: 0.2, description: 'Balanced quality and speed' },
                    { name: 'Draft', layerHeight: 0.3, description: 'Fast print' },
                    { name: 'Fast', layerHeight: 0.4, description: 'Fastest print, thick layers' }
                  ],
                  '0.8': [
                    { name: 'Standard', layerHeight: 0.3, description: 'Balanced for large nozzle' },
                    { name: 'Draft', layerHeight: 0.4, description: 'Fast print' },
                    { name: 'Fast', layerHeight: 0.6, description: 'Very fast, very thick layers' }
                  ]
                }

                // Add layer profiles only for the selected nozzle
                if (commonProfiles[selectedNozzle as keyof typeof commonProfiles]) {
                  this.extractedData.nozzleProfiles.layerProfiles[selectedNozzle] =
                    commonProfiles[selectedNozzle as keyof typeof commonProfiles]
                } else {
                  // Fallback profiles for unknown nozzle sizes
                  const nozzleSize = parseFloat(selectedNozzle)
                  this.extractedData.nozzleProfiles.layerProfiles[selectedNozzle] = [
                    { name: 'Fine', layerHeight: nozzleSize * 0.4, description: 'High quality' },
                    { name: 'Standard', layerHeight: nozzleSize * 0.5, description: 'Balanced quality and speed' },
                    { name: 'Draft', layerHeight: nozzleSize * 0.75, description: 'Fast print' }
                  ]
                }

                // Set current profile
                if (selectedNozzle && this.extractedData.printSettings?.layerHeight) {
                  this.extractedData.nozzleProfiles.currentProfile = {
                    nozzle: selectedNozzle,
                    layerHeight: this.extractedData.printSettings.layerHeight,
                    profileName: this.extractedData.profile || 'Unknown'
                  }
                }
              }
            })()

            extractor.extract().then(resolve).catch(reject)
          })
        })

        // Count plates using our existing service
        const { PlatesCountService } = await import('../services/plates/count/count.class')
        const platesService = new PlatesCountService({ app })

        // Create a mock context with the file path for testing
        const plateCountResult = await platesService.create({ testFilePath: tempFilePath } as any)
        const plateCount = plateCountResult.count

        ctx.body = {
          ...profileData,
          plateCount: plateCount,
          fileName: fileName,
          fileSize: fileData.length
        }

      } finally {
        // Clean up temporary file
        try {
          await fs.promises.unlink(tempFilePath)
        } catch (error) {
          // Ignore cleanup errors
        }
      }

    } catch (error) {
      console.error('Error processing file info:', error)
      ctx.status = 500
      ctx.body = {
        error: error instanceof Error ? error.message : 'Unknown error occurred'
      }
    }
  })

  return router
}
