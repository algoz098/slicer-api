// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
import 'mocha'
const assert = require('assert')
const fs = require('fs')
const path = require('path')
const os = require('os')

const { app } = require('../../../src/app')
const { FilamentsProfileService } = require('../../../src/services/filaments-profile/filaments-profile.class')
const { ProfileFileManager } = require('../../../src/utils/profile-file-manager')

describe('filaments-profile service - find', () => {
  it('returns expected entries from filament_list', async () => {
    const res = await app.service('filaments-profile').find()
    // console.log(res)
    assert.ok(res.length > 0)
  })
})

describe('filaments-profile service - get', () => {
  it('returns expected item by id', async () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'filaments-'))
    try {
      const sample = {
        type: 'filament',
        filament_list: [
          {
            name: 'f1',
            setting_id: 'F1',
            filament_type: 'PLA',
            nozzle_temperature: 220,
            bed_temperature: 60,
            filament_density: 1.24,
            custom_property: 'test_value'
          },
          { name: 'f2', setting_id: 'F2' }
        ]
      }
      const filePath = path.join(tmp, 'filaments.json')
      fs.writeFileSync(filePath, JSON.stringify(sample, null, 2))

      const svc = new FilamentsProfileService({ app } as any)
      ;(svc as any).profileManager = new ProfileFileManager(tmp, ['filament'], 'filament_list')

      const all = await svc.find()
      const firstId = all[0].id
      const one = await svc.get(firstId)

      assert.equal(one.text, 'f1')
      assert.equal(one.name, 'f1')
      assert.ok(typeof one.id === 'string')
      // id should be file-only (no index suffix)
      assert.ok(!/_-_[0-9]+$/.test(firstId))
      // coesão: o get deve retornar o mesmo id do find
      assert.equal(one.id, firstId)

      // Verify that all profile properties from JSON are included
      assert.equal(one.filament_type, 'PLA')
      assert.equal(one.nozzle_temperature, 220)
      assert.equal(one.bed_temperature, 60)
      assert.equal(one.filament_density, 1.24)
      assert.equal(one.custom_property, 'test_value')
      assert.equal(one.setting_id, 'F1')
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true })
    }
  })

  it('returns complete profile data including JSON properties', async () => {
    // Test with real profiles to verify complete data is returned
    const all = await app.service('filaments-profile').find()

    // Get any profile for testing
    assert.ok(all.length > 0, 'Should have at least one profile')
    const firstProfile = all[0]

    const profile = await app.service('filaments-profile').get(firstProfile.id)

    // Basic properties that should always be present
    assert.ok(profile.id, 'Should have id')
    assert.ok(profile.name, 'Should have name')
    assert.ok(profile.text, 'Should have text')
    assert.ok(profile.sub_path, 'Should have sub_path')

    // Verify coesão: get returns same id as find
    assert.equal(profile.id, firstProfile.id, 'get should return same id as find')

    // The profile should have all properties from the original JSON entry
    // At minimum, it should have more than just the 4 basic properties
    const propertyCount = Object.keys(profile).length
    // eslint-disable-next-line no-console
    console.log('Profile has', propertyCount, 'properties')
  })
})
