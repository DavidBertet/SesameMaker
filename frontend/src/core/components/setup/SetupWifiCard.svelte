<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Wi-Fi provisioning over a USB Improv session (core, generic). Owns the
  form state; the network snapshot is lifted via onNetChange. Auto-scans on
  mount when the device is offline. -->
<script>
  import { onMount } from 'svelte'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import WifiNetworkList from 'src/core/components/wifi/WifiNetworkList.svelte'
  import { getSignalStrength } from 'src/core/lib/wifi.js'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Button } from '$lib/components/ui/button'
  import { Input } from '$lib/components/ui/input'
  import { Wifi, RefreshCw, Search } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  let { session, net = null, onNetChange = () => {} } = $props()

  let networks = $state([])
  let scanned = $state(false)
  let ssid = $state('')
  let wifiPassword = $state('')
  let busy = $state(false)
  let scanning = $state(false)
  // Collapsed when online: "Change WiFi" expands the form + scans.
  let wifiEditing = $state(false)

  const online = $derived(net !== null && (net.flags & 1) === 1 && net.urls.length > 0)
  const showWifiForm = $derived(!online || wifiEditing)

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

  async function scan() {
    if (!session || busy || scanning) return
    scanning = true
    try {
      await run(async () => {
        // One retry: a scan colliding with an STA reconnect attempt (or a
        // log-interleaved frame) comes back empty — the next one lands.
        let found = []
        for (let attempt = 0; attempt < 2 && found.length === 0; attempt++) {
          if (attempt > 0) {
            await new Promise((r) => setTimeout(r, 2000))
          }
          try {
            found = await session.scanNetworks()
          } catch {
            found = []
          }
        }
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
    } finally {
      scanning = false
    }
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
      onNetChange(await session.getNetworkState())
      wifiEditing = false
      if (res.url) {
        toast.success(`Joined — web interface at ${res.url}`)
      } else {
        toast.success('Joined (device reported no URL).')
      }
    })
  }

  function changeWifi() {
    wifiEditing = true
    scan()
  }

  function cancelWifiEdit() {
    wifiEditing = false
  }

  onMount(() => {
    // Offline: show the form with networks right away (scan runs itself).
    if (!online) scan()
  })
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <Wifi class="size-4" />
      WiFi setup
    </Card.Title>
    <Card.Description>
      {#if showWifiForm}
        Scan for networks or type the SSID manually, then join.
      {:else}
        Only change this to switch networks.
      {/if}
    </Card.Description>
  </Card.Header>
  <Card.Content class="space-y-4">
    {#if !showWifiForm}
      <div class="flex flex-wrap items-center gap-3">
        <Badge variant="success">CONNECTED</Badge>
        {#if net?.urls?.length}
          <a
            href={net.urls[0]}
            target="_blank"
            rel="noopener"
            class="font-mono text-sm text-primary underline-offset-4 hover:underline"
          >
            {net.urls[0]}
          </a>
        {/if}
        <Button size="sm" variant="outline" onclick={changeWifi} disabled={busy}>
          Change WiFi
        </Button>
      </div>
    {:else}
      <div class="flex flex-wrap gap-2">
        <LoadingButton
          size="sm"
          variant="outline"
          onclick={scan}
          disabled={busy || scanning}
          loading={scanning}
          loadingLabel="Scanning..."
          icon={RefreshCw}
        >
          Scan networks
        </LoadingButton>
        {#if online}
          <Button size="sm" variant="ghost" onclick={cancelWifiEdit} disabled={busy}>Cancel</Button>
        {/if}
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
    {/if}
  </Card.Content>
</Card.Root>
