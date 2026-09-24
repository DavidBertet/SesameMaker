<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import DataTable from 'src/core/components/common/DataTable.svelte'
  import { garageState, protocolState, dryCfg, initializeGarage } from 'src/app/lib/door.svelte.js'
  import {
    pinTypeLabel,
    pinLevelText,
    pinMeaning,
    busStatusMeta,
    busMeaning,
    doorSummary,
  } from 'src/app/lib/inspector.js'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Button } from '$lib/components/ui/button'
  import { RefreshCw } from 'lucide-svelte'

  let pins = $state([])
  let buses = $state([])
  let lastUpdate = $state(null)
  let unsubs = $state([])
  let pollTimer = null
  let heartbeatTimer = null

  function fetchGpio() {
    sendMessage({ type: 'get_gpio_state' })
  }

  // Bus capture is on-demand: start it while this tab is open (heartbeat
  // refreshes the 60 s server deadline), stop it on close.
  function captureStart() {
    sendMessage({ type: 'bus_capture_start' })
  }

  onMount(() => {
    const uInit = initializeGarage()
    const uGpio = onMessageType('gpio_state', (data) => {
      pins = data.pins || []
      buses = data.buses || []
      lastUpdate = new Date()
    })
    unsubs = [uInit, uGpio]
    captureStart()
    fetchGpio()
    pollTimer = setInterval(fetchGpio, 2000)
    heartbeatTimer = setInterval(captureStart, 30000)
    return () => {
      unsubs.forEach((u) => u())
      if (pollTimer) clearInterval(pollTimer)
      if (heartbeatTimer) clearInterval(heartbeatTimer)
    }
  })

  onDestroy(() => {
    sendMessage({ type: 'bus_capture_stop' })
    unsubs.forEach((u) => u())
    if (pollTimer) clearInterval(pollTimer)
    if (heartbeatTimer) clearInterval(heartbeatTimer)
  })

  function isHigh(pin) {
    return pin.type !== 'analog' && !!pin.level
  }

  const stateRows = $derived(doorSummary(garageState, protocolState, dryCfg))
</script>

<SectionHeader
  title="Pin Inspector"
  subtitle="Live pin levels plus on-demand bus framing checks — valid patterns, not payload content."
/>

<div class="space-y-6">
  <Card.Root>
    <Card.Header class="pb-3">
      <div class="flex items-center justify-between">
        <div class="space-y-1">
          <Card.Title class="text-lg">Pin levels</Card.Title>
          <Card.Description>
            {#if protocolState.loaded}
              Protocol: {protocolState.id} ·
            {/if}
            {#if lastUpdate}
              updated {lastUpdate.toLocaleTimeString()} · polls every 2 s
            {:else}
              waiting for first sample…
            {/if}
          </Card.Description>
        </div>
        <Button size="sm" variant="outline" onclick={fetchGpio}>
          <RefreshCw class="size-4" />
          Refresh
        </Button>
      </div>
    </Card.Header>
    <Card.Content>
      <DataTable
        headers={['GPIO', 'Name', 'Role', 'Type', 'Mode', 'Level', 'Meaning']}
        rows={pins}
        empty="No samples yet…"
        rowKey={(pin) => pin.gpio}
      >
        {#snippet children(pin)}
          <tr class="border-t">
            <td class="p-2 font-mono">GPIO{pin.gpio}</td>
            <td class="p-2 font-mono">{pin.name}</td>
            <td class="p-2 text-muted-foreground">{pin.role}</td>
            <td class="p-2">
              <Badge variant="outline">{pinTypeLabel(pin.type)}</Badge>
            </td>
            <td class="p-2 text-muted-foreground">{pin.mode}</td>
            <td class="p-2">
              {#if pin.type === 'analog'}
                <span class="text-muted-foreground">—</span>
              {:else}
                <Badge variant={isHigh(pin) ? 'default' : 'secondary'}>
                  {pinLevelText(pin)}
                </Badge>
              {/if}
            </td>
            <td class="p-2 text-muted-foreground">{pinMeaning(pin)}</td>
          </tr>
        {/snippet}
      </DataTable>
    </Card.Content>
  </Card.Root>

  <Card.Root>
    <Card.Header class="pb-3">
      <Card.Title class="text-lg">Bus validation</Card.Title>
      <Card.Description>
        Framing checks over on-demand edge capture — valid patterns, not payload content.
      </Card.Description>
    </Card.Header>
    <Card.Content>
      <DataTable
        headers={['Bus', 'Type', 'Status', 'Summary']}
        rows={buses}
        empty="No capture running — monitoring starts while this tab is open…"
        rowKey={(bus) => bus.name}
      >
        {#snippet children(bus)}
          {@const meta = busStatusMeta(bus.status)}
          <tr class="border-t">
            <td class="p-2 font-mono">{bus.name}</td>
            <td class="p-2">
              <Badge variant="outline">{pinTypeLabel(bus.type)}</Badge>
            </td>
            <td class="p-2">
              <Badge variant={meta.variant}>{meta.label}</Badge>
            </td>
            <td class="p-2 text-muted-foreground">{busMeaning(bus)}</td>
          </tr>
        {/snippet}
      </DataTable>
    </Card.Content>
  </Card.Root>

  <Card.Root>
    <Card.Header class="pb-3">
      <Card.Title class="text-lg">All values</Card.Title>
      <Card.Description>Live door/controller state from the ESP32.</Card.Description>
    </Card.Header>
    <Card.Content>
      <DataTable headers={['Name', 'Value']} rows={stateRows} empty="No state yet…">
        {#snippet children([name, value])}
          <tr class="border-t">
            <td class="p-2 text-muted-foreground">{name}</td>
            <td class="p-2 font-mono">{value}</td>
          </tr>
        {/snippet}
      </DataTable>
    </Card.Content>
  </Card.Root>
</div>
