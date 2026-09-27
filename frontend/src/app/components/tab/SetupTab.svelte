<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- USB setup over WebSerial (Improv protocol): connect a device by cable,
  read its identity/version/network state, join Wi-Fi from a scan or a
  manual SSID, and manage the OTA password. Demo builds only (dev server +
  GitHub Pages) — never shipped on the device itself. -->
<script>
  import { onDestroy } from 'svelte'
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import WifiNetworkList from 'src/core/components/wifi/WifiNetworkList.svelte'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import { getSignalStrength } from 'src/core/lib/wifi.js'
  import { ImprovSession, requestDevicePort, STATE } from 'src/app/lib/serialImprov.js'
  import { toast } from 'svelte-sonner'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Button } from '$lib/components/ui/button'
  import { Input } from '$lib/components/ui/input'
  import { RefreshCw, PlugZap, Wifi, KeyRound, Search } from 'lucide-svelte'

  const supported = typeof navigator !== 'undefined' && 'serial' in navigator

  let session = $state(null)
  let portLabel = $state('')
  let info = $state(null) // [firmware, version, chip, name]
  let ota = $state({ value: null, revealed: false })
  let net = $state(null) // {flags, urls}
  let networks = $state([])
  let scanned = $state(false)
  let ssid = $state('')
  let wifiPassword = $state('')
  let otaNew = $state('')
  let busy = $state(false)

  const online = $derived(net !== null && (net.flags & 1) === 1 && net.urls.length > 0)

  async function run(fn) {
    if (!session || busy) return
    busy = true
    try {
      await fn()
    } catch (e) {
      toast.error(e.message || String(e))
    } finally {
      busy = false
    }
  }

  async function connect() {
    busy = true
    try {
      const port = await requestDevicePort()
      portLabel = `USB${port.getInfo ? ` (${port.getInfo().usbVendorId || '?'}:${port.getInfo().usbProductId || '?'})` : ''}`
      session = new ImprovSession(port)
      await fetchInfos()
    } catch (e) {
      toast.error(e.message || String(e))
      session = null
    } finally {
      busy = false
    }
  }

  async function disconnect() {
    const s = session
    session = null
    info = null
    ota = { value: null, revealed: false }
    net = null
    networks = []
    scanned = false
    if (s) {
      try {
        await s.close()
      } catch {
        // already gone
      }
    }
  }

  onDestroy(() => {
    if (session) {
      const s = session
      session = null
      s.close().catch(() => {})
    }
  })

  async function fetchInfos() {
    info = await session.getInfo()
    const pw = await session.getOtaPassword()
    ota = { value: pw, revealed: false }
    net = await session.getNetworkState()
  }

  async function refresh() {
    await run(fetchInfos)
  }

  async function scan() {
    await run(async () => {
      const found = await session.scanNetworks()
      // Strongest first, one row per name (repeaters share SSIDs).
      const best = new Map()
      for (const n of found) {
        if (!n.ssid) continue
        const prev = best.get(n.ssid)
        if (!prev || n.rssi > prev.rssi) best.set(n.ssid, n)
      }
      networks = [...best.values()].sort((a, b) => b.rssi - a.rssi)
      scanned = true
      if (networks.length === 0) {
        toast.info('No networks found — enter the SSID manually below.')
      }
    })
  }

  async function join() {
    const name = (ssid || '').trim()
    if (!name) {
      toast.error('Pick a network or type an SSID first.')
      return
    }
    await run(async () => {
      const res = await session.provisionWifi(name, wifiPassword)
      wifiPassword = ''
      net = await session.getNetworkState()
      if (res.url) {
        toast.success(`Joined — web interface at ${res.url}`)
      } else {
        toast.success('Joined (device reported no URL).')
      }
    })
  }

  async function setOta() {
    await run(async () => {
      await session.setOtaPassword(otaNew)
      otaNew = ''
      const pw = await session.getOtaPassword()
      ota = { value: pw, revealed: false }
      toast.success('OTA password updated.')
    })
  }
</script>

<SectionHeader title="USB Setup" subtitle="Provision a device over USB serial." />

{#if !supported}
  <Card.Root>
    <Card.Content class="pt-6">
      <p class="text-sm text-muted-foreground">
        WebSerial is not available here — it needs a Chromium-based browser over HTTPS or localhost.
      </p>
    </Card.Content>
  </Card.Root>
{:else}
  <div class="space-y-6">
    <Card.Root>
      <Card.Header class="pb-3">
        <Card.Title class="text-lg flex items-center gap-2">
          <PlugZap class="size-4" />
          Device connection
        </Card.Title>
        <Card.Description>
          {#if session}
            Connected{portLabel} — unplug or use Disconnect to release the port.
          {:else}
            Plug the device in over USB, then connect.
          {/if}
        </Card.Description>
      </Card.Header>
      <Card.Content class="flex flex-wrap items-center gap-3">
        {#if session}
          <Badge variant="success">CONNECTED</Badge>
          <Button size="sm" variant="outline" onclick={refresh} disabled={busy}
            >Refresh infos</Button
          >
          <Button size="sm" variant="ghost" onclick={disconnect}>Disconnect</Button>
        {:else}
          <Button size="sm" onclick={connect} disabled={busy}>Connect device</Button>
        {/if}
      </Card.Content>
    </Card.Root>

    {#if session}
      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg">Device infos</Card.Title>
          <Card.Description
            >Firmware, version, network state and OTA password, read live.</Card.Description
          >
        </Card.Header>
        <Card.Content>
          {#if info}
            <table class="w-full text-sm">
              <tbody>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">Firmware</td>
                  <td class="p-2 font-mono">{info[0] ?? '—'}</td>
                </tr>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">Version</td>
                  <td class="p-2 font-mono">{info[1] ?? '—'}</td>
                </tr>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">Chip</td>
                  <td class="p-2 font-mono">{info[2] ?? '—'}</td>
                </tr>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">Name</td>
                  <td class="p-2 font-mono">{info[3] ?? '—'}</td>
                </tr>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">WiFi</td>
                  <td class="p-2">
                    {#if net}
                      <Badge variant={online ? 'success' : 'secondary'}>
                        {online ? 'ONLINE' : 'OFFLINE'}
                      </Badge>
                      {#if online}
                        <a
                          href={net.urls[0]}
                          target="_blank"
                          rel="noopener"
                          class="ml-2 font-mono text-primary underline-offset-4 hover:underline"
                        >
                          {net.urls[0]}
                        </a>
                      {/if}
                    {:else}
                      <span class="text-muted-foreground">—</span>
                    {/if}
                  </td>
                </tr>
                <tr class="border-t">
                  <td class="p-2 text-muted-foreground">OTA password</td>
                  <td class="p-2 font-mono">
                    {#if ota.value === null}
                      <span class="text-muted-foreground">unreadable (old firmware?)</span>
                    {:else if ota.value === ''}
                      <span class="text-muted-foreground">none — uploads are open</span>
                    {:else if ota.revealed}
                      {ota.value}
                      <Button size="sm" variant="ghost" onclick={() => (ota.revealed = false)}
                        >Hide</Button
                      >
                    {:else}
                      {'•'.repeat(Math.min(ota.value.length, 16))}
                      <Button size="sm" variant="ghost" onclick={() => (ota.revealed = true)}
                        >Show</Button
                      >
                    {/if}
                  </td>
                </tr>
              </tbody>
            </table>
          {:else}
            <p class="text-sm text-muted-foreground">Reading…</p>
          {/if}
        </Card.Content>
      </Card.Root>

      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg flex items-center gap-2">
            <Wifi class="size-4" />
            WiFi setup
          </Card.Title>
          <Card.Description
            >Scan for networks or type the SSID manually, then join.</Card.Description
          >
        </Card.Header>
        <Card.Content class="space-y-4">
          <div class="flex flex-wrap gap-2">
            <Button size="sm" variant="outline" onclick={scan} disabled={busy}>
              <RefreshCw class="size-4" />
              Scan networks
            </Button>
          </div>
          {#if scanned}
            {#if networks.length > 0}
              <WifiNetworkList
                networks={networks.map((n) => ({ ssid: n.ssid, rssi: n.rssi, secure: n.auth }))}
                bind:selectedNetwork={ssid}
                {getSignalStrength}
              />
            {:else}
              <div
                class="flex flex-col items-center justify-center py-8 text-center border-2 border-dashed rounded-lg"
              >
                <Search class="h-10 w-10 text-muted-foreground mb-3" />
                <h3 class="text-lg font-semibold mb-2">No Networks Found</h3>
                <p class="text-muted-foreground mb-4 text-sm">
                  No WiFi networks are currently available. Type the SSID manually below.
                </p>
              </div>
            {/if}
          {/if}
          <div class="space-y-2">
            <label class="text-sm text-muted-foreground" for="setup-ssid">SSID</label>
            <Input
              id="setup-ssid"
              bind:value={ssid}
              placeholder="Network name (or pick above)"
              disabled={busy}
            />
          </div>
          <div class="space-y-2">
            <label class="text-sm text-muted-foreground" for="setup-wifi-password">Password</label>
            <Input
              id="setup-wifi-password"
              type="password"
              bind:value={wifiPassword}
              placeholder="Leave empty for open networks"
              disabled={busy}
            />
          </div>
          <LoadingButton
            class="w-full"
            onclick={join}
            disabled={busy || !ssid.trim()}
            loading={busy}
            loadingLabel="Connecting..."
            icon={Wifi}
          >
            Connect
          </LoadingButton>
        </Card.Content>
      </Card.Root>

      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg flex items-center gap-2">
            <KeyRound class="size-4" />
            OTA password
          </Card.Title>
          <Card.Description>Required for wireless uploads. Stored on the device.</Card.Description>
        </Card.Header>
        <Card.Content class="space-y-4">
          <div class="space-y-2">
            <label class="text-sm text-muted-foreground" for="setup-ota-password"
              >New password</label
            >
            <Input
              id="setup-ota-password"
              type="password"
              bind:value={otaNew}
              placeholder="Empty clears it (uploads open)"
              disabled={busy}
            />
          </div>
          <LoadingButton
            class="w-full"
            variant="outline"
            onclick={setOta}
            disabled={busy}
            loading={busy}
            loadingLabel="Saving..."
            icon={KeyRound}
          >
            Set password
          </LoadingButton>
        </Card.Content>
      </Card.Root>
    {/if}
  </div>
{/if}
