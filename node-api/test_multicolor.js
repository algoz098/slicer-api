const slicer = require('./orcaslicer_node.node');
const path = require('path');

(async () => {
    const resourcesPath = path.join(__dirname, '../OrcaSlicer/resources');
    const inputFile = path.join(__dirname, '../OrcaSlicer/resources/calib/pressure_advance/auto_pa_line_single.3mf');
    const outputFile = '/tmp/test_8color_fix.gcode.3mf';

    console.log('Initializing...');
    const initResult = slicer.initialize(resourcesPath);
    console.log('Init result:', initResult);

    console.log('Slicing 8-color multi-material model...');
    try {
        const sliceResult = await slicer.slice({
            input: inputFile,
            output: outputFile,
            plate_index: 1,
            transfer_printer_customizations: true,
            transfer_filament_customizations: true,
            transfer_process_customizations: true,
            transfer_project_overrides: true
        });
        console.log('=== SLICE RESULT ===');
        console.log(JSON.stringify(sliceResult, null, 2));
    } catch (e) {
        console.log('=== SLICE ERROR ===');
        console.log(e);
    }
})();

