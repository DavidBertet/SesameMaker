<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import {
    garageState,
    initializeGarage,
    doorCommand,
    lightCommand,
    lockCommand,
    syncGarage,
    pendingCommands,
  } from 'src/app/lib/door.svelte.js'
  import { doorMeta, isSettled } from 'src/app/lib/garage.js'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import { Button } from '$lib/components/ui/button'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Label } from '$lib/components/ui/label'
  import {
    DoorOpen,
    DoorClosed,
    Lightbulb,
    Lock,
    LockOpen,
    TriangleAlert,
    RefreshCw,
    Square,
  } from 'lucide-svelte'

  let unsub = $state(null)
  let leftOpenUnsubs = $state([])
  let leftOpen = $state({ enabled: false, warn_s: 0, close_s: 0 })

  onMount(() => {
    unsub = initializeGarage()
    const u1 = onMessageType('left_open_config', (data) => {
      leftOpen.enabled = !!data.enabled
      leftOpen.warn_s = data.warn_s || 0
      leftOpen.close_s = data.close_s || 0
    })
    const u2 = onMessageType('door_event', () => sendMessage({ type: 'get_left_open' }))
    leftOpenUnsubs = [u1, u2]
    sendMessage({ type: 'get_left_open' })
    return () => {
      if (unsub) unsub()
      leftOpenUnsubs.forEach((u) => u())
    }
  })

  onDestroy(() => {
    if (unsub) unsub()
    leftOpenUnsubs.forEach((u) => u())
  })

  const meta = $derived(doorMeta(garageState.door))
  const moving = $derived(garageState.moving || !isSettled(garageState.door))
  const statusDetail = $derived(
    garageState.door === 'unknown'
      ? pendingCommands.sync
        ? 'Syncing…'
        : 'Waiting for door status…'
      : meta.detail,
  )

  const primaryAction = $derived.by(() => {
    if (moving) return { label: 'Stop', action: 'stop', icon: Square }
    if (garageState.door === 'open') return { label: 'Close', action: 'close', icon: DoorClosed }
    return { label: 'Open', action: 'open', icon: DoorOpen }
  })

  const badgeColor = {
    green: 'bg-green-600 text-white border-transparent',
    amber: 'bg-amber-500 text-white border-transparent',
    red: 'bg-red-600 text-white border-transparent',
    gray: 'bg-stone-500 text-white border-transparent',
  }

  const panelLabel = {
    waiting: 'Detecting wall panel…',
    detected: 'Wall panel detected',
    emulated: 'Emulating wall panel',
    none: 'No wall panel (relay mode)',
  }

  const protocolLabel = {
    secplus1: 'Security+ 1.0',
    drycontact: 'Dry contact',
    secplus2: 'Security+ 2.0',
  }
</script>

<SectionHeader title="Garage Door" subtitle="Open, close and watch your door." />

<div class="space-y-6">
  {#if garageState.caps.obstruction && garageState.obstruction}
    <div
      class="flex items-center gap-3 rounded-lg border border-red-300 bg-red-50 dark:border-red-900 dark:bg-red-950 p-4"
    >
      <TriangleAlert class="size-5 text-red-600 dark:text-red-400" />
      <div>
        <p class="font-semibold text-red-700 dark:text-red-300">Obstruction detected</p>
        <p class="text-sm text-red-600/80 dark:text-red-400/80">
          Something is blocking the photo-eye beam or the door hit an obstacle.
        </p>
      </div>
    </div>
  {/if}

  <Card.Root class="gap-1 py-4">
    <Card.Header class="flex items-center justify-between">
      <Card.Description>Door status</Card.Description>
      <LoadingButton
        size="sm"
        variant="ghost"
        loading={pendingCommands.sync}
        icon={RefreshCw}
        onclick={() => syncGarage()}
      >
        Sync
      </LoadingButton>
    </Card.Header>
    <Card.Content>
      {#if garageState.door !== 'unknown'}
        <div class="flex items-end gap-3">
          <p class="text-6xl font-bold tracking-tight">{meta.label}</p>
          {#if moving}
            <span class="pb-3 text-lg text-muted-foreground animate-pulse">…</span>
          {/if}
        </div>
      {:else}
        <Skeleton class="h-15 w-40" />
      {/if}

      <div class="mt-4 flex flex-wrap items-center gap-2">
        <Badge class={badgeColor[meta.color]}>{meta.label}</Badge>
        <Badge variant="secondary">{panelLabel[garageState.panel]}</Badge>
        <Badge variant="outline"
          >{protocolLabel[garageState.protocol] ?? garageState.protocol}</Badge
        >
      </div>
      <p class="mt-2 text-sm text-muted-foreground">{statusDetail}</p>
      {#if leftOpen.enabled && garageState.door === 'open'}
        <p class="mt-1 text-sm text-amber-600 dark:text-amber-400">
          ⏱ Left-open watch is on{#if leftOpen.warn_s}
            — warns after {Math.round(leftOpen.warn_s / 60)} min{/if}{#if leftOpen.close_s},
            auto-closes after {Math.round(leftOpen.close_s / 60)} min{/if}. Tune it in Device
          Settings.
        </p>
      {/if}

      <div class="mt-6 flex flex-wrap gap-3">
        {#if !moving && garageState.door === 'stopped'}
          <!-- Mid-travel stop: the next toggle can go either way, so offer
            both. The backend pursues the target (chained toggles on secplus,
            pulse on dry-contact) until the door gets there. -->
          <Button size="lg" class="min-w-36" onclick={() => doorCommand('open')}>
            <DoorOpen class="size-4" />
            Open
          </Button>
          <Button size="lg" class="min-w-36" onclick={() => doorCommand('close')}>
            <DoorClosed class="size-4" />
            Close
          </Button>
        {:else}
          <Button size="lg" class="min-w-36" onclick={() => doorCommand(primaryAction.action)}>
            <primaryAction.icon class="size-4" />
            {primaryAction.label}
          </Button>
        {/if}
      </div>
    </Card.Content>
  </Card.Root>

  <div class="grid gap-6 md:grid-cols-2">
    {#if garageState.caps.light}
      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg flex items-center gap-2">
            <Lightbulb class="size-4" />
            Light
          </Card.Title>
          <Card.Description>Opener light via the wall bus</Card.Description>
        </Card.Header>
        <Card.Content class="space-y-4">
          <div class="flex items-center justify-between">
            <Label>Current state</Label>
            <Badge variant="secondary">{garageState.light}</Badge>
          </div>
          <div class="flex gap-3">
            <LoadingButton
              class="flex-1 {garageState.light === 'on' ? 'disabled:opacity-100' : ''}"
              variant={garageState.light === 'on' ? 'default' : 'outline'}
              disabled={pendingCommands.light !== null || garageState.light === 'on'}
              loading={pendingCommands.light === 'on'}
              onclick={() => lightCommand('on')}
            >
              On
            </LoadingButton>
            <LoadingButton
              class="flex-1 {garageState.light === 'off' ? 'disabled:opacity-100' : ''}"
              variant={garageState.light === 'off' ? 'default' : 'outline'}
              disabled={pendingCommands.light !== null || garageState.light === 'off'}
              loading={pendingCommands.light === 'off'}
              onclick={() => lightCommand('off')}
            >
              Off
            </LoadingButton>
          </div>
        </Card.Content>
      </Card.Root>
    {/if}

    {#if garageState.caps.lock}
      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg flex items-center gap-2">
            <Lock class="size-4" />
            Remote lock
          </Card.Title>
          <Card.Description>Locks out wireless remotes</Card.Description>
        </Card.Header>
        <Card.Content class="space-y-4">
          <div class="flex items-center justify-between">
            <Label>Current state</Label>
            <Badge variant="secondary">{garageState.locked}</Badge>
          </div>
          <div class="flex gap-3">
            <LoadingButton
              class="flex-1 {garageState.locked === 'locked' ? 'disabled:opacity-100' : ''}"
              variant={garageState.locked === 'locked' ? 'default' : 'outline'}
              disabled={pendingCommands.lock !== null || garageState.locked === 'locked'}
              loading={pendingCommands.lock === 'lock'}
              icon={Lock}
              onclick={() => lockCommand('lock')}
            >
              Lock
            </LoadingButton>
            <LoadingButton
              class="flex-1 {garageState.locked === 'unlocked' ? 'disabled:opacity-100' : ''}"
              variant={garageState.locked === 'unlocked' ? 'default' : 'outline'}
              disabled={pendingCommands.lock !== null || garageState.locked === 'unlocked'}
              loading={pendingCommands.lock === 'unlock'}
              icon={LockOpen}
              onclick={() => lockCommand('unlock')}
            >
              Unlock
            </LoadingButton>
          </div>
        </Card.Content>
      </Card.Root>
    {/if}
  </div>
  {#if !garageState.caps.light && !garageState.caps.lock}
    <p class="text-sm text-muted-foreground">
      Dry-contact mode drives the opener through a relay only — light and lock controls are not
      available.
    </p>
  {/if}
</div>
