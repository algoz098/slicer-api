const path = require('path')
const fs = require('fs')

const appRoot = path.resolve(__dirname, '..')
const schemaPath = path.join(appRoot, 'src', 'services', 'printer-profiles', 'printer-profiles.schema')

// Load the compiled schema validator from lib if available, otherwise from src
let validatorModule
try {
  validatorModule = require(path.join(appRoot, 'lib', 'services', 'printer-profiles', 'printer-profiles.schema'))
} catch (e) {
  validatorModule = require(schemaPath)
}

// The getValidator used in the module relies on project validators; we'll just
// reconstruct a minimal TypeBox validator here for the profileFileContentSchema.
const { Type } = require('@feathersjs/typebox')
const Ajv = require('ajv').default
const addFormats = require('ajv-formats')

const ajv = new Ajv({ allErrors: true, allowUnionTypes: true })
addFormats(ajv)

// Import the profileFileContentSchema directly from the source file by parsing it
// as JSON is not possible; instead we'll recreate a compatible schema here similar
// to what's exported in the repo.

const profileEntrySchema = {
  type: 'object',
  properties: {
    name: { type: 'string' },
    sub_path: { type: 'string' }
  },
  required: ['name', 'sub_path'],
  additionalProperties: false
}

const profileFileContentSchema = {
  type: 'object',
  properties: {
    name: { type: 'string' },
    version: { anyOf: [{ type: 'string' }, { type: 'number' }] },
    force_update: { anyOf: [{ type: 'string' }, { type: 'number' }, { type: 'boolean' }] },
    description: { type: 'string' },
    url: { type: 'string' },
    author: { type: 'string' },
    license: { type: 'string' },
    tags: { type: 'array', items: { type: 'string' } },
    metadata: { type: 'object', additionalProperties: true },
    machine_model_list: { type: 'array', items: profileEntrySchema },
    process_list: { type: 'array', items: profileEntrySchema },
    filament_list: { type: 'array', items: profileEntrySchema },
    machine_list: { type: 'array', items: profileEntrySchema }
  },
  additionalProperties: true
}

const validate = ajv.compile(profileFileContentSchema)

const samples = [
  'Prusa.json',
  'Anycubic.json',
  'Vivedino.json',
  'Vzbot.json',
  'Mellow.json'
]

const profilesDir = path.join(appRoot, 'config', 'orcaslicer', 'profiles', 'resources', 'profiles')

for (const f of samples) {
  const full = path.join(profilesDir, f)
  try {
    const raw = fs.readFileSync(full, 'utf8')
    const data = JSON.parse(raw)
    const ok = validate(data)
    if (ok) {
      console.log(f + ': OK')
    } else {
      console.log(f + ': INVALID')
      console.log(validate.errors)
    }
  } catch (e) {
    console.error('Error reading', f, e.message)
  }
}
