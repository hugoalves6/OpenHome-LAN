import { describe, expect, it } from 'vitest'
import { appInfoInitialState, appInfoReducer, defaultSettings, Settings } from './appInfo'

describe('LAN preferences in application settings', () => {
  it('loads older settings without losing existing preferences', () => {
    const legacy = { ...defaultSettings, zoomLevel: 125, appTheme: 'dark' as const }
    delete (legacy as Partial<Settings>).lanHub
    const loaded = appInfoReducer(appInfoInitialState, { type: 'load_settings', payload: legacy })
    expect(loaded.settings.lanHub).toEqual(defaultSettings.lanHub)
    expect(loaded.settings.zoomLevel).toBe(125)
    expect(loaded.settings.appTheme).toBe('dark')
  })

  it('restores saved credentials alongside other preferences after JSON serialization', () => {
    const saved = {
      ...defaultSettings,
      saveCardSize: 210,
      lanHub: { address: 'http://192.168.1.125:8321', password: 'test-only-password' },
    }
    const loaded = appInfoReducer(appInfoInitialState, {
      type: 'load_settings',
      payload: JSON.parse(JSON.stringify(saved)),
    })
    expect(loaded.settings.lanHub).toEqual(saved.lanHub)
    expect(loaded.settings.saveCardSize).toBe(210)
    expect(loaded.settingsLoaded).toBe(true)
  })
})
