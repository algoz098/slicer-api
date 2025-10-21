import * as path from 'node:path'
import * as fs from 'node:fs'
import * as os from 'node:os'


const addonDir = process.env.ORCACLI_ADDON_DIR || path.resolve(__dirname, '../../OrcaSlicerAddon/bindings/node')

const orca = require(addonDir)
const resourcesPath = process.env.ORCACLI_RESOURCES || path.resolve(__dirname, '../../OrcaSlicer/resources')

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

            (orca as any).setLoggingSilenced(true)
            try {
                orca.initialize({
                    resourcesPath,
                    verbose: false,
                    strict: true, // enforce API-only control: no env-driven autoloads
                    vendors: [], // nao varrer disco automaticamente; vamos injetar via bundle
                    printerProfiles: [],
                    filamentProfiles: [],
                    processProfiles: []
                })
            } finally {
                (orca as any).setLoggingSilenced(false)
            }
            console.log(`[Orca] Addon loaded. addonDir=${addonDir} resourcesPath=${resourcesPath}`)

            // Precarregar apenas a impressora BBL A1 0.4 via bundle em memoria (sem varrer disco no addon)
            try {
                const profilesRoot = path.join(resourcesPath, 'profiles')
                const bblJsonPath = path.join(profilesRoot, 'BBL.json')
                const machineDir = path.join(profilesRoot, 'BBL', 'machine')
                const processDir = path.join(profilesRoot, 'BBL', 'process')
                const filamentDir = path.join(profilesRoot, 'BBL', 'filament')

                const readUtf8 = (p: string) => fs.readFileSync(p, 'utf8')
                let vendorVersion: string | number = '1'
                try {
                    const vroot = JSON.parse(readUtf8(bblJsonPath))
                    if (vroot && typeof vroot === 'object') {
                        if (typeof vroot.version === 'string' || typeof vroot.version === 'number') vendorVersion = vroot.version
                    }
                } catch {}

                const modelName = 'Bambu Lab A1'
                const nozzleName = 'Bambu Lab A1 0.4 nozzle'
                const defaultProcess = '0.20mm Standard @BBL A1'
                const defaultFilament = 'Generic PLA @BBL A1'

                // A1 mini variants
                const a1mModelName = 'Bambu Lab A1 mini'
                const a1mNozzleName = 'Bambu Lab A1 mini 0.4 nozzle'
                const a1mDefaultProcess = '0.20mm Standard @BBL A1M'
                const a1mDefaultFilament = 'Generic PLA @BBL A1M'


                const vendorJsonObj = {
                    // Force canonical vendor name to match bundle key and sandbox folder
                    name: 'BBL',
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
                        // Order matters: base profiles first, then model-specific variants
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

                // Add A1 mini generic filament variants to ensure compatibility across materials
                vendorJsonObj.filament_list.push({ name: 'Generic PETG @BBL A1M', sub_path: 'filament/Generic PETG @BBL A1M.json' })
                vendorJsonObj.filament_list.push({ name: 'Generic TPU @BBL A1M', sub_path: 'filament/Generic TPU @BBL A1M.json' })



                // Diretorios Creality (para co-locar no mesmo sandbox)
                const crMachineDir = path.join(profilesRoot, 'Creality', 'machine')
                const crProcessDir = path.join(profilesRoot, 'Creality', 'process')
                const crFilamentDir = path.join(profilesRoot, 'Creality', 'filament')

                // Descobrir a versão real do vendor Creality a partir dos resources para evitar rejeição por versão inválida
                let crealityVendorVersion: string | number = '02.03.01.00'
                try {
                    const crRootPath = path.join(profilesRoot, 'Creality.json')
                    const crRoot = JSON.parse(readUtf8(crRootPath))
                    if (crRoot && (typeof crRoot.version === 'string' || typeof crRoot.version === 'number')) {
                        crealityVendorVersion = crRoot.version
                    }
                } catch {}

                // Vendor Creality minimal: somente os itens necessários para K1 Max 0.4
                const crealityVendorJsonMinimal = {
                    name: 'Creality',
                    version: crealityVendorVersion,
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

                // Helper para montar arquivos com toler
                const files: Record<string, string> = {}
                const addFile = (key: string, fullPath: string, required = true) => {
                    try {
                        files[key] = fs.readFileSync(fullPath, 'utf8')
                    } catch (e) {
                        console[required ? 'error' : 'warn'](`[Orca] ${required ? 'Missing required' : 'Missing optional'} file: ${fullPath}`)
                        if (required) throw e
                    }
                }

                // --- BBL ---
                // vendor root JSON inside bundle files (alguns codepaths esperam isso)
                files['profiles/BBL.json'] = JSON.stringify(vendorJsonObj)
                // machine (modelo e herdados)
                addFile(`BBL/machine/${modelName}.json`, path.join(machineDir, `${modelName}.json`), true)
                addFile('BBL/machine/fdm_machine_common.json', path.join(machineDir, 'fdm_machine_common.json'), true)
                addFile('BBL/machine/fdm_bbl_3dp_001_common.json', path.join(machineDir, 'fdm_bbl_3dp_001_common.json'), true)
                addFile(`BBL/machine/${nozzleName}.json`, path.join(machineDir, `${nozzleName}.json`), true)
                // A1 mini model + nozzle (criticos)
                addFile(`BBL/machine/${a1mModelName}.json`, path.join(machineDir, `${a1mModelName}.json`), true)
                addFile(`BBL/machine/${a1mNozzleName}.json`, path.join(machineDir, `${a1mNozzleName}.json`), true)
                // process (cadeia de heranca do preset padrao)
                addFile('BBL/process/fdm_process_common.json', path.join(processDir, 'fdm_process_common.json'), true)
                addFile('BBL/process/fdm_process_single_common.json', path.join(processDir, 'fdm_process_single_common.json'), true)
                addFile('BBL/process/fdm_process_single_0.20.json', path.join(processDir, 'fdm_process_single_0.20.json'), true)
                addFile(`BBL/process/${defaultProcess}.json`, path.join(processDir, `${defaultProcess}.json`), true)
                // A1 mini default process (requer heran
                addFile(`BBL/process/${a1mDefaultProcess}.json`, path.join(processDir, `${a1mDefaultProcess}.json`), true)
                addFile('BBL/process/0.20mm Standard @BBL P1P.json', path.join(processDir, '0.20mm Standard @BBL P1P.json'), true)
                // filament (bases + defaults)
                addFile('BBL/filament/fdm_filament_common.json', path.join(filamentDir, 'fdm_filament_common.json'), true)
                addFile('BBL/filament/fdm_filament_pla.json', path.join(filamentDir, 'fdm_filament_pla.json'), true)
                addFile(`BBL/filament/${defaultFilament}.json`, path.join(filamentDir, `${defaultFilament}.json`), true)
                addFile(`BBL/filament/${a1mDefaultFilament}.json`, path.join(filamentDir, `${a1mDefaultFilament}.json`), true)
                // generic filaments ABS/PLA/PETG/TPU (opcionais para compatibilidade mais ampla)
                addFile('BBL/filament/fdm_filament_pet.json', path.join(filamentDir, 'fdm_filament_pet.json'), true)
                addFile('BBL/filament/fdm_filament_abs.json', path.join(filamentDir, 'fdm_filament_abs.json'), false)
                addFile('BBL/filament/fdm_filament_tpu.json', path.join(filamentDir, 'fdm_filament_tpu.json'), false)
                addFile('BBL/filament/Generic PLA @base.json', path.join(filamentDir, 'Generic PLA @base.json'), false)
                addFile('BBL/filament/Generic PETG @base.json', path.join(filamentDir, 'Generic PETG @base.json'), true)
                addFile('BBL/filament/Generic PETG @BBL A1.json', path.join(filamentDir, 'Generic PETG @BBL A1.json'), false)
                addFile('BBL/filament/Generic ABS @base.json', path.join(filamentDir, 'Generic ABS @base.json'), false)
                addFile('BBL/filament/Generic ABS @BBL A1.json', path.join(filamentDir, 'Generic ABS @BBL A1.json'), false)
                addFile('BBL/filament/Generic TPU.json', path.join(filamentDir, 'Generic TPU.json'), false)
                addFile('BBL/filament/Generic TPU @BBL A1.json', path.join(filamentDir, 'Generic TPU @BBL A1.json'), false)
                // A1 mini extras (alguns requeridos para PETG herdado)
                addFile('BBL/filament/Generic PETG @BBL A1M.json', path.join(filamentDir, 'Generic PETG @BBL A1M.json'), true)
                addFile('BBL/filament/Generic TPU @BBL A1M.json', path.join(filamentDir, 'Generic TPU @BBL A1M.json'), false)

                // --- Creality (co-localizado no mesmo sandbox) ---
                files['profiles/Creality.json'] = JSON.stringify(crealityVendorJsonMinimal)
                addFile('Creality/machine/fdm_machine_common.json', path.join(crMachineDir, 'fdm_machine_common.json'), false)
                addFile('Creality/machine/fdm_creality_common.json', path.join(crMachineDir, 'fdm_creality_common.json'), false)
                addFile('Creality/machine/Creality K1 Max.json', path.join(crMachineDir, 'Creality K1 Max.json'), false)
                addFile('Creality/machine/Creality K1 Max (0.4 nozzle).json', path.join(crMachineDir, 'Creality K1 Max (0.4 nozzle).json'), false)
                addFile('Creality/process/fdm_process_common.json', path.join(crProcessDir, 'fdm_process_common.json'), false)
                addFile('Creality/process/fdm_process_creality_common.json', path.join(crProcessDir, 'fdm_process_creality_common.json'), false)
                addFile('Creality/process/fdm_process_common_klipper.json', path.join(crProcessDir, 'fdm_process_common_klipper.json'), false)
                addFile('Creality/process/0.20mm Standard @Creality K1Max (0.4 nozzle).json', path.join(crProcessDir, '0.20mm Standard @Creality K1Max (0.4 nozzle).json'), false)
                addFile('Creality/filament/fdm_filament_common.json', path.join(crFilamentDir, 'fdm_filament_common.json'), false)
                addFile('Creality/filament/fdm_filament_pla.json', path.join(crFilamentDir, 'fdm_filament_pla.json'), false)
                addFile('Creality/filament/fdm_filament_pet.json', path.join(crFilamentDir, 'fdm_filament_pet.json'), false)
                addFile('Creality/filament/fdm_filament_abs.json', path.join(crFilamentDir, 'fdm_filament_abs.json'), false)
                addFile('Creality/filament/fdm_filament_tpu.json', path.join(crFilamentDir, 'fdm_filament_tpu.json'), false)
                addFile('Creality/filament/Creality Generic PLA.json', path.join(crFilamentDir, 'Creality Generic PLA.json'), false)
                addFile('Creality/filament/Creality Generic PETG.json', path.join(crFilamentDir, 'Creality Generic PETG.json'), false)
                addFile('Creality/filament/Creality Generic ABS.json', path.join(crFilamentDir, 'Creality Generic ABS.json'), false)
                addFile('Creality/filament/Creality Generic TPU.json', path.join(crFilamentDir, 'Creality Generic TPU.json'), false)
                addFile('Creality/filament/Creality HF Generic PLA.json', path.join(crFilamentDir, 'Creality HF Generic PLA.json'), false)

                const bundle = {
                    vendor: 'BBL',
                    vendorJson: JSON.stringify(vendorJsonObj),
                    files,
                }

                // Carrega sandbox interno do addon com apenas esses arquivos
                if (typeof (orca as any).loadVendorBundle === 'function') {
                    (orca as any).setLoggingSilenced(true)
                    try { (orca as any).loadVendorBundle(bundle) } finally { (orca as any).setLoggingSilenced(false) }
                    // DEBUG: verificar materializacao dos arquivos criticos no sandbox do addon
                    try {
                        const sandbox = path.join(os.tmpdir(), '.orcaslicercli', `bundle-${process.pid}`)
                        const critical = [
                            path.join(sandbox, 'profiles', 'BBL', 'machine', `${a1mModelName}.json`),
                            path.join(sandbox, 'profiles', 'BBL', 'machine', `${a1mNozzleName}.json`),
                            path.join(sandbox, 'profiles', 'BBL', 'filament', 'Generic PETG @base.json'),
                            path.join(sandbox, 'profiles', 'BBL', 'filament', 'fdm_filament_pet.json'),
                            path.join(sandbox, 'profiles', 'BBL', 'filament', 'Generic PETG @BBL A1M.json')
                        ]
                        for (const p of critical) {
                            const ok = fs.existsSync(p) && fs.statSync(p).size > 0
                            console.log(`[Orca][debug] sandbox check: ${ok ? 'OK' : 'MISSING/EMPTY'} -> ${p}`)
                        }
                    } catch (e) {
                        console.warn('[Orca][debug] sandbox check failed:', e)
                    }
                    // Primeiro registre todos os vendors do sandbox atual
                    if (typeof (orca as any).loadVendor === 'function') {
                        try { (orca as any).setLoggingSilenced(true); try { (orca as any).loadVendor('BBL') } finally { (orca as any).setLoggingSilenced(false) } } catch {}
                        try { (orca as any).setLoggingSilenced(true); try { (orca as any).loadVendor('Creality') } finally { (orca as any).setLoggingSilenced(false) } } catch {}
                    }
                    // Pré-carregar filamentos A1M comuns para resolver substituições de projeto (ex.: PETG)
                    try {
                        const orcaAny = (orca as any)
                        if (typeof orcaAny.loadFilamentProfile === 'function') {
                            try { orcaAny.setLoggingSilenced(true); try { orcaAny.loadFilamentProfile('Generic PLA @BBL A1M') } finally { orcaAny.setLoggingSilenced(false) } } catch {}
                            try { orcaAny.setLoggingSilenced(true); try { orcaAny.loadFilamentProfile('Generic PETG @BBL A1M') } finally { orcaAny.setLoggingSilenced(false) } } catch {}
                        }
                    } catch {}
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
                            orcaAny.setLoggingSilenced(true);
                            try { orcaAny.loadVendorBundle({ vendor, vendorJson, files }) } finally { orcaAny.setLoggingSilenced(false) }
                            try { if (typeof orcaAny.loadVendor === 'function') { orcaAny.setLoggingSilenced(true); try { orcaAny.loadVendor(vendor) } finally { orcaAny.setLoggingSilenced(false) } } } catch {}
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
            // Nenhum preset materializado aqui por padrão. Os perfis serão carregados sob demanda
            // pelo serviço (3MF/STL) conforme o request ou conteúdo do projeto.


        } finally {
            try { process.chdir(prevCwd) } catch {}
        }

    } catch (e) {
        console.error('[Orca] Fail to load:', e)
        throw e
    }


}
