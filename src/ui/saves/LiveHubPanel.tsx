import { liveHub, remotePathData } from '@openhome-core/lan/liveHub'
import { R } from '@openhome-core/util/functional'
import useSettings from '@openhome-ui/hooks/settings'
import { Link } from 'react-router'
import { useSaves } from '@openhome-ui/state/saves'
import { Button, Card, Flex, Text } from '@radix-ui/themes'
import { useState, useSyncExternalStore } from 'react'

export default function LiveHubPanel() {
  useSyncExternalStore(liveHub.subscribe, liveHub.snapshot)
  const saves = useSaves()
  const { settings } = useSettings()
  const [error, setError] = useState('')
  const [busy, setBusy] = useState(false)
  const action = async (callback: () => Promise<void>) => {
    setBusy(true)
    setError('')
    try {
      await callback()
    } catch (error) {
      setError(String(error))
    } finally {
      setBusy(false)
    }
  }
  return (
    <Card>
      <Flex direction="column" gap="2">
        <Text weight="bold">LAN console saves</Text>
        <Link to="/settings/lan">LAN hub settings</Link>
        <Button
          disabled={busy || !settings.lanHub.password}
          onClick={() =>
            void action(async () => {
              await liveHub.connect(settings.lanHub.address, settings.lanHub.password)
            })
          }
        >
          Connect to hub
        </Button>
        <Text size="1">{liveHub.message}</Text>
        {liveHub.saves.map((remote) => {
          const path = remotePathData(remote.path)
          const open = saves.allOpenSaves.some((save) => save.filePath.raw === path.raw)
          return (
            <Flex key={remote.path} direction="column" gap="1">
              <Text size="1">{path.name}</Text>
              <Button
                disabled={
                  busy || open || !remote.online || !remote.synced || remote.pending || remote.busy
                }
                onClick={() =>
                  void action(async () => {
                    const result = await saves.buildAndOpenSave(path)
                    if (R.isErr(result)) {
                      await liveHub.close(path.raw)
                      throw new Error(JSON.stringify(result.error))
                    }
                    if (!result.data) await liveHub.close(path.raw)
                  })
                }
              >
                {open
                  ? 'Open in editor'
                  : !remote.online
                    ? 'DSi offline — locked'
                    : remote.pending
                      ? 'Delivering to card…'
                      : !remote.synced
                        ? 'Waiting for sync…'
                        : remote.busy
                          ? 'Already being edited'
                          : 'Open console save'}
              </Button>
            </Flex>
          )
        })}
        <Text size="1">
          Edits stay pending until you choose Save all changes. Keep the DSi app open until card
          delivery is confirmed.
        </Text>
        {error && (
          <Text color="red" size="1">
            {error}
          </Text>
        )}
      </Flex>
    </Card>
  )
}
