// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Params, ServiceInterface } from '@feathersjs/feathers'
import { BadRequest } from '@feathersjs/errors'
import * as fs from 'fs'
import * as path from 'path'
import * as os from 'os'
import * as yauzl from 'yauzl'

import type { Application } from '../../../declarations'
import type { PlatesCount, PlatesCountData, PlatesCountPatch, PlatesCountQuery } from './count.schema'

export type { PlatesCount, PlatesCountData, PlatesCountPatch, PlatesCountQuery }

export interface PlatesCountServiceOptions {
  app: Application
}

export interface PlatesCountParams extends Params<PlatesCountQuery> {}

export class PlatesCountService<ServiceParams extends PlatesCountParams = PlatesCountParams>
  implements ServiceInterface<PlatesCount, PlatesCountData, ServiceParams, PlatesCountPatch>
{
  constructor(public options: PlatesCountServiceOptions) {}

  async create(data: PlatesCountData, params?: ServiceParams): Promise<PlatesCount>
  async create(data: PlatesCountData[], params?: ServiceParams): Promise<PlatesCount[]>
  async create(
    data: PlatesCountData | PlatesCountData[],
    params?: ServiceParams
  ): Promise<PlatesCount | PlatesCount[]> {
    if (Array.isArray(data)) {
      return Promise.all(data.map(current => this.create(current, params)))
    }

    return this.processFile(data, params)
  }

  private async processFile(data: PlatesCountData, params?: ServiceParams): Promise<PlatesCount> {
    // Temporary solution: test with a hardcoded file path for development
    if ((data as any)?.testFilePath) {
      console.log('Debug - using test file path:', (data as any).testFilePath)
      return this.processFileFromPath((data as any).testFilePath)
    }

    // Check if file was extracted by the hook
    if ((data as any)?.uploadedFile) {
      return this.processUploadedFile((data as any).uploadedFile)
    }

    // Fallback: try to get file from Koa context directly
    const ctx = (params as any)?.request?.ctx
    let uploadedFile = null

    // Check various possible locations for the file
    if (ctx?.request?.files?.file) {
      uploadedFile = ctx.request.files.file
    } else if (ctx?.feathersFiles?.file) {
      uploadedFile = ctx.feathersFiles.file
    } else if ((params as any)?.files?.file) {
      uploadedFile = (params as any).files.file
    } else if ((data as any)?.file) {
      uploadedFile = (data as any).file
    }

    if (uploadedFile) {
      return this.processUploadedFile(uploadedFile)
    }

    throw new BadRequest('No file provided. Please upload a 3MF, STL, or OBJ file.')
  }

  private async processFileFromPath(filePath: string): Promise<PlatesCount> {
    const fileName = path.basename(filePath)
    const fileExtension = path.extname(fileName).toLowerCase()

    let plateCount: number

    if (fileExtension === '.3mf') {
      plateCount = await this.countPlatesIn3MF(filePath)
    } else if (fileExtension === '.stl' || fileExtension === '.obj') {
      plateCount = 1 // STL and OBJ files typically contain single objects
    } else {
      throw new BadRequest(`Unsupported file format: ${fileExtension}. Supported formats: .3mf, .stl, .obj`)
    }

    return {
      count: plateCount,
      fileName: fileName
    }
  }

  private async processUploadedFile(uploadedFile: any): Promise<PlatesCount> {
    const fileName = uploadedFile.originalFilename || uploadedFile.name || 'uploaded_file'
    const fileExtension = path.extname(fileName).toLowerCase()

    // Create a temporary file
    const tempDir = os.tmpdir()
    const tempFilePath = path.join(tempDir, `temp_${Date.now()}_${fileName}`)

    try {
      // Write the uploaded file to temporary location
      await fs.promises.writeFile(tempFilePath, uploadedFile.buffer || uploadedFile.path)

      let plateCount: number

      if (fileExtension === '.3mf') {
        plateCount = await this.countPlatesIn3MF(tempFilePath)
      } else if (fileExtension === '.stl' || fileExtension === '.obj') {
        plateCount = 1 // STL and OBJ files typically contain single objects
      } else {
        throw new BadRequest(`Unsupported file format: ${fileExtension}. Supported formats: .3mf, .stl, .obj`)
      }

      return {
        count: plateCount,
        fileName: fileName
      }
    } finally {
      // Clean up temporary file
      try {
        await fs.promises.unlink(tempFilePath)
      } catch (error) {
        // Ignore cleanup errors
      }
    }
  }

  private async countPlatesIn3MF(filePath: string): Promise<number> {
    return new Promise((resolve, reject) => {
      yauzl.open(filePath, { lazyEntries: true }, (err, zipfile) => {
        if (err) {
          reject(new Error(`Failed to open 3MF file: ${err.message}`))
          return
        }

        let plateFiles: string[] = []
        let modelEntry: yauzl.Entry | null = null

        // First pass: identify plate files and model file
        zipfile.readEntry()
        zipfile.on('entry', (entry: yauzl.Entry) => {
          // Identify BambuStudio/OrcaSlicer plate files
          // Look for plate files with various extensions (.xml, .png, etc.)
          if (entry.fileName.startsWith('Metadata/plate_') &&
              (entry.fileName.endsWith('.xml') ||
               entry.fileName.endsWith('.png') ||
               entry.fileName.endsWith('.jpg'))) {
            // Extract plate number to avoid counting duplicates (plate_1.png, plate_1_small.png, etc.)
            const plateMatch = entry.fileName.match(/Metadata\/plate_(\d+)\./)
            if (plateMatch) {
              const plateNumber = plateMatch[1]
              if (!plateFiles.includes(plateNumber)) {
                plateFiles.push(plateNumber)
              }
            }
          }
          // Identify the main 3D model file
          else if (entry.fileName === '3D/3dmodel.model') {
            modelEntry = entry
          }

          zipfile.readEntry()
        })

        zipfile.on('end', () => {
          // If we found plate files, count them directly (OrcaSlicer/BambuStudio format)
          if (plateFiles.length > 0) {
            zipfile.close()
            resolve(plateFiles.length)
            return
          }

          // Otherwise, process the 3D model file (standard 3MF format)
          if (modelEntry) {
            // Reopen the file to read the model entry
            yauzl.open(filePath, { lazyEntries: true }, (err, newZipfile) => {
              if (err) {
                reject(new Error(`Failed to reopen 3MF file: ${err.message}`))
                return
              }
              this.processModelFile(newZipfile, modelEntry!, resolve, reject)
            })
          } else {
            zipfile.close()
            reject(new Error('3D/3dmodel.model not found in 3MF file'))
          }
        })

        zipfile.on('error', (error) => {
          reject(new Error(`Failed to read 3MF file: ${error.message}`))
        })
      })
    })
  }

  private processModelFile(zipfile: yauzl.ZipFile, modelEntry: yauzl.Entry, resolve: (value: number) => void, reject: (error: Error) => void) {
    zipfile.openReadStream(modelEntry, (err, readStream) => {
      if (err) {
        zipfile.close()
        reject(new Error(`Failed to read 3D model: ${err.message}`))
        return
      }

      let chunks: Buffer[] = []
      readStream.on('data', (chunk: Buffer) => {
        chunks.push(chunk)
      })

      readStream.on('end', () => {
        const modelContent = Buffer.concat(chunks).toString('utf8')
        zipfile.close()

        try {
          // Parse the 3D model XML to count build items
          const buildItemCount = this.countBuildItemsInModel(modelContent)
          resolve(buildItemCount)
        } catch (error) {
          reject(new Error(`Failed to parse 3MF model: ${error}`))
        }
      })

      readStream.on('error', (error) => {
        zipfile.close()
        reject(new Error(`Failed to read model stream: ${error.message}`))
      })
    })
  }

  /**
   * Counts build items in the 3D model XML content
   * This follows the 3MF specification and OrcaSlicer algorithm:
   * - Each <item> tag within <build> represents an object instance on the build plate
   * - This is the correct way to count "plates" (object instances)
   */
  private countBuildItemsInModel(modelContent: string): number {
    try {
      // Look for <build> section and count <item> tags within it
      const buildSectionMatch = modelContent.match(/<build[^>]*>(.*?)<\/build>/s)
      if (!buildSectionMatch) {
        // No build section found, fallback to counting objects
        return this.fallbackCountObjects(modelContent)
      }

      const buildSection = buildSectionMatch[1]

      // Count <item> tags within the build section
      // Each <item> represents an object instance on the build plate
      const itemMatches = buildSection.match(/<item[^>]*>/g)
      const itemCount = itemMatches ? itemMatches.length : 0

      if (itemCount > 0) {
        return itemCount
      }

      // Fallback if no items found
      return this.fallbackCountObjects(modelContent)
    } catch (error) {
      // Fallback on any parsing error
      return this.fallbackCountObjects(modelContent)
    }
  }

  /**
   * Fallback method to count objects when build items cannot be determined
   */
  private fallbackCountObjects(modelContent: string): number {
    try {
      // Count <object> tags as a fallback
      const objectMatches = modelContent.match(/<object[^>]*>/g)
      const objectCount = objectMatches ? objectMatches.length : 0

      return objectCount > 0 ? objectCount : 1
    } catch (error) {
      return 1
    }
  }
}

export const getOptions = (app: Application): PlatesCountServiceOptions => {
  return { app }
}
