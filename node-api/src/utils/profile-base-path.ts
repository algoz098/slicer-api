import * as fs from 'fs'
import * as path from 'path'

export function resolveProfilesBasePath(): string {
  const candidates = [
    path.join(__dirname, '../config/orcaslicer/profiles/resources/profiles'),
    path.join(process.cwd(), 'config/orcaslicer/profiles/resources/profiles'),
    path.join(process.cwd(), 'node-api/config/orcaslicer/profiles/resources/profiles'),
    path.join(process.cwd(), 'OrcaSlicer/resources/profiles'),
    path.join(process.cwd(), 'node-api/OrcaSlicer/resources/profiles')
  ]
  for (const c of candidates) {
    if (fs.existsSync(c)) return c
  }
  return candidates[0]
}

