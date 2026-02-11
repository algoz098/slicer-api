const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const REPO_ROOT = path.resolve(__dirname, '..');
const RESOURCES_DIR = path.join(REPO_ROOT, 'OrcaSlicer/resources');
const PROFILES_DIR = path.join(RESOURCES_DIR, 'profiles/BBL');

// Adjust addon path if needed
const ADDON_PATH = path.join(REPO_ROOT, 'OrcaSlicerAddon/bindings/node');
const orca = require(ADDON_PATH);

function loadJson(filePath) {
    if (!fs.existsSync(filePath)) {
        console.error(`File not found: ${filePath}`);
        return {};
    }
    const content = fs.readFileSync(filePath, 'utf8');
    try {
        return JSON.parse(content);
    } catch (e) {
        console.error(`Failed to parse JSON: ${filePath}`, e);
        return {};
    }
}

function resolveProfile(startFile, typeDir) {
    let currentFile = startFile;
    let mergedConfig = {};
    const inheritanceChain = [];

    // First pass: collect inheritance chain
    while (currentFile) {
        let fullPath = currentFile;
        if (!path.isAbsolute(currentFile)) {
            // Try to find in type directory
            fullPath = path.join(typeDir, currentFile);
            if (!fullPath.endsWith('.json')) fullPath += '.json';

            if (!fs.existsSync(fullPath)) {
                // Try finding recursively in typeDir if not direct match
                try {
                    const findCmd = `find "${typeDir}" -name "${currentFile}*" | head -n 1`;
                    const found = execSync(findCmd).toString().trim();
                    if (found) fullPath = found;
                } catch (e) {}
            }
        }

        if (!fs.existsSync(fullPath)) {
            console.warn(`Could not resolve profile: ${currentFile} (looked in ${typeDir})`);
            break;
        }

        const config = loadJson(fullPath);
        inheritanceChain.push(config);

        if (config.inherits) {
            currentFile = config.inherits;
        } else {
            currentFile = null;
        }
    }

    // Merge from base (last in chain) to specific (first in chain)
    // Reverse chain to apply base first
    for (let i = inheritanceChain.length - 1; i >= 0; i--) {
        const config = inheritanceChain[i];
        // Merge keys, ignoring metadata keys that shouldn't propagate if not needed
        for (const key in config) {
            if (key === 'inherits' || key === 'type' || key === 'name' || key === 'from' || key === 'instantiation' || key === 'setting_id') {
                continue;
            }
            mergedConfig[key] = config[key];
        }
    }

    return mergedConfig;
}

async function run() {
    console.log('--- Reproduction Script ---');

    // Load Machine Profile
    const machineDir = path.join(PROFILES_DIR, 'machine');
    const machineConfig = resolveProfile('Bambu Lab A1 0.4 nozzle', machineDir);
    console.log('Loaded Machine Config keys:', Object.keys(machineConfig).length);

    // Load Filament Profile
    const filamentDir = path.join(PROFILES_DIR, 'filament');
    const filamentConfig = resolveProfile('Bambu PLA Basic @BBL A1', filamentDir);
    console.log('Loaded Filament Config keys:', Object.keys(filamentConfig).length);

    // Load Process Profile
    const processDir = path.join(PROFILES_DIR, 'process');
    // Note: The file name is "0.20mm Standard @BBL A1.json" but we search for it
    const processConfig = resolveProfile('0.20mm Standard @BBL A1', processDir);
    console.log('Loaded Process Config keys:', Object.keys(processConfig).length);

    // Merge all into one options object
    // Priority: Process > Filament > Machine (usual precedence for slicer settings)
    // Actually, OrcaSlicer precedence is complicated, but usually specific overrides base.
    // Here we treat them as distinct sets of keys mostly.
    const fullOptions = {
        ...machineConfig,
        ...filamentConfig,
        ...processConfig
    };

    console.log('Total merged options:', Object.keys(fullOptions).length);

    // Initialize Addon
    console.log('Initializing Addon...');
    orca.initialize({
        resourcesPath: RESOURCES_DIR,
        verbose: true
    });

    // Prepare Slice Params
    const inputStl = path.resolve(REPO_ROOT, 'example_files/3DBenchy.stl');
    const outputGcode = path.resolve(REPO_ROOT, 'output_repro.gcode');

    const sliceParams = {
        input: inputStl,
        output: outputGcode,
        // We pass the full merged config as 'options' (custom_settings)
        options: fullOptions,
        // Disable profile loading from disk (pure JSON mode)
        printerProfileName: '',
        filamentProfileName: '',
        processProfileName: '',
        // Enable verbose logging
        verbose: true
    };

    console.log('Slicing...');
    try {
        const result = await orca.slice(sliceParams);
        console.log('Slice Result:', result);

        if (result.output) {
             const stats = fs.statSync(result.output);
             console.log(`Output G-code size: ${stats.size} bytes`);
        }
    } catch (e) {
        console.error('Slicing failed:', e);
    }
}

run().catch(e => console.error(e));
