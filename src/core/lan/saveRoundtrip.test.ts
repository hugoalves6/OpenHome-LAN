import { execFileSync } from 'node:child_process'
import fs from 'node:fs'
import path from 'node:path'
import { describe, expect, it } from 'vitest'
import { HGSSSAV } from '../save/HGSSSAV'
import { remotePathData } from './liveHub'

describe('Windows save bytes accepted by the LAN hub', () => {
  it('moves a boxed Pokemon on a disposable upstream HeartGold fixture', () => {
    const original = fs.readFileSync(
      path.resolve('src/core/save/__test__/save-files/heartgold.sav')
    )
    const save = new HGSSSAV(
      remotePathData('roms/nds/test-heartgold.sav'),
      new Uint8Array(original)
    )
    let source: [number, number] | undefined, dest: [number, number] | undefined
    for (let box = 0; box < 18; box++)
      for (let slot = 0; slot < 30; slot++) {
        if (save.getMonAt(box, slot) && !source) source = [box, slot]
        if (!save.getMonAt(box, slot) && !dest) dest = [box, slot]
      }
    expect(source).toBeDefined()
    expect(dest).toBeDefined()
    if (!source || !dest) throw new Error('Fixture requires a Pokemon and an empty slot')
    const mon = save.getMonAt(...source)
    save.setMonAt(...dest, mon)
    save.setMonAt(...source, undefined)
    save.updatedBoxSlots.push(
      { box: source[0], boxSlot: source[1] },
      { box: dest[0], boxSlot: dest[1] }
    )
    const edited = save.prepareWriter().bytes
    expect(edited.length).toBe(524288)
    const result = execFileSync(
      'python',
      [
        '-c',
        `
import sys,json,base64
sys.path.insert(0,'lan/server')
from nds_boxes import Save,u16,decrypt
r=json.load(sys.stdin)
old=Save(base64.b64decode(r['old'])); new=Save(base64.b64decode(r['new']))
source=r['source']; dest=r['dest']
assert u16(decrypt(old.record(*source)),8)==u16(decrypt(new.record(*dest)),8)
assert u16(decrypt(new.record(*source)),8)==0
allowed=set(range(old.offset(*source),old.offset(*source)+136))|set(range(old.offset(*dest),old.offset(*dest)+136))
checksum=old.storage+old.layout[1]-2;allowed.update((checksum,checksum+1))
assert all(i in allowed for i,(a,b) in enumerate(zip(old.data,new.data)) if a!=b)
print('Hub parser verified desktop edit and byte preservation')
`,
      ],
      {
        input: JSON.stringify({
          old: original.toString('base64'),
          new: Buffer.from(edited).toString('base64'),
          source,
          dest,
        }),
        encoding: 'utf8',
      }
    )
    expect(result).toContain('verified')
  })
})
