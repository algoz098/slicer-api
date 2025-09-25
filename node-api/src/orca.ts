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

            // Load a generic/default profile set (best-effort): printer, filament, and process
            // We attempt a small set of common candidates and stop on the first success of each category.
            const printerCandidates = [
                'BBL A1 mini 0.4 nozzle'
            ]
            const filamentCandidates = [
                'Generic PLA'
            ]
            const processCandidates = [
                '0.20mm Standard'
            ]

            const tryLoad = (label: string, fn: (name: string) => void, candidates: string[]) => {
                for (const name of candidates) {
                    try {
                        fn(name)
                        console.log(`[Orca] Loaded ${label}: ${name}`)
                        return name
                    } catch (e) {
                        // try next
                    }
                }
                console.warn(`[Orca] No generic ${label} found among candidates: ${candidates.join(', ')}`)
                return null
            }

            // Vendor 'BBL' is already requested in initialize; proceed to load generic presets
            tryLoad('printer profile', orca.loadPrinterProfile, printerCandidates)
            tryLoad('filament profile', orca.loadFilamentProfile, filamentCandidates)
            tryLoad('process profile', orca.loadProcessProfile, processCandidates)

            app.set('orca', orca)
        } finally {
            try { process.chdir(prevCwd) } catch {}
        }

    } catch (e) {
        console.error('[Orca] Fail to load:', e)
        throw e
    }
}
