import { sanitizeBblGcodeTemplates } from '../../../../src/services/slicer/3mf/gcode-sanitizer.js'
import assert from 'assert'

describe('gcode-sanitizer', () => {
    describe('sanitizeBblGcodeTemplates', () => {
        it('should replace flush_volumetric_speeds[index] with safe default', () => {
            const opts: Record<string, any> = {
                machine_start_gcode:
                    'M620.1 E F{flush_volumetric_speeds[initial_no_support_extruder]/2.4053*60} T{flush_temperatures[initial_no_support_extruder]}',
            }
            sanitizeBblGcodeTemplates(opts)
            assert.ok(
                !opts.machine_start_gcode.includes('flush_volumetric_speeds'),
                'flush_volumetric_speeds should be replaced'
            )
            assert.ok(
                !opts.machine_start_gcode.includes('flush_temperatures'),
                'flush_temperatures should be replaced'
            )
            // Should contain the default values instead
            assert.ok(
                opts.machine_start_gcode.includes('0/2.4053*60'),
                'Should replace flush_volumetric_speeds with 0'
            )
            assert.ok(
                opts.machine_start_gcode.includes('T{220}'),
                'Should replace flush_temperatures with 220'
            )
        })

        it('should replace legacy square bracket [BBL_VAR[idx]] with literal (no outer brackets)', () => {
            const opts: Record<string, any> = {
                change_filament_gcode:
                    'M109 S[flush_temperatures[next_extruder]]\nG1 E50 F{flush_volumetric_speeds[next_extruder]/2.4053*60}',
            }
            sanitizeBblGcodeTemplates(opts)
            // Legacy syntax [var[idx]] should become just the literal, no brackets
            assert.ok(
                opts.change_filament_gcode.includes('M109 S220'),
                `Should produce "M109 S220", got: ${opts.change_filament_gcode.split('\n')[0]}`
            )
            assert.ok(
                !opts.change_filament_gcode.includes('[220]'),
                'Should NOT leave [220] legacy bracket artifact'
            )
        })

        it('should guard previous_extruder vector accesses in {if} blocks', () => {
            const opts: Record<string, any> = {
                change_filament_gcode:
                    '{if nozzle_temperature[previous_extruder] > 142 && next_extruder < 255}\nM104 S220\n{endif}',
            }
            sanitizeBblGcodeTemplates(opts)
            assert.ok(
                opts.change_filament_gcode.includes('previous_extruder >= 0'),
                'Should add guard for previous_extruder >= 0'
            )
        })

        it('should guard long_retractions_when_cut[previous_extruder]', () => {
            const opts: Record<string, any> = {
                change_filament_gcode:
                    '{if long_retractions_when_cut[previous_extruder]}\nG1 E-2\n{endif}',
            }
            sanitizeBblGcodeTemplates(opts)
            assert.ok(
                opts.change_filament_gcode.includes('previous_extruder >= 0'),
                'Should add guard for previous_extruder >= 0'
            )
        })

        it('should not double-guard already guarded conditions', () => {
            const opts: Record<string, any> = {
                change_filament_gcode:
                    '{if previous_extruder >= 0 && (nozzle_temperature[previous_extruder] > 142)}\nM104 S220\n{endif}',
            }
            sanitizeBblGcodeTemplates(opts)
            const matches = opts.change_filament_gcode.match(/previous_extruder >= 0/g)
            assert.strictEqual(
                matches?.length,
                1,
                'Should not add duplicate guard'
            )
        })

        it('should leave non-BBL templates unchanged', () => {
            const original = 'G28\nG1 Z5 F3000\nM104 S{nozzle_temperature_initial_layer[0]}'
            const opts: Record<string, any> = {
                machine_start_gcode: original,
            }
            sanitizeBblGcodeTemplates(opts)
            assert.strictEqual(opts.machine_start_gcode, original)
        })

        it('should handle array-valued template fields', () => {
            const opts: Record<string, any> = {
                filament_start_gcode: [
                    'M104 S{flush_temperatures[0]}',
                    'G1 E5',
                ],
            }
            sanitizeBblGcodeTemplates(opts)
            assert.ok(
                !opts.filament_start_gcode[0].includes('flush_temperatures'),
                'Should sanitize array elements'
            )
            assert.strictEqual(opts.filament_start_gcode[1], 'G1 E5', 'Should leave clean elements unchanged')
        })

        it('should handle missing/undefined template fields gracefully', () => {
            const opts: Record<string, any> = {
                nozzle_temperature: [220],
                layer_height: '0.2',
            }
            // Should not throw
            sanitizeBblGcodeTemplates(opts)
            assert.strictEqual(opts.nozzle_temperature[0], 220)
            assert.strictEqual(opts.layer_height, '0.2')
        })

        it('should handle the full BBL A1 machine_start_gcode pattern', () => {
            const bblStartGcode = [
                ';===== machine: A1 =======',
                'M620 S[next_extruder]A',
                'M620.1 E F{flush_volumetric_speeds[initial_no_support_extruder]/2.4053*60} T{flush_temperatures[initial_no_support_extruder]}',
                'M621 S[next_extruder]A',
            ].join('\n')

            const opts: Record<string, any> = {
                machine_start_gcode: bblStartGcode,
            }
            sanitizeBblGcodeTemplates(opts)

            // Should not contain any BBL-proprietary variables
            assert.ok(!opts.machine_start_gcode.includes('flush_volumetric_speeds'))
            assert.ok(!opts.machine_start_gcode.includes('flush_temperatures'))
            // Should still contain normal G-code
            assert.ok(opts.machine_start_gcode.includes('M620'))
        })

        it('should handle the full BBL change_filament_gcode pattern', () => {
            const bblChangeGcode = [
                'M620 S[next_extruder]A',
                '{if nozzle_temperature[previous_extruder] > 142 && next_extruder < 255}',
                'M104 S220',
                '{endif}',
                '{if long_retractions_when_cut[previous_extruder]}',
                'G1 E-2 F1800',
                '{endif}',
            ].join('\n')

            const opts: Record<string, any> = {
                change_filament_gcode: bblChangeGcode,
            }
            sanitizeBblGcodeTemplates(opts)

            // Both {if} blocks should now be guarded
            const guardCount = (opts.change_filament_gcode.match(/previous_extruder >= 0/g) || []).length
            assert.strictEqual(guardCount, 2, 'Both {if} blocks should be guarded')
        })
    })
})
