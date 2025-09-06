// For more information about this file see https://dove.feathersjs.com/guides/cli/service.test.html
import 'mocha'
const assert = require('assert')
const fs = require('fs')
const path = require('path')
const os = require('os')

const { app } = require('../../../src/app')
const { PrinterProfilesService } = require('../../../src/services/printer-profiles/printer-profiles.class')

describe('printer-profiles service', () => {
  it('implements find/get/create/update/patch/remove using a temp profiles dir', async () => {
    const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'profiles-'))
    try {
      // create a sample profile file
      const sample = {
        type: 'process',
        process_list: [
          { name: 'p1', setting_id: 'S1' },
          { name: 'p2', setting_id: 'S2' }
        ]
      }
      const filePath = path.join(tmp, 'example.json')
      fs.writeFileSync(filePath, JSON.stringify(sample, null, 2))

      // Create service with temporary directory
      const svc = new PrinterProfilesService({ app } as any)
      // Override the profileManager to use the temporary directory
      const { ProfileFileManager } = require('../../../src/utils/profile-file-manager')
      ;(svc as any).profileManager = new ProfileFileManager(tmp, ['process'])

      const all = await svc.find()
      assert.equal(all.length, 2)
      assert.equal(all[0].text, 'p1')

      const one = await svc.get(all[0].id)
      assert.equal(one.text, 'p1')

      const created = await svc.create({ text: 'p3' } as any)
      assert.equal(created.text, 'p3')
      const all2 = await svc.find()
      assert.equal(all2.length, 3)

      const updated = await svc.update(created.id, { text: 'p3-up' } as any)
      assert.equal(updated.text, 'p3-up')
      const patched = await svc.patch(created.id, { foo: 'bar' } as any)
      assert.equal((patched as any).foo, 'bar')

      const removed = await svc.remove(created.id)
      assert.equal(removed.text, 'p3-up')
      const all3 = await svc.find()
      assert.equal(all3.length, 2)
    } finally {
      fs.rmSync(tmp, { recursive: true, force: true })
    }
  })
})
