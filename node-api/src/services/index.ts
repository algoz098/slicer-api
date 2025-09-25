import { profileConverter } from './profile-converter/profile-converter'
import { slicer3Mf } from './slicer/3mf/3mf'
import { slicerStl } from './slicer/stl/stl'
// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html#configure-functions
import type { Application } from '../declarations'

export const services = (app: Application) => {
  app.configure(profileConverter)
  app.configure(slicer3Mf)
  app.configure(slicerStl)
  // All services will be registered here
}
