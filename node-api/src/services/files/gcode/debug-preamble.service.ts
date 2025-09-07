import type { Params, ServiceInterface } from '@feathersjs/feathers'
import type { Application } from '../../../declarations'
import type { FilesGcode } from './gcode.schema'
import type { UploadedFile } from '../../../types/common'
import { CoreInputBuilder } from './core/core-input-builder'

export interface DebugPreambleServiceOptions { app: Application }
export interface DebugPreambleParams extends Params {}

export class DebugPreambleService<SP extends DebugPreambleParams = DebugPreambleParams>
  implements ServiceInterface<FilesGcode, any, SP, any>
{
  constructor(public options: DebugPreambleServiceOptions) {}

  async create(data: any, params?: SP): Promise<FilesGcode> {
    const koaCtx = (params as any)?.request?.ctx
    const uploaded: UploadedFile | undefined = koaCtx?.request?.files?.file || koaCtx?.feathersFiles?.file

    const coreBuilder = new CoreInputBuilder(this.options.app)
    const core = uploaded
      ? await coreBuilder.fromUploadedFile(uploaded as any, {})
      : { threeMfPath: '', profiles: { machine: {}, process: {}, filament: {} }, flavor: 'Marlin', units: 'mm' }

    const { WriterFacadeTs } = await import('./core/writer')
    const preamble = new WriterFacadeTs().generatePreamble(core as any)

    return {
      id: 0,
      file: { originalFilename: 'preamble.gcode', buffer: Buffer.from(preamble, 'utf8') }
    }
  }
}

export const getDebugOptions = (app: Application) => ({ app })

