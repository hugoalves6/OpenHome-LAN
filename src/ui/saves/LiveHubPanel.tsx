import { liveHub, remotePathData } from '@openhome-core/lan/liveHub'
import { R } from '@openhome-core/util/functional'
import { useSaves } from '@openhome-ui/state/saves'
import { Button, Card, Flex, Text, TextField } from '@radix-ui/themes'
import { useState, useSyncExternalStore } from 'react'

export default function LiveHubPanel() {
  useSyncExternalStore(liveHub.subscribe, liveHub.snapshot)
  const saves = useSaves()
  const [base, setBase] = useState(() => localStorage.getItem('openhome-lan-hub') || liveHub.base)
  const [password, setPassword] = useState('')
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
        <TextField.Root
          aria-label="Hub address"
          value={base}
          onChange={(event) => setBase(event.target.value)}
        />
        <TextField.Root
          aria-label="Hub password"
          type="password"
          value={password}
          onChange={(event) => setPassword(event.target.value)}
          placeholder="Hub password"
        />
        <Button
          disabled={busy}
          onClick={() =>
            void action(async () => {
              await liveHub.connect(base, password)
              localStorage.setItem('openhome-lan-hub', base)
              setPassword('')
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
          Remote edits save automatically. Wait for card confirmation before leaving the DSi app.
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
