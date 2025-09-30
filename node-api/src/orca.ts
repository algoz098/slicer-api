import * as path from 'node:path'
import * as fs from 'node:fs'

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
                strict: false, // enforce API-only control: no env-driven autoloads
                vendors: [], // nao varrer disco automaticamente; vamos injetar via bundle
                printerProfiles: [],
                filamentProfiles: [],
                processProfiles: []
            })
            console.log(`[Orca] Addon loaded. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

            // Precarregar apenas a impressora BBL A1 0.4 via bundle em memoria (sem varrer disco no addon)
            try {
                const profilesRoot = path.join(resourcesPath, 'profiles')
                const bblJsonPath = path.join(profilesRoot, 'BBL.json')
                const machineDir = path.join(profilesRoot, 'BBL', 'machine')
                const processDir = path.join(profilesRoot, 'BBL', 'process')
                const filamentDir = path.join(profilesRoot, 'BBL', 'filament')

                const readUtf8 = (p: string) => fs.readFileSync(p, 'utf8')
                let vendorName = 'BBL'
                let vendorVersion: string | number = '1'
                try {
                    const vroot = JSON.parse(readUtf8(bblJsonPath))
                    if (vroot && typeof vroot === 'object') {
                        if (typeof vroot.name === 'string') vendorName = vroot.name
                        if (typeof vroot.version === 'string' || typeof vroot.version === 'number') vendorVersion = vroot.version
                    }
                } catch {}

                const modelName = 'Bambu Lab A1'
                const nozzleName = 'Bambu Lab A1 0.4 nozzle'
                const defaultProcess = '0.20mm Standard @BBL A1'
                const defaultFilament = 'Bambu PLA Basic @BBL A1'

                // A1 mini variants
                const a1mModelName = 'Bambu Lab A1 mini'
                const a1mNozzleName = 'Bambu Lab A1 mini 0.4 nozzle'
                const a1mDefaultProcess = '0.20mm Standard @BBL A1M'
                const a1mDefaultFilament = 'Bambu PLA Basic @BBL A1M'


                const vendorJsonObj = {
                    name: vendorName,
                    version: vendorVersion,
                    machine_model_list: [ { name: modelName, sub_path: `machine/${modelName}.json` } ],
                    process_list: [
                        { name: 'fdm_process_common', sub_path: 'process/fdm_process_common.json' },
                        { name: 'fdm_process_single_common', sub_path: 'process/fdm_process_single_common.json' },
                        { name: 'fdm_process_single_0.20', sub_path: 'process/fdm_process_single_0.20.json' },
                        { name: defaultProcess, sub_path: `process/${defaultProcess}.json` }
                    ],
                    filament_list: [
                        { name: 'fdm_filament_common', sub_path: 'filament/fdm_filament_common.json' },
                        { name: 'fdm_filament_pla', sub_path: 'filament/fdm_filament_pla.json' },
                        { name: 'fdm_filament_pet', sub_path: 'filament/fdm_filament_pet.json' },
                        { name: 'fdm_filament_abs', sub_path: 'filament/fdm_filament_abs.json' },
                        { name: 'fdm_filament_tpu', sub_path: 'filament/fdm_filament_tpu.json' },
                        { name: 'Bambu PLA Basic @base', sub_path: 'filament/Bambu PLA Basic @base.json' },
                        { name: defaultFilament, sub_path: `filament/${defaultFilament}.json` },
                        // Generic materials for BBL A1
                        { name: 'Generic PLA @base', sub_path: 'filament/Generic PLA @base.json' },
                        { name: 'Generic PLA @BBL A1', sub_path: 'filament/Generic PLA @BBL A1.json' },
                        { name: 'Generic PETG @base', sub_path: 'filament/Generic PETG @base.json' },
                        { name: 'Generic PETG @BBL A1', sub_path: 'filament/Generic PETG @BBL A1.json' },
                        { name: 'Generic ABS @base', sub_path: 'filament/Generic ABS @base.json' },
                        { name: 'Generic ABS @BBL A1', sub_path: 'filament/Generic ABS @BBL A1.json' },
                        { name: 'Generic TPU', sub_path: 'filament/Generic TPU.json' },
                        { name: 'Generic TPU @BBL A1', sub_path: 'filament/Generic TPU @BBL A1.json' }
                    ],
                    machine_list: [
                        { name: 'fdm_machine_common', sub_path: 'machine/fdm_machine_common.json' },
                        { name: 'fdm_bbl_3dp_001_common', sub_path: 'machine/fdm_bbl_3dp_001_common.json' },
                        { name: nozzleName, sub_path: `machine/${nozzleName}.json` }
                    ]
                }

                // Estender vendorJsonObj para incluir A1 mini
                vendorJsonObj.machine_model_list.push({ name: a1mModelName, sub_path: `machine/${a1mModelName}.json` })
                vendorJsonObj.machine_list.push({ name: a1mNozzleName, sub_path: `machine/${a1mNozzleName}.json` })
                // Primeiro adicione a herança (P1P), depois o preset do A1M para garantir ordem de carga
                vendorJsonObj.process_list.push({ name: '0.20mm Standard @BBL P1P', sub_path: 'process/0.20mm Standard @BBL P1P.json' })
                vendorJsonObj.process_list.push({ name: a1mDefaultProcess, sub_path: `process/${a1mDefaultProcess}.json` })
                vendorJsonObj.filament_list.push({ name: a1mDefaultFilament, sub_path: `filament/${a1mDefaultFilament}.json` })



                // Diretorios Creality (para co-locar no mesmo sandbox)
                const crMachineDir = path.join(profilesRoot, 'Creality', 'machine')
                const crProcessDir = path.join(profilesRoot, 'Creality', 'process')
                const crFilamentDir = path.join(profilesRoot, 'Creality', 'filament')

                // Vendor Creality minimal: somente os itens necessários para K1 Max 0.4
                const crealityVendorJsonMinimal = {
                    name: 'Creality',
                    version: '0',
                    force_update: '0',
                    description: 'Creality minimal bundle (K1 Max 0.4)',
                    machine_model_list: [
                        { name: 'Creality K1 Max', sub_path: 'machine/Creality K1 Max.json' }
                    ],
                    machine_list: [
                        { name: 'fdm_machine_common', sub_path: 'machine/fdm_machine_common.json' },
                        { name: 'fdm_creality_common', sub_path: 'machine/fdm_creality_common.json' },
                        { name: 'Creality K1 Max (0.4 nozzle)', sub_path: 'machine/Creality K1 Max (0.4 nozzle).json' }
                    ],
                    process_list: [
                        { name: 'fdm_process_common', sub_path: 'process/fdm_process_common.json' },
                        { name: 'fdm_process_creality_common', sub_path: 'process/fdm_process_creality_common.json' },
                        { name: 'fdm_process_common_klipper', sub_path: 'process/fdm_process_common_klipper.json' },
                        { name: '0.20mm Standard @Creality K1Max (0.4 nozzle)', sub_path: 'process/0.20mm Standard @Creality K1Max (0.4 nozzle).json' }
                    ],
                    filament_list: [
                        { name: 'fdm_filament_common', sub_path: 'filament/fdm_filament_common.json' },
                        { name: 'fdm_filament_pla', sub_path: 'filament/fdm_filament_pla.json' },
                        { name: 'fdm_filament_pet', sub_path: 'filament/fdm_filament_pet.json' },
                        { name: 'fdm_filament_abs', sub_path: 'filament/fdm_filament_abs.json' },
                        { name: 'fdm_filament_tpu', sub_path: 'filament/fdm_filament_tpu.json' },
                        { name: 'Creality Generic PLA', sub_path: 'filament/Creality Generic PLA.json' },
                        { name: 'Creality Generic PETG', sub_path: 'filament/Creality Generic PETG.json' },
                        { name: 'Creality Generic ABS', sub_path: 'filament/Creality Generic ABS.json' },
                        { name: 'Creality Generic TPU', sub_path: 'filament/Creality Generic TPU.json' },
                        { name: 'Creality HF Generic PLA', sub_path: 'filament/Creality HF Generic PLA.json' }
                    ]
                }

                const bundle = {
                    vendor: 'BBL',
                    vendorJson: JSON.stringify(vendorJsonObj),
                    files: {
                        // --- BBL ---
                        // machine (modelo e herdados)
                        [`BBL/machine/${modelName}.json`]: readUtf8(path.join(machineDir, `${modelName}.json`)),
                        'BBL/machine/fdm_machine_common.json': readUtf8(path.join(machineDir, 'fdm_machine_common.json')),
                        'BBL/machine/fdm_bbl_3dp_001_common.json': readUtf8(path.join(machineDir, 'fdm_bbl_3dp_001_common.json')),
                        [`BBL/machine/${nozzleName}.json`]: readUtf8(path.join(machineDir, `${nozzleName}.json`)),
                        // A1 mini model + nozzle
                        [`BBL/machine/${a1mModelName}.json`]: readUtf8(path.join(machineDir, `${a1mModelName}.json`)),
                        [`BBL/machine/${a1mNozzleName}.json`]: readUtf8(path.join(machineDir, `${a1mNozzleName}.json`)),
                        // process (cadeia de heranca do preset padrao)
                        'BBL/process/fdm_process_common.json': readUtf8(path.join(processDir, 'fdm_process_common.json')),
                        'BBL/process/fdm_process_single_common.json': readUtf8(path.join(processDir, 'fdm_process_single_common.json')),
                        'BBL/process/fdm_process_single_0.20.json': readUtf8(path.join(processDir, 'fdm_process_single_0.20.json')),
                        [`BBL/process/${defaultProcess}.json`]: readUtf8(path.join(processDir, `${defaultProcess}.json`)),
                        // A1 mini default process
                        [`BBL/process/${a1mDefaultProcess}.json`]: readUtf8(path.join(processDir, `${a1mDefaultProcess}.json`)),
                        // Heran e7a: P1P Standard requerido pelo A1M Standard
                        'BBL/process/0.20mm Standard @BBL P1P.json': readUtf8(path.join(processDir, '0.20mm Standard @BBL P1P.json')),
                        // filament (cadeia de heranca do filamento padrao)
                        'BBL/filament/fdm_filament_common.json': readUtf8(path.join(filamentDir, 'fdm_filament_common.json')),
                        'BBL/filament/fdm_filament_pla.json': readUtf8(path.join(filamentDir, 'fdm_filament_pla.json')),
                        'BBL/filament/Bambu PLA Basic @base.json': readUtf8(path.join(filamentDir, 'Bambu PLA Basic @base.json')),
                        [`BBL/filament/${defaultFilament}.json`]: readUtf8(path.join(filamentDir, `${defaultFilament}.json`)),
                        // A1 mini default filament
                        [`BBL/filament/${a1mDefaultFilament}.json`]: readUtf8(path.join(filamentDir, `${a1mDefaultFilament}.json`)),
                        // generic filaments ABS/PLA/PETG/TPU (bases + A1 variants)
                        'BBL/filament/fdm_filament_pet.json': readUtf8(path.join(filamentDir, 'fdm_filament_pet.json')),
                        'BBL/filament/fdm_filament_abs.json': readUtf8(path.join(filamentDir, 'fdm_filament_abs.json')),
                        'BBL/filament/fdm_filament_tpu.json': readUtf8(path.join(filamentDir, 'fdm_filament_tpu.json')),
                        'BBL/filament/Generic PLA @base.json': readUtf8(path.join(filamentDir, 'Generic PLA @base.json')),
                        'BBL/filament/Generic PLA @BBL A1.json': readUtf8(path.join(filamentDir, 'Generic PLA @BBL A1.json')),
                        'BBL/filament/Generic PETG @base.json': readUtf8(path.join(filamentDir, 'Generic PETG @base.json')),
                        'BBL/filament/Generic PETG @BBL A1.json': readUtf8(path.join(filamentDir, 'Generic PETG @BBL A1.json')),
                        'BBL/filament/Generic ABS @base.json': readUtf8(path.join(filamentDir, 'Generic ABS @base.json')),
                        'BBL/filament/Generic ABS @BBL A1.json': readUtf8(path.join(filamentDir, 'Generic ABS @BBL A1.json')),
                        'BBL/filament/Generic TPU.json': readUtf8(path.join(filamentDir, 'Generic TPU.json')),
                        'BBL/filament/Generic TPU @BBL A1.json': readUtf8(path.join(filamentDir, 'Generic TPU @BBL A1.json')),

                        // --- Creality (co-localizado no mesmo sandbox) ---
                        // vendor root JSON
                        'profiles/Creality.json': JSON.stringify(crealityVendorJsonMinimal),
                        // machine
                        'Creality/machine/fdm_machine_common.json': readUtf8(path.join(crMachineDir, 'fdm_machine_common.json')),
                        'Creality/machine/fdm_creality_common.json': readUtf8(path.join(crMachineDir, 'fdm_creality_common.json')),
                        'Creality/machine/Creality K1 Max.json': readUtf8(path.join(crMachineDir, 'Creality K1 Max.json')),
                        'Creality/machine/Creality K1 Max (0.4 nozzle).json': readUtf8(path.join(crMachineDir, 'Creality K1 Max (0.4 nozzle).json')),
                        // process
                        'Creality/process/fdm_process_common.json': readUtf8(path.join(crProcessDir, 'fdm_process_common.json')),
                        'Creality/process/fdm_process_creality_common.json': readUtf8(path.join(crProcessDir, 'fdm_process_creality_common.json')),
                        'Creality/process/fdm_process_common_klipper.json': readUtf8(path.join(crProcessDir, 'fdm_process_common_klipper.json')),
                        'Creality/process/0.20mm Standard @Creality K1Max (0.4 nozzle).json': readUtf8(path.join(crProcessDir, '0.20mm Standard @Creality K1Max (0.4 nozzle).json')),
                        // filament (minimo para ficar utilizavel)
                        'Creality/filament/fdm_filament_common.json': readUtf8(path.join(crFilamentDir, 'fdm_filament_common.json')),
                        'Creality/filament/fdm_filament_pla.json': readUtf8(path.join(crFilamentDir, 'fdm_filament_pla.json')),
                        'Creality/filament/fdm_filament_pet.json': readUtf8(path.join(crFilamentDir, 'fdm_filament_pet.json')),
                        'Creality/filament/fdm_filament_abs.json': readUtf8(path.join(crFilamentDir, 'fdm_filament_abs.json')),
                        'Creality/filament/fdm_filament_tpu.json': readUtf8(path.join(crFilamentDir, 'fdm_filament_tpu.json')),
                        'Creality/filament/Creality Generic PLA.json': readUtf8(path.join(crFilamentDir, 'Creality Generic PLA.json')),
                        'Creality/filament/Creality Generic PETG.json': readUtf8(path.join(crFilamentDir, 'Creality Generic PETG.json')),
                        'Creality/filament/Creality Generic ABS.json': readUtf8(path.join(crFilamentDir, 'Creality Generic ABS.json')),
                        'Creality/filament/Creality Generic TPU.json': readUtf8(path.join(crFilamentDir, 'Creality Generic TPU.json')),
                        'Creality/filament/Creality HF Generic PLA.json': readUtf8(path.join(crFilamentDir, 'Creality HF Generic PLA.json')),
                    }
                }

                // Carrega sandbox interno do addon com apenas esses arquivos
                if (typeof (orca as any).loadVendorBundle === 'function') {
                    (orca as any).loadVendorBundle(bundle)
                    // Primeiro registre todos os vendors do sandbox atual
                    if (typeof (orca as any).loadVendor === 'function') {
                        try { (orca as any).loadVendor('BBL') } catch {}
                        try { (orca as any).loadVendor('Creality') } catch {}
                    }
                    // Só depois materialize os perfis padrão (BBL A1 0.4)
                    // Nenhum preset materializado aqui; será definido após carregar vendors externos (public/profiles)
                    console.log(`[Orca] Loaded vendors BBL + Creality (sandbox interno); nenhum preset materializado neste passo`)
                } else {
                    console.warn('[Orca] loadVendorBundle not available in addon; skipping pre-load')
                }
            } catch (err) {
                console.warn('[Orca] Failed to pre-load BBL A1 0.4 via bundle:', err)
            }


            // Load additional vendor bundles from node-api/public/profiles (if present)
            try {
                const publicProfilesDir = path.resolve(__dirname, '../public/profiles')
                const orcaAny = (orca as any)
                console.log(`[Orca] Scanning external profiles at: ${publicProfilesDir}`)
                if (fs.existsSync(publicProfilesDir) && fs.statSync(publicProfilesDir).isDirectory() && typeof orcaAny?.loadVendorBundle === 'function') {
                    const entries = fs.readdirSync(publicProfilesDir)
                    const vendorRootJsons = entries.filter(n => n.toLowerCase().endsWith('.json'))
                    console.log(`[Orca] Found vendor roots: ${vendorRootJsons.join(', ') || '(none)'}`)
                    for (const rootJsonName of vendorRootJsons) {
                        const vendor = path.basename(rootJsonName, path.extname(rootJsonName))
                        const vendorRootPath = path.join(publicProfilesDir, rootJsonName)
                        const vendorDir = path.join(publicProfilesDir, vendor)
                        // Require the vendor folder with files to avoid partial/invalid bundles
                        if (!fs.existsSync(vendorDir) || !fs.statSync(vendorDir).isDirectory()) {
                            console.warn(`[Orca] Skipping vendor "${vendor}": folder ${vendorDir} not found`)
                            continue
                        }

                        const vendorJson = fs.readFileSync(vendorRootPath, 'utf8')

                        // Recursively collect all files under <public>/profiles/<Vendor>
                        const files: Record<string, string> = {}
                        const stack: string[] = [vendorDir]
                        while (stack.length) {
                            const current = stack.pop() as string
                            const items = fs.readdirSync(current)
                            for (const name of items) {
                                const full = path.join(current, name)
                                const st = fs.statSync(full)
                                if (st.isDirectory()) {
                                    stack.push(full)
                                } else {
                                    // key should be like: <Vendor>/machine/... or <Vendor>/process/... etc
                                    const relFromVendor = full.slice(vendorDir.length + 1).split(path.sep).join('/')
                                    const key = `${vendor}/${relFromVendor}`
                                    files[key] = fs.readFileSync(full, 'utf8')
                                }
                            }
                        }

                        try {
                            console.log(`[Orca] Loading vendor bundle: ${vendor}`)
                            orcaAny.loadVendorBundle({ vendor, vendorJson, files })
                            try { if (typeof orcaAny.loadVendor === 'function') orcaAny.loadVendor(vendor) } catch {}
                            console.log(`[Orca] Loaded external vendor bundle from public/profiles: ${vendor}`)
                        } catch (err) {
                            console.error(`[Orca] Failed to load external vendor bundle ${vendor}:`, err)
                        }
                    }
                } else {
                    console.warn('[Orca] public/profiles not found or addon missing loadVendorBundle; skipping external bundles')
                }
            } catch (err) {
                console.warn('[Orca] Skipped loading public/profiles bundles:', err)
            }

            // Do not load any generic/default presets here.
            // We keep the engine in a minimal state; specific vendors and presets
            // will be loaded on-demand by the service layer just-in-time for each request.

            app.set('orca', orca)
            app.set('orca_resourcesPath', resourcesPath)
            // Materializar preset padrão: Creality K2 Plus 0.4 (vendor CrealityPrint)
            try {
                const orcaAny = (orca as any)
                const targetPrinter = 'Creality K2 Plus 0.4 nozzle'
                const targetProcess = '0.20mm Standard @Creality K2 Plus 0.4 nozzle'
                const targetFilament = 'Creality Generic PLA'
                // garantir vendor registrado
                if (typeof orcaAny.loadVendor === 'function') {
                    try { orcaAny.loadVendor('CrealityPrint') } catch {}
                }
                if (typeof orcaAny.loadPrinterProfile === 'function') {
                    orcaAny.loadPrinterProfile(targetPrinter)
                }
                if (typeof orcaAny.loadProcessProfile === 'function') {
                    orcaAny.loadProcessProfile(targetProcess)
                }
                if (typeof orcaAny.loadFilamentProfile === 'function') {
                    orcaAny.loadFilamentProfile(targetFilament)
                }
                console.log('[Orca] Default preset materialized: Creality K2 Plus 0.4 (printer/process/filament)')
            } catch (err) {
                console.warn('[Orca] Failed to materialize default preset K2 Plus 0.4:', err)
            }

        } finally {
            try { process.chdir(prevCwd) } catch {}
        }

    } catch (e) {
        console.error('[Orca] Fail to load:', e)
        throw e
    }


}
