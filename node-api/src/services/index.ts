import { medias } from './medias/medias'
import { profileConverter } from './profile-converter/profile-converter'
import { profiles } from './profiles/profiles'
import { slicer3Mf } from './slicer/3mf/3mf'
import { slicerStl } from './slicer/stl/stl'
import { slicerModelInfo } from './slicer/model-info/model-info'
// For more information about this file see https://dove.feathersjs.com/guides/cli/application.html#configure-functions
import type { Application } from '../declarations'

export const services = (app: Application) => {
  app.configure(medias)
  app.configure(profileConverter)
  app.configure(profiles)
  app.configure(slicer3Mf)
  app.configure(slicerStl)
  app.configure(slicerModelInfo)
  // All services will be registered here
}
