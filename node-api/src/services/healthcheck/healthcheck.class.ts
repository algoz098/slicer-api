// For more information about this file see https://dove.feathersjs.com/guides/cli/service.class.html#custom-services
import type { Params, ServiceInterface } from '@feathersjs/feathers'

import type { Application } from '../../declarations'
import type { Healthcheck, HealthcheckData, HealthcheckPatch, HealthcheckQuery } from './healthcheck.schema'

export type { Healthcheck, HealthcheckData, HealthcheckPatch, HealthcheckQuery }

export interface HealthcheckServiceOptions {
  app: Application
}

export interface HealthcheckParams extends Params<HealthcheckQuery> {}

// This is a skeleton for a custom service class. Remove or add the methods you need here
export class HealthcheckService<ServiceParams extends HealthcheckParams = HealthcheckParams>
  implements ServiceInterface<Healthcheck, HealthcheckData, ServiceParams, HealthcheckPatch>
{
  constructor(public options: HealthcheckServiceOptions) {}
  // Only implement lightweight read-only checks for safety and minimal surface area
  async find(_params?: ServiceParams): Promise<Healthcheck> {
    const uptime = Math.floor(process.uptime())
    const timestamp = new Date().toISOString()
    const pid = process.pid
    const version = process.env.npm_package_version ? String(process.env.npm_package_version) : null

    return {
      status: 'ok',
      uptime,
      timestamp,
      pid,
      version
    }
  }
}

export const getOptions = (app: Application) => {
  return { app }
}
