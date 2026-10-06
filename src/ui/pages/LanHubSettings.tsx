import { R } from '@openhome-core/util/functional'
import ContentCard from '@openhome-ui/components/ContentCard'
import useSettings from '@openhome-ui/hooks/settings'
import { Button, Flex, Text, TextField } from '@radix-ui/themes'
import { useState } from 'react'

export default function LanHubSettings() {
  const { settingsLoaded } = useSettings()
  return settingsLoaded ? <LanHubSettingsForm /> : <ContentCard>Loading settings?</ContentCard>
}

function LanHubSettingsForm() {
  const { settings, updateSettings } = useSettings()
  const [address, setAddress] = useState(settings.lanHub.address)
  const [password, setPassword] = useState(settings.lanHub.password)
  const [message, setMessage] = useState('')
  const [busy, setBusy] = useState(false)

  async function save() {
    setBusy(true)
    setMessage('')
    try {
      const url = new URL(address.trim())
      if (
        url.protocol !== 'http:' ||
        !/^192\.168\.1\.\d{1,3}$/.test(url.hostname) ||
        Number(url.hostname.split('.').at(-1)) > 255 ||
        url.username ||
        url.password ||
        url.pathname !== '/' ||
        url.search ||
        url.hash
      ) {
        throw new Error('Enter the LAN hub address, for example http://192.168.1.125:8321')
      }
      const normalized = url.origin
      const result = await updateSettings({ lanHub: { address: normalized, password } })
      if (R.isErr(result)) throw new Error('Could not save settings. Please try again.')
      setAddress(normalized)
      localStorage.removeItem('openhome-lan-hub')
      setMessage('Saved. Use Connect to hub on Home to connect with these settings.')
    } catch (error) {
      setMessage(error instanceof Error ? error.message : 'Could not save settings.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <ContentCard>
      <Flex direction="column" gap="4" style={{ maxWidth: 520 }}>
        <Text weight="bold" size="5">
          LAN Hub
        </Text>
        <Text as="label">
          Server IP and port
          <TextField.Root
            aria-label="Hub address"
            value={address}
            placeholder="http://192.168.1.125:8321"
            disabled={busy}
            onChange={(event) => setAddress(event.target.value)}
          />
        </Text>
        <Text as="label">
          Hub password
          <TextField.Root
            aria-label="Hub password"
            type="password"
            value={password}
            disabled={busy}
            onChange={(event) => setPassword(event.target.value)}
          />
        </Text>
        <Text size="2">
          The address and password are saved locally with your other app settings.
        </Text>
        <Button disabled={busy} onClick={() => void save()}>
          {busy ? 'Saving…' : 'Save LAN settings'}
        </Button>
        {message && (
          <Text role="status" size="2">
            {message}
          </Text>
        )}
      </Flex>
    </ContentCard>
  )
}
