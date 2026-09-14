<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import ProtocolSelector from 'src/app/components/common/ProtocolSelector.svelte'
  import {
    garageState,
    protocolState,
    dryCfg,
    initializeGarage,
    fetchGarageRaw,
    onGarageRaw,
    doorCommand,
    lightCommand,
    lockCommand,
    syncGarage,
  } from 'src/app/lib/door.svelte.js'
  import { decodeByte, decodeDoorResp } from 'src/app/lib/garage.js'
  import { Button } from '$lib/components/ui/button'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { ScrollArea } from '$lib/components/ui/scroll-area'
  import { Activity, RefreshCw } from 'lucide-svelte'

  let raw = $state({ rx: [], tx: [], rx_total: 0, tx_total: 0 })
  let unsubStatus = $state(null)
  let unsubRaw = $state(null)
  let pollTimer = null

  onMount(() => {
    unsubStatus = initializeGarage()
    unsubRaw = onGarageRaw((data) => {
      raw = data
    })
    return () => {
      if (unsubStatus) unsubStatus()
      if (unsubRaw) unsubRaw()
      if (pollTimer) clearInterval(pollTimer)
    }
  })

  // Raw bus polling only makes sense on a bus protocol. In dry-contact mode
  // the wall bus is idle, so don't pester the backend (or the mock) with
  // get_garage_raw every 2 s.
  $effect(() => {
    if (garageState.protocol === 'drycontact') return
    fetchGarageRaw()
    pollTimer = setInterval(fetchGarageRaw, 2000)
    return () => {
      clearInterval(pollTimer)
      pollTimer = null
    }
  })

  onDestroy(() => {
    if (unsubStatus) unsubStatus()
    if (unsubRaw) unsubRaw()
    if (pollTimer) clearInterval(pollTimer)
  })

  // The opener answers a status query (0x38/0x3A/0x39) by echoing the query
  // byte back followed by the payload, so RX carries full [query, payload]
  // pairs - these are joined by describe() below. A lone payload byte (no
  // query echo in the RX window) means the query echo was missed; its
  // meaning depends on WHICH query we just sent - match the most recent TX
  // query to decode it, otherwise don't guess.
  function decodeLoneReply(byte, t) {
    const tx = raw.tx || []
    let lastQuery = null
    for (let i = tx.length - 1; i >= 0; i--) {
      const q = tx[i]
      if (q.t > t) continue // only queries sent before this RX byte
      if (q.byte === 0x38 || q.byte === 0x39 || q.byte === 0x3a) {
        lastQuery = q.byte
        break
      }
    }
    if (lastQuery === 0x38) return { name: 'door reply', detail: `door: ${decodeDoorResp(byte)}` }
    if (lastQuery === 0x3a)
      return { name: 'other reply', detail: `light/lock payload 0x${byte.toString(16)}` }
    if (lastQuery === 0x39)
      return { name: 'obstruction reply', detail: byte ? 'obstructed' : 'clear' }
    return { name: 'opener reply', detail: `0x${byte.toString(16)}` }
  }

  // Pair 0x38 command bytes with their payload byte for a readable log.
  function describe(list) {
    const rows = []
    for (let i = 0; i < list.length; i++) {
      const { t, byte } = list[i]
      if (byte === 0x38 && i + 1 < list.length && list[i + 1].t - t < 50) {
        rows.push({
          t,
          hex: '0x38',
          name: 'QUERY_DOOR_STATUS',
          detail: `door: ${decodeDoorResp(list[i + 1].byte)}`,
        })
        i++
      } else if (byte === 0x3a && i + 1 < list.length && list[i + 1].t - t < 50) {
        rows.push({
          t,
          hex: '0x3A',
          name: 'QUERY_OTHER_STATUS',
          detail: `light/lock payload 0x${list[i + 1].byte.toString(16)}`,
        })
        i++
      } else if (byte === 0x39 && i + 1 < list.length && list[i + 1].t - t < 50) {
        rows.push({
          t,
          hex: '0x39',
          name: 'OBSTRUCTION',
          detail: list[i + 1].byte ? 'obstructed' : 'clear',
        })
        i++
      } else {
        // Lone byte: not a query command, so it's the reply to a query we
        // primed and flushed. Decode by the most recent TX query.
        const isCmd = byte >= 0x30 && byte <= 0x3a
        const reply = isCmd ? null : decodeLoneReply(byte, t)
        rows.push({
          t,
          hex: `0x${byte.toString(16).toUpperCase().padStart(2, '0')}`,
          name: reply ? reply.name : (decodeByte(byte) ?? '-'),
          detail: reply ? reply.detail : '',
        })
      }
    }
    return rows
  }

  const rxRows = $derived(describe(raw.rx || []))
  const txRows = $derived(describe(raw.tx || []))

  const panelLabel = {
    waiting: 'Detecting wall panel…',
    detected: 'Wall panel detected',
    emulated: 'Emulating wall panel',
    none: 'Relay mode (no wall bus)',
  }

  const subtitle = $derived(
    garageState.protocol === 'drycontact'
      ? 'Relay pulses and reed limit sensors — no wall-bus traffic in this mode.'
      : 'Raw Security+ 1.0 bus traffic - use this to confirm wiring and decode.',
  )
  const busNote = $derived(
    garageState.protocol === 'drycontact'
      ? 'Relay mode: the wall bus is idle. Door position comes from the limit sensors below.'
      : '1200 baud, 8E1, half duplex. Panel replies: 0x38 door, 0x3A light/lock, 0x39 obstruction.',
  )
  const isBus = $derived(garageState.protocol !== 'drycontact')
</script>

<SectionHeader title="Protocol" {subtitle} />

<div class="space-y-6">
  <ProtocolSelector />

  {#if protocolState.loaded}
    <Card.Root>
      <Card.Header class="pb-3">
        <Card.Title class="text-lg flex items-center gap-2">
          <Activity class="size-4" />
          Bus overview
        </Card.Title>
      </Card.Header>
      <Card.Content class="flex flex-wrap items-center gap-3">
        <Badge variant="secondary">{panelLabel[garageState.panel]}</Badge>
        {#if isBus}
          <Badge variant="outline">RX bytes: {raw.rx_total}</Badge>
          <Badge variant="outline">TX bytes: {raw.tx_total}</Badge>
        {/if}
        <span class="text-xs text-muted-foreground">
          {busNote}
        </span>
      </Card.Content>
    </Card.Root>

    {#if isBus}
      <div class="grid gap-6 lg:grid-cols-2">
        <Card.Root>
          <Card.Header class="pb-2">
            <Card.Description>Received (RX)</Card.Description>
          </Card.Header>
          <Card.Content>
            <ScrollArea class="h-72 rounded-md border">
              <table class="w-full text-sm">
                <thead class="sticky top-0 bg-muted/95 backdrop-blur">
                  <tr class="text-left">
                    <th class="p-2 font-medium">t (ms)</th>
                    <th class="p-2 font-medium">Byte</th>
                    <th class="p-2 font-medium">Meaning</th>
                  </tr>
                </thead>
                <tbody>
                  {#each rxRows as row}
                    <tr class="border-t">
                      <td class="p-2 tabular-nums text-muted-foreground">{row.t}</td>
                      <td class="p-2 font-mono">{row.hex}</td>
                      <td class="p-2">
                        {row.name}
                        {#if row.detail}
                          <span class="text-muted-foreground"> - {row.detail}</span>
                        {/if}
                      </td>
                    </tr>
                  {:else}
                    <tr><td class="p-4 text-muted-foreground" colspan="3">No traffic yet…</td></tr>
                  {/each}
                </tbody>
              </table>
            </ScrollArea>
          </Card.Content>
        </Card.Root>

        <Card.Root>
          <Card.Header class="pb-2">
            <Card.Description>Transmitted (TX)</Card.Description>
          </Card.Header>
          <Card.Content>
            <ScrollArea class="h-72 rounded-md border">
              <table class="w-full text-sm">
                <thead class="sticky top-0 bg-muted/95 backdrop-blur">
                  <tr class="text-left">
                    <th class="p-2 font-medium">t (ms)</th>
                    <th class="p-2 font-medium">Byte</th>
                    <th class="p-2 font-medium">Meaning</th>
                  </tr>
                </thead>
                <tbody>
                  {#each txRows as row}
                    <tr class="border-t">
                      <td class="p-2 tabular-nums text-muted-foreground">{row.t}</td>
                      <td class="p-2 font-mono">{row.hex}</td>
                      <td class="p-2">{row.name}</td>
                    </tr>
                  {/each}
                  {#if txRows.length === 0}
                    <tr><td class="p-4 text-muted-foreground" colspan="3">Nothing sent yet…</td></tr
                    >
                  {/if}
                </tbody>
              </table>
            </ScrollArea>
          </Card.Content>
        </Card.Root>
      </div>
    {:else}
      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg">Limit sensors</Card.Title>
          <Card.Description
            >Live reed-switch state — this is the dry-contact position feedback.</Card.Description
          >
        </Card.Header>
        <Card.Content class="flex flex-wrap items-center gap-3">
          {#if garageState.sensors.valid}
            {#if dryCfg.sensor_mode === 2}
              <Badge variant={garageState.sensors.open ? 'default' : 'secondary'}>
                Open reed: {garageState.sensors.open ? 'hit' : 'clear'}
              </Badge>
            {/if}
            <Badge variant={garageState.sensors.close ? 'default' : 'secondary'}>
              Close reed: {garageState.sensors.close ? 'hit' : 'clear'}
            </Badge>
          {:else}
            <Badge variant="secondary">No sensors — relay-only mode</Badge>
          {/if}
          <span class="text-xs text-muted-foreground">
            Trigger the door and watch the reeds flip as it reaches each end.
          </span>
        </Card.Content>
      </Card.Root>
    {/if}

    <Card.Root>
      <Card.Header class="pb-3">
        <Card.Title class="text-lg">Fire commands</Card.Title>
        <Card.Description>
          {isBus
            ? 'Send raw actions and watch them show up in the logs above.'
            : 'Pulse the relay and watch the reeds above flip at each end of travel.'}
        </Card.Description>
      </Card.Header>
      <Card.Content class="space-y-4">
        <div class="flex flex-wrap items-center gap-3">
          <span class="w-14 text-sm text-muted-foreground">Door</span>
          <Button size="sm" variant="outline" onclick={() => doorCommand('open')}>Open</Button>
          <Button size="sm" variant="outline" onclick={() => doorCommand('close')}>Close</Button>
          <Button size="sm" variant="outline" onclick={() => doorCommand('stop')}>Stop</Button>
          <Button size="sm" variant="outline" onclick={() => doorCommand('toggle')}>Toggle</Button>
        </div>
        {#if garageState.caps.light}
          <div class="flex flex-wrap items-center gap-3">
            <span class="w-14 text-sm text-muted-foreground">Light</span>
            <Button size="sm" variant="outline" onclick={() => lightCommand('on')}>On</Button>
            <Button size="sm" variant="outline" onclick={() => lightCommand('off')}>Off</Button>
            <Button size="sm" variant="outline" onclick={() => lightCommand('toggle')}
              >Toggle</Button
            >
          </div>
        {/if}
        {#if garageState.caps.lock}
          <div class="flex flex-wrap items-center gap-3">
            <span class="w-14 text-sm text-muted-foreground">Lock</span>
            <Button size="sm" variant="outline" onclick={() => lockCommand('lock')}>Lock</Button>
            <Button size="sm" variant="outline" onclick={() => lockCommand('unlock')}>Unlock</Button
            >
            <Button size="sm" variant="outline" onclick={() => lockCommand('toggle')}>Toggle</Button
            >
          </div>
        {/if}
        <div class="flex flex-wrap items-center gap-3 border-t pt-4">
          <span class="w-14 text-sm text-muted-foreground">{isBus ? 'Bus' : 'Relay'}</span>
          <Button size="sm" variant="ghost" onclick={() => syncGarage()}>
            <RefreshCw class="size-4" />
            Query status (sync)
          </Button>
          {#if isBus}
            <Button size="sm" variant="ghost" onclick={() => fetchGarageRaw()}>Refresh logs</Button>
          {/if}
        </div>
      </Card.Content>
    </Card.Root>
  {/if}
</div>
