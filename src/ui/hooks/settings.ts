import { R } from '@openhome-core/util/functional'
import useBackend from '@openhome-core/backend/useBackend'
import { AppInfoContext, Settings as SettingsType } from '@openhome-ui/state/appInfo'
import { useContext } from 'react'
import { MonDisplayState } from './monDisplay'

export default function useSettings() {
  const [appInfoState, dispatchAppInfoState] = useContext(AppInfoContext)
  const backend = useBackend()

  async function updateSettings(newSettings: Partial<SettingsType>) {
    const updated = { ...appInfoState.settings, ...newSettings }
    const result = await backend.updateSettings(updated)
    if (R.isErr(result)) return result
    dispatchAppInfoState({ type: 'load_settings', payload: updated })
    return result
  }

  async function updateMonDisplayState(newState: Partial<MonDisplayState>) {
    return updateSettings({
      monDisplayState: { ...appInfoState.settings.monDisplayState, ...newState },
    })
  }

  return {
    settings: appInfoState.settings,
    settingsLoaded: appInfoState.settingsLoaded,
    updateSettings,
    monDisplayState: appInfoState.settings.monDisplayState,
    updateMonDisplayState,
    extraSaveTypes: appInfoState.extraSaveTypes,
  }
}
