import { invoke } from '@tauri-apps/api/core'
import { PathData } from '../save/util/path'

export type RemoteSaveStatus = {
  path: string
  card_id?: string
  online: boolean
  synced: boolean
  pending: boolean
  busy: boolean
  sha256?: string
}
type Entry = {
  lease: string
  sha256: string
  bytes: Uint8Array
  locked: string
  pending?: string
  uncertain?: boolean
  attempted?: string
  committed?: boolean
}
type Transport = (route: string, body?: unknown) => Promise<unknown>
export const isRemoteSave = (path: string) => path.startsWith('openhome://')
const remotePath = (path: string) => path.slice('openhome://'.length)
export const remotePathData = (path: string): PathData => ({
  raw: 'openhome://' + path,
  name: path.split('/').at(-1) ?? 'save.sav',
  dir: 'openhome://roms/nds',
  ext: 'sav',
  separator: '/',
})
function base64(bytes: Uint8Array) {
  let binary = ''
  for (let offset = 0; offset < bytes.length; offset += 8192)
    binary += String.fromCharCode(...bytes.subarray(offset, offset + 8192))
  return btoa(binary)
}
const pause = (ms: number) => new Promise((resolve) => setTimeout(resolve, ms))

export class LiveHub {
  base = 'http://192.168.1.125:8321'
  private token = ''
  private entries = new Map<string, Entry>()
  private listeners = new Set<() => void>()
  private revision = 0
  private polling = false
  private timer?: ReturnType<typeof setInterval>
  saves: RemoteSaveStatus[] = []
  message = 'Connect to your LAN hub'
  constructor(private transport?: Transport) {}
  subscribe = (listener: () => void) => {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }
  snapshot = () => this.revision
  private notify() {
    this.revision++
    this.listeners.forEach((listener) => listener())
  }
  private async request<T>(route: string, body?: unknown): Promise<T> {
    if (this.transport) return (await this.transport(route, body)) as T
    return invoke<T>('lan_hub_request', {
      base: this.base,
      route,
      token: this.token || null,
      body: body ?? null,
    })
  }
  async connect(base: string, password: string) {
    if (this.entries.size) throw new Error('Close remote saves before changing the hub connection')
    this.base = base.replace(/\/$/, '')
    const login = await this.request<{ token: string }>('/ohnx/login', { password })
    this.token = login.token
    await this.poll()
    if (!this.timer) this.timer = setInterval(() => void this.poll(), 3000)
  }
  async poll() {
    if (this.polling || (!this.transport && !this.token)) return
    this.polling = true
    try {
      const status = await this.request<{ saves: RemoteSaveStatus[] }>('/ohnx/live/status')
      this.saves = status.saves
      for (const [path, entry] of this.entries) {
        const row = this.saves.find((save) => save.path === path)
        if (entry.uncertain) {
          const attempted = entry.attempted
          if (attempted && row?.sha256 === attempted) {
            entry.sha256 = attempted
            entry.pending = attempted
            entry.committed = true
            entry.uncertain = false
          } else if (row?.online && row.sha256 === entry.sha256 && !row.pending) {
            entry.uncertain = false
            entry.attempted = undefined
          } else continue
        }
        if (entry.pending) {
          if (row?.online && row.synced && !row.pending && row.sha256 === entry.pending)
            entry.pending = undefined
          else {
            entry.locked = 'Waiting for delivery to card. Keep the DSi app open.'
            continue
          }
        }
        if (!row?.online) {
          entry.locked = 'DSi offline. Editing is locked.'
          continue
        }
        try {
          await this.request('/ohnx/live/renew', { path, lease: entry.lease })
          entry.locked = ''
        } catch (error) {
          entry.locked = String(error)
        }
      }
      this.message = 'Hub connected — keep OpenHome DS open on the console'
    } catch {
      this.message = 'Hub offline'
      for (const entry of this.entries.values())
        entry.locked = 'Hub unavailable. Editing is locked.'
      this.saves = this.saves.map((save) => ({ ...save, online: false }))
    } finally {
      this.polling = false
      this.notify()
    }
  }
  lockReason(path: string) {
    if (!isRemoteSave(path)) return ''
    return this.entries.get(remotePath(path))?.locked ?? 'Remote save must be reopened'
  }
  assertEditable(path: string) {
    const reason = this.lockReason(path)
    if (reason) throw new Error(reason)
  }
  async load(path: string) {
    const remote = remotePath(path)
    const existing = this.entries.get(remote)
    if (existing) {
      this.assertEditable(path)
      await this.request('/ohnx/live/renew', { path: remote, lease: existing.lease })
      return existing.bytes.slice()
    }
    // Keep the first release single-console/single-save: avoid partial multi-save remote commits.
    if (this.entries.size) throw new Error('Close the current console save before opening another')
    const result = await this.request<{ lease: string; sha256: string; bytes: string }>(
      '/ohnx/live/acquire',
      { path: remote }
    )
    const bytes = Uint8Array.fromBase64(result.bytes)
    if (bytes.length !== 524288) throw new Error('Invalid hub save size')
    this.entries.set(remote, { lease: result.lease, sha256: result.sha256, bytes, locked: '' })
    this.notify()
    return bytes.slice()
  }
  hasUnresolvedWrite() {
    return [...this.entries.values()].some(
      (entry) => !!entry.pending || !!entry.uncertain || !!entry.committed
    )
  }
  markWritesComplete() {
    for (const entry of this.entries.values()) entry.committed = false
  }
  async preflight(paths: string[]) {
    for (const path of paths.filter(isRemoteSave)) {
      const entry = this.entries.get(remotePath(path))
      if (entry?.uncertain) await this.poll()
      if (entry?.pending) await this.write(path, entry.bytes)
      this.assertEditable(path)
      if (!entry) throw new Error('Remote save not open')
      await this.request('/ohnx/live/renew', { path: remotePath(path), lease: entry.lease })
    }
  }
  async write(path: string, bytes: Uint8Array) {
    const remote = remotePath(path),
      entry = this.entries.get(remote)
    if (!entry) throw new Error('Remote save not open')
    // After a transport timeout the hub may already have committed. Poll/reopen rather than retrying blindly.
    if (!entry.pending) {
      this.assertEditable(path)
      const digest = await crypto.subtle.digest('SHA-256', bytes.slice())
      entry.attempted = Array.from(new Uint8Array(digest), (byte) =>
        byte.toString(16).padStart(2, '0')
      ).join('')
      entry.bytes = bytes.slice()
      entry.locked = 'Sending edited save…'
      this.notify()
      try {
        const result = await this.request<{ sha256: string; delivered: boolean }>(
          '/ohnx/live/commit',
          {
            path: remote,
            lease: entry.lease,
            sha256: entry.sha256,
            bytes: base64(bytes),
          }
        )
        if (result.sha256 !== entry.attempted)
          throw new Error('Hub returned an unexpected edited-save checksum')
        entry.committed = result.sha256 !== entry.sha256
        entry.sha256 = result.sha256
        entry.attempted = undefined
        if (result.delivered) {
          entry.locked = ''
          this.notify()
          return
        }
        entry.pending = result.sha256
      } catch (error) {
        entry.uncertain = true
        entry.locked = 'Delivery uncertain. Check the hub and reopen; your editor draft remains.'
        this.notify()
        throw error
      }
    }
    entry.locked = 'Waiting for delivery to card. Keep the DSi app open.'
    this.notify()
    const deadline = Date.now() + 120000
    while (entry.pending && Date.now() < deadline) {
      await this.poll()
      if (entry.pending) await pause(1000)
    }
    if (entry.pending)
      throw new Error(
        'Saved on hub, but card delivery is unconfirmed. Keep this draft and reconnect the DSi.'
      )
    this.assertEditable(path)
  }
  async close(path: string) {
    const remote = remotePath(path),
      entry = this.entries.get(remote)
    if (!entry) return
    await this.request('/ohnx/live/release', { path: remote, lease: entry.lease }).catch(
      () => undefined
    )
    this.entries.delete(remote)
    this.notify()
  }
}
export const liveHub = new LiveHub()
