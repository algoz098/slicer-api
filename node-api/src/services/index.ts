import { filesGcode } from './files/gcode/gcode'
import { filesGcodeDebugPreamble } from './files/gcode/debug-preamble'
import { filamentsProfile } from './filaments-profile/filaments-profile'
import { filesInfo } from './files/info/info'
import { filesUpload } from './files/upload/upload'
import { validation } from './validation/validation'
import { platesCount } from './plates/count/count'
import { printerProfiles } from './printer-profiles/printer-profiles'
import { healthcheck } from './healthcheck/healthcheck'
// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html#configure-functions
import type { Application } from '../declarations'

export const services = (app: Application) => {
  app.configure(filesGcode)
  app.configure(filesGcodeDebugPreamble)
  app.configure(filamentsProfile)
  // Register core services first (dependencies for other services)
  app.configure(validation)
  app.configure(filesUpload)

  // Register application services that depend on core services
  app.configure(filesInfo)
  app.configure(platesCount)
  app.configure(printerProfiles)
  app.configure(healthcheck)
  // All services will be registered here
}
