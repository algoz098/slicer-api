import axios from 'axios'
import * as fs from 'node:fs'
import * as path from 'node:path'
import JSZip from 'jszip'
import assert from 'assert'
import type { Server } from 'http'
import { app } from '../../../../src/app'

//
// Teste focado em reproduzir o bug observado com um 3MF multi‑objeto
// (por exemplo o "Adhesive Wall Hook" salvo para A1), onde:
//   - a cama/print profile está configurada com print_sequence = by object
//   - apenas o primeiro objeto é efetivamente fatiado
//   - o skirt ainda aparece em volta da região onde o segundo objeto deveria estar.
//
// Neste arquivo também exercitamos o caminho on‑the‑fly completo com
// perfis BBL A1 explícitos: impressora Bambu Lab A1 0.4 nozzle,
// processo 0.16mm Optimal @BBL A1 e filamento Bambu PLA Basic @BBL A1,
// resolvendo toda a cadeia de herança a partir dos JSONs do OrcaSlicer.
//

type ProfileDir = 'machine' | 'filament' | 'process'

const PROFILES_ROOT = path.resolve(
	__dirname,
	'../../../../../OrcaSlicer/resources/profiles/BBL',
)

const PROFILE_DIRS: Record<ProfileDir, string> = {
	machine: path.join(PROFILES_ROOT, 'machine'),
	filament: path.join(PROFILES_ROOT, 'filament'),
	process: path.join(PROFILES_ROOT, 'process'),
}

function resolveProfileChain(
	name: string,
	dir: ProfileDir,
	visited = new Set<string>(),
): Record<string, unknown> {
	if (visited.has(name)) return {}
	visited.add(name)

	const file = path.join(PROFILE_DIRS[dir], `${name}.json`)
	if (!fs.existsSync(file)) return {}

	const raw = JSON.parse(fs.readFileSync(file, 'utf8')) as any

	let base: Record<string, unknown> = {}
	if (raw.inherits && typeof raw.inherits === 'string') {
		base = resolveProfileChain(raw.inherits, dir, visited)
	}

	// Merge: filho sobrescreve pai, remove metadados irrelevantes
	const child: Record<string, unknown> = {}
	for (const [k, v] of Object.entries(raw)) {
		if (
			[
				'name',
				'type',
				'from',
				'instantiation',
				'inherits',
				'setting_id',
				'version',
				'force_update',
				'description',
				'url',
				'bed_model',
				'bed_texture',
				'default_bed_type',
				'family',
				'machine_tech',
				'model_id',
				'default_materials',
				'compatible_printers',
			].includes(k)
		)
			continue
		child[k] = v
	}

	return { ...base, ...child }
}

function flattenValues(
	obj: Record<string, unknown>,
): Record<string, string | number | boolean | string[]> {
	const out: Record<string, string | number | boolean | string[]> = {}
	for (const [k, v] of Object.entries(obj)) {
		if (Array.isArray(v)) {
			if (v.length === 0) continue
			if (v.length === 1) {
				const s = String(v[0])
				const n = Number(s)
				if (!Number.isNaN(n) && /^[+-]?[\d.]+$/.test(s.trim())) {
					out[k] = n
				} else if (s === 'true') {
					out[k] = true
				} else if (s === 'false') {
					out[k] = false
				} else {
					out[k] = s
				}
			} else {
				out[k] = v.map(String)
			}
		} else if (typeof v === 'string') {
			const n = Number(v)
			if (!Number.isNaN(n) && /^[+-]?[\d.]+$/.test(v.trim())) {
				out[k] = n
			} else if (v === 'true') {
				out[k] = true
			} else if (v === 'false') {
				out[k] = false
			} else {
				out[k] = v
			}
		} else if (typeof v === 'number' || typeof v === 'boolean') {
			out[k] = v
		}
	}
	return out
}

describe('slicer/3mf - BBL A1 multi-objeto (BUG conhecido)', function () {
	this.timeout(300_000)

	let server: Server | null = null
	let baseURL = ''
	let inputFile: string
		let mergedConfig: Record<string, string | number | boolean | string[]>

		before(async () => {
			inputFile = path.resolve(
				__dirname,
				'../../../fixtures/adhesive-wall-hook-a1-multiobject.3mf',
			)

			// Resolve perfis BBL A1 completos via JSON do OrcaSlicer
			const printerRaw = resolveProfileChain('Bambu Lab A1 0.4 nozzle', 'machine')
			const filamentRaw = resolveProfileChain('Bambu PLA Basic @BBL A1', 'filament')
			const processRaw = resolveProfileChain('0.16mm Optimal @BBL A1', 'process')

			const printerConfig = flattenValues(printerRaw)
			const filamentConfig = flattenValues(filamentRaw)
			const processConfig = flattenValues(processRaw)

			const raw: Record<string, string | number | boolean | string[]> = {
				...printerConfig,
				...filamentConfig,
				...processConfig,
				printer_model: 'Bambu Lab A1',
				printer_variant: '0.4',
			}

			// Removemos templates de G-code proprietários BBL que usam comandos
			// ainda não suportados pelo fork (M1002, G392, etc.), pois eles não
			// são necessários para validar o bug multi‑objeto.
			const GCODE_TEMPLATE_KEYS = [
				'machine_start_gcode',
				'machine_end_gcode',
				'change_filament_gcode',
				'layer_change_gcode',
				'machine_pause_gcode',
				'time_lapse_gcode',
				'printing_by_object_gcode',
				'before_layer_change_gcode',
				'filament_start_gcode',
				'filament_end_gcode',
			]
			for (const k of GCODE_TEMPLATE_KEYS) {
				// eslint-disable-next-line @typescript-eslint/no-dynamic-delete
				delete (raw as any)[k]
			}

			mergedConfig = raw

			server = await app.listen(0)
			const address = server.address()
			const port = typeof address === 'string' || address === null ? 0 : address.port
			baseURL = `http://127.0.0.1:${port}`
		})

	after(async () => {
		try {
			if (server) server.close()
		} catch {}
	})

		async function sliceAndGetGcode() {
		const body: any = {
			filePath: inputFile,
			plate: 1,
				config: mergedConfig,
		}

		const resp = await axios.post(`${baseURL}/slicer/3mf`, body, {
			headers: { 'content-type': 'application/json' },
			validateStatus: () => true,
		})

		assert.strictEqual(
			resp.status,
			201,
			`Slice falhou (status=${resp.status}). Body: ${JSON.stringify(resp.data)}`,
		)

		const outPath: string | undefined = resp.data?.outputPath
		assert.ok(typeof outPath === 'string' && outPath.length > 0, 'outputPath deve ser uma string n�o vazia')
		assert.ok(fs.existsSync(outPath), `Arquivo de sa�da n�o existe em disco: ${outPath}`)

		const content = await fs.promises.readFile(outPath)
		const zip = await JSZip.loadAsync(content)
		const gcodeEntryName = Object.keys(zip.files).find(name => /Metadata\/.*\.gcode$/i.test(name))
		assert.ok(gcodeEntryName, '3MF gerado n�o cont�m Metadata/*.gcode')

		const gcode = await zip.files[gcodeEntryName!]!.async('string')
		return gcode
	}

	function extractConfigBlock(s: string): string {
		const start = s.indexOf('; CONFIG_BLOCK_START')
		const end = s.indexOf('; CONFIG_BLOCK_END')
		if (start === -1 || end === -1) return ''
		return s.slice(start, end + '; CONFIG_BLOCK_END'.length)
	}

	it('DEVE fatiar todos os objetos quando print_sequence=by object está configurado na mesa', async () => {
		const gcode = await sliceAndGetGcode()
		const cfg = extractConfigBlock(gcode)

		// O 3MF tem print_sequence=by object na config da mesa (plate).
		// Bug: arrange_order nao inicializado fazia apenas o 1o objeto ser fatiado.
		// Fix: preparePrintConfig() e usado no re-apply pos-reposicionamento,
		// preservando print_sequence=by object; e o bloco de arrange_order dispara.
		//
		// Verificamos dois invariantes:
		// 1. print_sequence=by object permanece (config da mesa foi respeitada)
		// 2. G-code contem secoes de multiplos objetos (todos foram incluidos)
		assert.ok(
			/print_sequence\s*=\s*by object/.test(cfg),
			`Esperado print_sequence=by object no CONFIG_BLOCK (config da mesa deve ser preservada), mas obteve:\n${cfg}`,
		)

		const objectSections = gcode.match(/; printing object /gi) ?? []
		assert.ok(
			objectSections.length >= 2,
			`Esperado >= 2 secoes de objeto ('; printing object') no G-code, mas encontrou ${objectSections.length}. ` +
			`Bug onde apenas o 1o objeto e fatiado.\nPrimeiros 2000 chars:\n${gcode.slice(0, 2000)}`,
		)
	})
})
