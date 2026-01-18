#!/usr/bin/env python3
import json
import zipfile
import sys

input_3mf = sys.argv[1] if len(sys.argv) > 1 else '/Users/maosone/Documents/slicer-api/node-api/test/fixtures/teste_a1mini.3mf'

with zipfile.ZipFile(input_3mf, 'r') as z:
    # Listar todos os arquivos de config no 3MF
    print('=== Arquivos no 3MF ===')
    for name in z.namelist():
        if 'config' in name.lower() or 'settings' in name.lower():
            print(f'  {name}')
    print()
    
    # Verificar process_settings (print preset overrides)
    for name in ['Metadata/process_settings_1.config', 'Metadata/print_settings_1.config']:
        try:
            content = z.read(name).decode('utf-8')
            data = json.loads(content)
            print(f'=== {name} ===')
            print(f'skirt_loops: {data.get("skirt_loops", "N/A")}')
            print(f'brim_type: {data.get("brim_type", "N/A")}')
            print()
        except Exception as e:
            print(f'{name}: {e}')
            print()
    
    # Verificar different_settings_to_system
    content = z.read('Metadata/project_settings.config').decode('utf-8')
    data = json.loads(content)
    print('=== different_settings_to_system (project_settings.config) ===')
    diff = data.get('different_settings_to_system', [])
    print(f'Value: {diff}')
    print()
    
    # Verificar skirt_loops no project_settings
    print('=== project_settings.config keys relevantes ===')
    print(f'skirt_loops: {data.get("skirt_loops", "N/A")}')
    print(f'brim_type: {data.get("brim_type", "N/A")}')

