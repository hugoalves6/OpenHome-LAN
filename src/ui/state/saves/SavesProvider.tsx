import { liveHub, isRemoteSave } from '@openhome-core/lan/liveHub'
import useBackend from '@openhome-core/backend/useBackend'
import { getCurrentWindow } from '@tauri-apps/api/window'
import { SAV } from '@openhome-core/save/interfaces'
import { OhpkmIdentifier } from '@openhome-core/pkm/Lookup'
import { OHPKM } from '@openhome-core/pkm/OHPKM'
import { SAVClass } from '@openhome-core/save/util'
import { $R, Option, R, range, Result } from '@openhome-core/util/functional'
import { Dialog } from '@openhome-ui/components/dialog/Dialog'
import PromptDialog from '@openhome-ui/components/dialog/PromptDialog'
import { ErrorIcon } from '@openhome-ui/components/Icons'
import useDisplayError from '@openhome-ui/hooks/displayError'
import { Button, Callout, Flex } from '@radix-ui/themes'
import {
  ReactNode,
  useCallback,
  useContext,
  useEffect,
  useEffectEvent,
  useReducer,
  useRef,
  useState,
} from 'react'
import { useNavigate } from 'react-router'
import { useBanksAndBoxes } from '../../state-zustand/banks-and-boxes/store'
import { useConvertStrategies } from '../convert-strategies'
import { ItemBagContext } from '../items/reducer'
import { useOhpkmStore } from '../ohpkm'
import { openSavesReducer, SavesContext } from './reducer'

export type SavesProviderProps = {
  children: ReactNode
}

type SaveTypeCallback = (saveType?: SAVClass | PromiseLike<SAVClass>) => void

export default function SavesProvider({ children }: SavesProviderProps) {
  const backend = useBackend()
  const [itemBagState, bagDispatch] = useContext(ItemBagContext)
  const [releaseWarningDisplayed, setReleaseWarningDisplayed] = useState(false)
  const [saving, setSavingState] = useState(false)
  const saveInFlight = useRef(false)
  const [closeRequest, setCloseRequest] = useState<{
    kind: 'save' | 'window' | 'discard'
    save?: SAV
  }>()
  const setSaving = (value: boolean) => {
    saveInFlight.current = value
    setSavingState(value)
  }
  const [changesSavedDisplayed, setChangesSavedDisplayed] = useState(false)
  const displayError = useDisplayError()
  const [openSavesState, openSavesDispatch] = useReducer(openSavesReducer, {
    monsToRelease: [],
    openSaves: {},
    pendingMonLocations: [],
  })
  const { defaultConvertStrategy } = useConvertStrategies()
  const disambiguationResolver = useRef<Option<SaveTypeCallback>>(undefined)
  const [disambiguationSaveTypes, setDisambiguationSaveTypes] = useState<Option<SAVClass[]>>()
  const navigate = useNavigate()
  const ohpkmStore = useOhpkmStore()
  const { banks, getCurrentBank, bankModified, markBankSaved } = useBanksAndBoxes()

  const promptDisambiguation = useCallback(async (possibleSaveTypes: SAVClass[]) => {
    setDisambiguationSaveTypes(possibleSaveTypes)

    return new Promise<Option<SAVClass>>((resolve) => {
      disambiguationResolver.current = resolve
    })
  }, [])

  const allOpenSaves = Object.values(openSavesState.openSaves)
    .sort((a, b) => a.index - b.index)
    .map((data) => data.save)

  const saveChanges = useCallback(
    async (releaseWarningAccepted: boolean): Promise<Result<null, SaveError[]>> => {
      if (saveInFlight.current) return R.Err([BackendSaveError('A save is already in progress')])
      if (openSavesState.pendingMonLocations.length)
        return R.Err([BackendSaveError('Wait for the current Pokémon move to finish')])
      try {
        await liveHub.preflight(allOpenSaves.map((save) => save.filePath.raw))
      } catch (error) {
        return R.Err([BackendSaveError(String(error))])
      }

      const shouldReleasePokemon = openSavesState.monsToRelease.length > 0
      if (shouldReleasePokemon && !releaseWarningAccepted) {
        setReleaseWarningDisplayed(true)
        return R.Err([BackendSaveError('Confirm Pokémon releases before saving')])
      }

      setSaving(true)
      const result = await backend.startTransaction()

      if (R.isErr(result)) {
        displayError('Error Starting Save Transaction', result.error)
        setSaving(false)
        return R.Err([TransactionStart(result.error)])
      }

      // Write appropriate trainer data to handler fields
      for (const save of allOpenSaves) {
        for (const boxNum of range(save.getBoxCount())) {
          for (const boxSlot of range(save.boxSlotCount)) {
            const data = save.getMonAt(boxNum, boxSlot)
            if (!data) continue

            const trackedData = await ohpkmStore.loadIfTracked(data)
            if (!trackedData) continue

            trackedData.tradeToSave(save)

            const converted = save.convertOhpkm(trackedData, defaultConvertStrategy)
            if (R.isErr(converted)) {
              await backend.rollbackTransaction()
              setSaving(false)
              return Promise.resolve(R.Err([PkmConversion(converted.error)]))
            }
            $R(save.convertOhpkm(trackedData, defaultConvertStrategy)).map((mon) =>
              save.setMonAt(boxNum, boxSlot, mon)
            )
          }
        }
      }

      const saveWriters = allOpenSaves.map((save) => save.prepareWriter())

      const bankResult = await backend.writeHomeBanks({
        banks,
        current_bank: getCurrentBank().index,
      })
      if (R.isErr(bankResult)) {
        await backend.rollbackTransaction()
        setSaving(false)
        return R.Err([BackendSaveError(bankResult.error)])
      }

      const promises = [
        backend.writeAllSaveFiles(saveWriters),
        backend.deleteHomeMons(
          openSavesState.monsToRelease.filter(
            (monOrIdentifier) => typeof monOrIdentifier === 'string'
          )
        ),
      ]

      if (itemBagState.modified) {
        const saveBagResult = await backend.saveItemBag(itemBagState.itemCounts)
        if (R.isErr(saveBagResult)) {
          displayError('Error Saving Bag', saveBagResult.error)
          await backend.rollbackTransaction()
          setSaving(false)
          return R.Err([SaveItemBagData(saveBagResult.error)])
        }
      }

      const results = (await Promise.all(promises)).flat()
      const errors = results.filter(R.isErr).map((r) => r.error)

      if (errors.length) {
        displayError('Error Saving', errors)
        await backend.rollbackTransaction()
        setSaving(false)
        return R.Err(errors.map(BackendSaveError))
      }

      const syncedStateResult = await backend.saveSyncedState()
      if (R.isErr(syncedStateResult)) {
        displayError('Error Saving', syncedStateResult.error)
        await backend.rollbackTransaction()
        setSaving(false)
        return R.Err([BackendSaveError(syncedStateResult.error)])
      }

      const commitResult = await backend.commitTransaction()
      if (R.isErr(commitResult)) {
        setSaving(false)
        return R.Err([TransactionCommit(commitResult.error)])
      }

      openSavesDispatch({ type: 'clear_updated_box_slots' })
      openSavesDispatch({ type: 'clear_mons_to_release' })

      liveHub.markWritesComplete()
      markBankSaved()
      bagDispatch({ type: 'clear_modified' })
      setChangesSavedDisplayed(true)

      setSaving(false)
      return R.Ok(null)
    },
    [
      backend,
      allOpenSaves,
      openSavesState.monsToRelease,
      itemBagState.modified,
      itemBagState.itemCounts,
      displayError,
      ohpkmStore,
      defaultConvertStrategy,
      bagDispatch,
      banks,
      getCurrentBank,
      markBankSaved,
      openSavesState.pendingMonLocations.length,
    ]
  )

  // load bag
  useEffect(() => {
    if (!itemBagState.loaded && !itemBagState.error) {
      backend.loadItemBag().then(
        R.match(
          (bagObj) => {
            bagDispatch({ type: 'load_item_bag', payload: bagObj })
          },
          (err) => {
            bagDispatch({ type: 'set_error', payload: err })
          }
        )
      )
    }
  }, [backend, itemBagState.loaded, itemBagState.error, bagDispatch])

  const hasUnsavedChanges =
    bankModified ||
    itemBagState.modified ||
    openSavesState.monsToRelease.length > 0 ||
    openSavesState.pendingMonLocations.length > 0 ||
    allOpenSaves.some((save) => save.updatedBoxSlots.length > 0)

  const [, refreshDirty] = useReducer((value: number) => value + 1, 0)
  const dirtySignature = useRef('')
  const checkDirty = useEffectEvent(() => {
    const signature = JSON.stringify(allOpenSaves.map((save) => save.updatedBoxSlots))
    if (signature !== dirtySignature.current) {
      dirtySignature.current = signature
      refreshDirty()
    }
  })
  useEffect(() => {
    const timer = setInterval(() => checkDirty(), 500)
    return () => clearInterval(timer)
  }, [])

  async function releaseSaves() {
    for (const save of allOpenSaves)
      if (isRemoteSave(save.filePath.raw)) await liveHub.close(save.filePath.raw)
  }
  async function closeSave(save: SAV) {
    if (isRemoteSave(save.filePath.raw)) await liveHub.close(save.filePath.raw)
    openSavesDispatch({ type: 'remove_save', payload: save })
  }
  function requestCloseSave(save: SAV) {
    if (saveInFlight.current) return
    if (hasUnsavedChanges) setCloseRequest({ kind: 'save', save })
    else void closeSave(save)
  }
  async function requestCloseWindow() {
    if (saveInFlight.current) return
    if (hasUnsavedChanges) setCloseRequest({ kind: 'window' })
    else {
      await releaseSaves()
      await getCurrentWindow().destroy()
    }
  }
  async function saveAllChanges(confirmed = false) {
    try {
      const result = await saveChanges(confirmed)
      if (R.isErr(result) && (confirmed || !openSavesState.monsToRelease.length))
        displayError(
          'Changes were not fully saved',
          result.error.map((error) => JSON.stringify(error))
        )
      return R.isOk(result)
    } catch (error) {
      await backend.rollbackTransaction()
      setSaving(false)
      displayError('Changes were not fully saved', String(error))
      return false
    }
  }
  async function finishClose(save: boolean) {
    const request = closeRequest
    if (!request || saveInFlight.current || openSavesState.pendingMonLocations.length) return
    if (save) {
      if (!(await saveAllChanges(true))) return
      setCloseRequest(undefined)
      if (request.kind === 'window') {
        await releaseSaves()
        await getCurrentWindow().destroy()
      } else if (request.save) await closeSave(request.save)
    } else {
      if (liveHub.hasUnresolvedWrite()) {
        displayError(
          'Delivery still pending',
          'Changes may already be on the hub. Complete Save before closing or discarding.'
        )
        return
      }
      // Discard the entire editing session, including transfers into the bank.
      await backend.emitMenuEvent('discard-edits')
      await releaseSaves()
      setCloseRequest(undefined)
      if (request.kind === 'window') await getCurrentWindow().destroy()
      else window.location.reload()
    }
  }
  const closeWindowEvent = useEffectEvent(requestCloseWindow)
  const saveEvent = useEffectEvent(async () => {
    await saveAllChanges()
  })
  const discardEvent = useEffectEvent(() => {
    if (!saveInFlight.current) setCloseRequest({ kind: 'discard' })
  })
  useEffect(() => {
    const stop = backend.onMenuEvents({ save: () => void saveEvent(), reset: () => discardEvent() })
    let disposed = false
    let unlisten: (() => void) | undefined
    void getCurrentWindow()
      .onCloseRequested((event) => {
        event.preventDefault()
        void closeWindowEvent()
      })
      .then((stop) => {
        if (disposed) stop()
        else unlisten = stop
      })
    return () => {
      disposed = true
      stop()
      unlisten?.()
    }
  }, [backend])

  if (openSavesState.error) {
    return (
      <Callout.Root>
        <Callout.Icon>
          <ErrorIcon />
        </Callout.Icon>
        <Callout.Text>{openSavesState.error}</Callout.Text>
      </Callout.Root>
    )
  }

  function hideReleaseWarning() {
    setReleaseWarningDisplayed(false)
  }

  async function saveChangesReleaseConfirmed() {
    await saveAllChanges(true)
    hideReleaseWarning()
  }

  function dismissSavedMessage() {
    setChangesSavedDisplayed(false)
  }

  return (
    <>
      <SavesContext
        value={{
          openSavesState,
          openSavesDispatch,
          allOpenSaves: Object.values(openSavesState.openSaves)
            .filter((data) => !!data)
            .sort((a, b) => a.index - b.index)
            .map((data) => data.save),
          promptDisambiguation,
          requestCloseSave,
          saveAllChanges: async () => {
            await saveAllChanges()
          },
          requestDiscard: () => setCloseRequest({ kind: 'discard' }),
          hasUnsavedChanges,
          saving,
        }}
      >
        <div />
        <div inert={saving} style={{ height: '100%' }}>
          {children}
        </div>
      </SavesContext>
      <PromptDialog
        title="Unsaved changes"
        open={!!closeRequest}
        onClose={() => setCloseRequest(undefined)}
        description="Save all pending changes to the bank and game saves? Saving a LAN save also sends it to the NDS. Discard undoes this session and closes all open saves. Pokémon marked for release will be permanently deleted if you save."
        actions={[
          { uniqueLabel: 'Cancel', action: () => setCloseRequest(undefined), type: 'cancel' },
          { uniqueLabel: 'Discard changes', action: () => finishClose(false), type: 'destructive' },
          { uniqueLabel: 'Save all changes', action: () => finishClose(true) },
        ]}
      />
      <SaveDisambiguationDialog
        open={Boolean(disambiguationSaveTypes)}
        saveTypes={disambiguationSaveTypes}
        onSelect={(selected) => {
          setDisambiguationSaveTypes(undefined)
          disambiguationResolver.current?.(selected)
          navigate('/home')
        }}
      />
      <PromptDialog
        title={`Release ${openSavesState.monsToRelease.length} Pokémon`}
        open={releaseWarningDisplayed}
        description={`Are you sure you want to release ${openSavesState.monsToRelease.length} Pokémon? This will permanently delete each Pokémon and its associated tracking data. This action cannot be undone.`}
        actions={[
          { uniqueLabel: 'Cancel', action: hideReleaseWarning, type: 'cancel' },
          {
            uniqueLabel: `Release ${openSavesState.monsToRelease.length} Pokémon`,
            action: saveChangesReleaseConfirmed,
            type: 'destructive',
          },
        ]}
      />
      <PromptDialog
        title={saving ? 'Saving Changes...' : 'Changes Saved'}
        open={changesSavedDisplayed || saving}
        onClose={dismissSavedMessage}
        description={
          saving
            ? 'Saving changes...'
            : 'All changes to boxes, save files, Pokédex, and settings have been saved.'
        }
        actions={[{ uniqueLabel: 'Ok', action: dismissSavedMessage }]}
      />
    </>
  )
}

type SaveError =
  | { _SaveErrorType: 'TransactionStart'; message: string }
  | { _SaveErrorType: 'TransactionCommit'; message: string }
  | { _SaveErrorType: 'IdentifierNotTracked'; identifier: OhpkmIdentifier }
  | { _SaveErrorType: 'PkmConversion'; message: string }
  | { _SaveErrorType: 'GenG12Identifier'; mon: OHPKM }
  | { _SaveErrorType: 'GenG345Identifier'; mon: OHPKM }
  | { _SaveErrorType: 'SaveItemBagData'; message: string }
  | { _SaveErrorType: 'BackendSaveError'; message: string }
  | { _SaveErrorType: 'ReloadLookup'; message: string }

const TransactionStart: (message: string) => SaveError = (message: string) => ({
  _SaveErrorType: 'TransactionStart',
  message,
})

const TransactionCommit: (message: string) => SaveError = (message: string) => ({
  _SaveErrorType: 'TransactionCommit',
  message,
})

const SaveItemBagData: (message: string) => SaveError = (message: string) => ({
  _SaveErrorType: 'SaveItemBagData',
  message,
})

const BackendSaveError: (message: string) => SaveError = (message: string) => ({
  _SaveErrorType: 'BackendSaveError',
  message,
})

const PkmConversion: (message: string) => SaveError = (message: string) => ({
  _SaveErrorType: 'PkmConversion',
  message,
})

interface SaveDisambiguationDialogProps {
  open: boolean
  saveTypes?: SAVClass[]
  onSelect?: SaveTypeCallback
}

function SaveDisambiguationDialog({ open, saveTypes, onSelect }: SaveDisambiguationDialogProps) {
  return (
    <Dialog.Container open={open} onOpenChange={(open) => !open && onSelect?.()}>
      <Dialog.Title>Ambiguous Save Type</Dialog.Title>
      <Dialog.Description>Select a save type to proceed:</Dialog.Description>
      <Flex gap="1" mt="1" direction="column">
        {saveTypes?.map((saveType) => (
          <Button
            key={saveType.saveTypeID}
            onClick={() => onSelect?.(saveType)}
            style={{ width: '100%', minHeight: 36, height: 'fit-content' }}
          >
            {saveType.saveTypeName}
          </Button>
        ))}
      </Flex>
      <Dialog.Actions>
        <Dialog.Close>Cancel</Dialog.Close>
      </Dialog.Actions>
    </Dialog.Container>
  )
}
