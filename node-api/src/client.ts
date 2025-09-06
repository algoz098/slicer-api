// For more information about this file see https://dove.feathersjs.com/guides/cli/client.html
import { feathers } from '@feathersjs/feathers'
import type { TransportConnection, Application } from '@feathersjs/feathers'
import authenticationClient from '@feathersjs/authentication-client'
import type { AuthenticationClientOptions } from '@feathersjs/authentication-client'

import { filamentsProfileClient } from './services/filaments-profile/filaments-profile.shared'
export type {
  FilamentsProfile,
  FilamentsProfileData,
  FilamentsProfileQuery,
  FilamentsProfilePatch
} from './services/filaments-profile/filaments-profile.shared'

import { filesInfoClient } from './services/files/info/info.shared'
export type {
  FilesInfo,
  FilesInfoData,
  FilesInfoQuery,
  FilesInfoPatch
} from './services/files/info/info.shared'

import { platesCountClient } from './services/plates/count/count.shared'
export type {
  PlatesCount,
  PlatesCountData,
  PlatesCountQuery,
  PlatesCountPatch
} from './services/plates/count/count.shared'

import { printerProfilesClient } from './services/printer-profiles/printer-profiles.shared'
export type {
  PrinterProfiles,
  PrinterProfilesData,
  PrinterProfilesQuery,
  PrinterProfilesPatch
} from './services/printer-profiles/printer-profiles.shared'

import { healthcheckClient } from './services/healthcheck/healthcheck.shared'
export type {
  Healthcheck,
  HealthcheckData,
  HealthcheckQuery,
  HealthcheckPatch
} from './services/healthcheck/healthcheck.shared'

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

  client.configure(healthcheckClient)
  client.configure(printerProfilesClient)
  client.configure(platesCountClient)
  client.configure(filesInfoClient)
  client.configure(filamentsProfileClient)
  return client
}
