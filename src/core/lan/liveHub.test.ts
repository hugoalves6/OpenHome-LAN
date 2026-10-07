import { describe, expect, it, vi } from 'vitest'
import { LiveHub, remotePathData } from './liveHub'

const path = 'roms/nds/disposable.sav'
const virtual = remotePathData(path).raw
const bytes = new Uint8Array(524288)
const encoded = Buffer.from(bytes).toString('base64')
const editedSha = '07854d2fef297a06ba81685e660c332de36d5d18d546927d30daad6d7fda1541'
const status = (online = true, pending = false, sha256 = 'old') => ({
  saves: [{ path, online, synced: !pending, pending, busy: true, sha256 }],
})

describe('online-only console editing', () => {
  it('keeps local saves editable and locks unknown remote saves', () => {
    const hub = new LiveHub()
    expect(hub.lockReason('C:/local.sav')).toBe('')
    expect(() => hub.assertEditable(virtual)).toThrow('reopened')
  })
  it('rejects offline opens and enforces one remote save', async () => {
    const offline = new LiveHub(async () => {
      throw new Error('Console offline')
    })
    await expect(offline.load(virtual)).rejects.toThrow('offline')
    const hub = new LiveHub(async () => ({ lease: 'lease', sha256: 'old', bytes: encoded }))
    expect(await hub.load(virtual)).toHaveLength(524288)
    await expect(hub.load(remotePathData('roms/nds/other.sav').raw)).rejects.toThrow('Close')
  })
  it('locks immediately on an offline status or expired editing lease', async () => {
    let online = true,
      expired = false
    const hub = new LiveHub(async (route) => {
      if (route.endsWith('acquire')) return { lease: 'lease', sha256: 'old', bytes: encoded }
      if (route.endsWith('status')) return status(online)
      if (expired) throw new Error('Editing session expired')
      return { ok: true }
    })
    await hub.load(virtual)
    online = false
    await hub.poll()
    expect(() => hub.assertEditable(virtual)).toThrow('offline')
    online = true
    expired = true
    await hub.poll()
    expect(() => hub.assertEditable(virtual)).toThrow('expired')
  })
  it('requires console receipt before a write resolves', async () => {
    let acknowledged = false
    const transport = vi.fn(async (route: string) => {
      if (route.endsWith('acquire')) return { lease: 'lease', sha256: 'old', bytes: encoded }
      if (route.endsWith('commit')) return { sha256: editedSha, delivered: false }
      if (route.endsWith('status')) {
        acknowledged = true
        return status(true, false, editedSha)
      }
      return { ok: true }
    })
    const hub = new LiveHub(transport)
    await hub.load(virtual)
    await hub.write(virtual, bytes)
    expect(acknowledged).toBe(true)
    expect(hub.lockReason(virtual)).toBe('')
    expect(transport.mock.calls.some(([route]) => route.endsWith('commit'))).toBe(true)
  })
  it('never blindly retries a commit after an uncertain response', async () => {
    const transport = vi.fn(async (route: string) => {
      if (route.endsWith('acquire')) return { lease: 'lease', sha256: 'old', bytes: encoded }
      throw new Error('Disconnected')
    })
    const hub = new LiveHub(transport)
    await hub.load(virtual)
    await expect(hub.write(virtual, bytes)).rejects.toThrow('Disconnected')
    await expect(hub.write(virtual, bytes)).rejects.toThrow('uncertain')
    expect(transport.mock.calls.filter(([route]) => route.endsWith('commit'))).toHaveLength(1)
  })
  it('recovers a committed response that was lost without allowing discard', async () => {
    let committed = false
    const attempted = Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)))
      .map((byte) => byte.toString(16).padStart(2, '0'))
      .join('')
    const transport = vi.fn(async (route: string) => {
      if (route.endsWith('acquire')) return { lease: 'lease', sha256: 'old', bytes: encoded }
      if (route.endsWith('commit')) {
        committed = true
        throw new Error('Response lost after commit')
      }
      if (route.endsWith('status')) return status(true, committed, committed ? attempted : 'old')
      return { ok: true }
    })
    const hub = new LiveHub(transport)
    await hub.load(virtual)
    await expect(hub.write(virtual, bytes)).rejects.toThrow('Response lost')
    expect(hub.hasUnresolvedWrite()).toBe(true)
    await hub.poll()
    expect(hub.hasUnresolvedWrite()).toBe(true)
    expect(transport.mock.calls.filter(([route]) => route.endsWith('commit'))).toHaveLength(1)
  })
})
