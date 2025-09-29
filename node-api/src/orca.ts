import * as path from 'node:path'

const addonDir = process.env.ORCACLI_ADDON_DIR || path.resolve(__dirname, '../../../OrcaSlicerCli/bindings/node')

const orca = require(addonDir)
const resourcesPath = process.env.ORCACLI_RESOURCES || path.resolve(__dirname, '../../../OrcaSlicer/resources')

export default function(app: any) {
    try {
        // eslint-disable-next-line @typescript-eslint/no-var-requires
        console.log(`[Orca] Started loading. addonDir=${addonDir} resourcesPath=${resourcesPath} `)


        const prevCwd = process.cwd()
        try {
            try {
                process.chdir('/tmp')
            } catch {

            }

            orca.initialize({
                resourcesPath,
                verbose: false,
                strict: true, // enforce API-only control: no env-driven autoloads
                vendors: [],
                printerProfiles: [],
                filamentProfiles: [],
                processProfiles: []
            })
            console.log(`[Orca] Addon loaded. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

            // Do not load any generic/default presets here.
            // We keep the engine in a minimal state; specific vendors and presets
            // will be loaded on-demand by the service layer just-in-time for each request.

            app.set('orca', orca)
            app.set('orca_resourcesPath', resourcesPath)
        } finally {
            try { process.chdir(prevCwd) } catch {}
        }

    } catch (e) {
        console.error('[Orca] Fail to load:', e)
        throw e
    }
}
