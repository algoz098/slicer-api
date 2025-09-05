// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Id, NullableId, Params, ServiceInterface } from '@feathersjs/feathers'
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
    // With koa-body, files are available in the Koa context
    const ctx = (params as any)?.request?.ctx
    if (!ctx || !ctx.request || !ctx.request.files || !ctx.request.files.file) {
      throw new BadRequest('No file provided')
    }

    const file = ctx.request.files.file
    const fileName = file.originalFilename || file.name || 'uploaded_file'
    const fileExtension = path.extname(fileName).toLowerCase()

    // Create a temporary file
    const tempDir = os.tmpdir()
    const tempFilePath = path.join(tempDir, `temp_${Date.now()}_${fileName}`)

    try {
      // Write the uploaded file to temporary location
      await fs.promises.writeFile(tempFilePath, file.buffer || file.path)

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

        let foundMetadata = false
        let metadataContent = ''

        zipfile.readEntry()
        zipfile.on('entry', (entry: yauzl.Entry) => {
          if (entry.fileName === 'Metadata/model_settings.config') {
            foundMetadata = true
            zipfile.openReadStream(entry, (err, readStream) => {
              if (err) {
                reject(new Error(`Failed to read metadata: ${err.message}`))
                return
              }

              let chunks: Buffer[] = []
              readStream.on('data', (chunk: Buffer) => {
                chunks.push(chunk)
              })

              readStream.on('end', () => {
                metadataContent = Buffer.concat(chunks).toString('utf8')
                zipfile.close()

                try {
                  // Parse the model_settings.config to find plate count
                  const lines = metadataContent.split('\n')
                  for (const line of lines) {
                    if (line.includes('plate_count') || line.includes('plate count')) {
                      const match = line.match(/(\d+)/)
                      if (match) {
                        resolve(parseInt(match[1], 10))
                        return
                      }
                    }
                  }

                  // If no explicit plate count found, try to count objects
                  let objectCount = 0
                  for (const line of lines) {
                    if (line.includes('<object') || line.includes('object id')) {
                      objectCount++
                    }
                  }

                  resolve(objectCount > 0 ? objectCount : 1)
                } catch (error) {
                  reject(new Error(`Failed to parse 3MF metadata: ${error}`))
                }
              })

              readStream.on('error', (error) => {
                reject(new Error(`Failed to read metadata stream: ${error.message}`))
              })
            })
          } else {
            zipfile.readEntry()
          }
        })

        zipfile.on('end', () => {
          if (!foundMetadata) {
            zipfile.close()
            reject(new Error('Metadata/model_settings.config not found in 3MF file'))
          }
        })

        zipfile.on('error', (error) => {
          reject(new Error(`Failed to read 3MF file: ${error.message}`))
        })
      })
    })
  }
}

export const getOptions = (app: Application): PlatesCountServiceOptions => {
  return { app }
}
