import { platesCount } from './plates/count/count'
import { printerProfiles } from './printer-profiles/printer-profiles'
import { healthcheck } from './healthcheck/healthcheck'
// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html#configure-functions
import type { Application } from '../declarations'

export const services = (app: Application) => {
  app.configure(platesCount)
  app.configure(printerProfiles)
  app.configure(healthcheck)
  // All services will be registered here
}
