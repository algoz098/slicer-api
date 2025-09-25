// For more information about this file see https://dove.feathersjs.com/guides/cli/client.html
import { feathers } from '@feathersjs/feathers'
import type { TransportConnection, Application } from '@feathersjs/feathers'
import authenticationClient from '@feathersjs/authentication-client'
import type { AuthenticationClientOptions } from '@feathersjs/authentication-client'

import { profileConverterClient } from './services/profile-converter/profile-converter.shared'
export type {
  ProfileConverter,
  ProfileConverterData,
  ProfileConverterQuery,
  ProfileConverterPatch
} from './services/profile-converter/profile-converter.shared'

import { slicer3MfClient } from './services/slicer/3mf/3mf.shared'
export type {
  Slicer3Mf,
  Slicer3MfData,
  Slicer3MfQuery,
  Slicer3MfPatch
} from './services/slicer/3mf/3mf.shared'

import { slicerStlClient } from './services/slicer/stl/stl.shared'
export type {
  SlicerStl,
  SlicerStlData,
  SlicerStlQuery,
  SlicerStlPatch
} from './services/slicer/stl/stl.shared'

export interface Configuration {
  connection: TransportConnection<ServiceTypes>
}

export interface ServiceTypes {}

export type ClientApplication = Application<ServiceTypes, Configuration>

/**
 * Returns a typed client for the node-api app.
 *
 * @param connection The REST or Socket.io Feathers client connection
 * @param authenticationOptions Additional settings for the authentication client
 * @see https://dove.feathersjs.com/api/client.html
 * @returns The Feathers client application
 */
export const createClient = <Configuration = any,>(
  connection: TransportConnection<ServiceTypes>,
  authenticationOptions: Partial<AuthenticationClientOptions> = {}
) => {
  const client: ClientApplication = feathers()

  client.configure(connection)
  client.configure(authenticationClient(authenticationOptions))
  client.set('connection', connection)

  client.configure(slicerStlClient)
  client.configure(slicer3MfClient)
  client.configure(profileConverterClient)
  return client
}
