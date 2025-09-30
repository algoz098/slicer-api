# Vendor presets for OrcaSlicer addon

Place vendor profiles here to make them available to the slicer at app boot.

Expected structure (mirrors OrcaSlicer `resources/profiles` layout):

- <Vendor>.json  (vendor root file that lists machine/process/filament entries)
- <Vendor>/
  - machine/*.json
  - process/*.json
  - filament/*.json

Example for Creality Print K2 Plus:

- CrealityPrint.json
- CrealityPrint/
  - machine/
    - Creality K2 Plus.json
    - Creality K2 Plus (0.4 nozzle).json
    - fdm_creality_common.json
    - fdm_machine_common.json
  - process/
    - fdm_process_common.json
    - fdm_process_creality_common.json
    - 0.20mm Standard @Creality K2 Plus (0.4 nozzle).json
  - filament/
    - fdm_filament_common.json
    - fdm_filament_pla.json
    - Creality HF Generic PLA.json

Notes:
- The vendor root JSON (<Vendor>.json) must reference the files above via `sub_path` fields like `machine/<file>.json`, `process/<file>.json`, etc.
- On startup, the app will automatically bundle and load every vendor found here using the Node addon API (`loadVendorBundle` + `loadVendor`).
- No fallbacks are applied. If the folder/files are incomplete or missing, the vendor is skipped.

