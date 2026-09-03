<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import SectionHeader from 'src/components/common/SectionHeader.svelte'
  import LoadingButton from 'src/components/common/LoadingButton.svelte'
  import {
    garageState,
    initializeGarage,
    doorCommand,
    lightCommand,
    lockCommand,
    syncGarage,
    pendingCommands,
  } from 'src/lib/door.svelte.js'
  import { doorMeta, isSettled } from 'src/lib/garage.js'
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

  onMount(() => {
    unsub = initializeGarage()
    return () => {
      if (unsub) unsub()
    }
  })

  onDestroy(() => {
    if (unsub) unsub()
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
  }
</script>

<SectionHeader
  title="Garage Door"
  subtitle="Security+ 1.0 wall bus - open, close and watch your door."
/>

<div class="space-y-6">
  {#if garageState.obstruction}
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
      </div>
      <p class="mt-2 text-sm text-muted-foreground">{statusDetail}</p>

      <div class="mt-6 flex flex-wrap gap-3">
        <Button size="lg" class="min-w-36" onclick={() => doorCommand(primaryAction.action)}>
          <primaryAction.icon class="size-4" />
          {primaryAction.label}
        </Button>
      </div>
    </Card.Content>
  </Card.Root>

  <div class="grid gap-6 md:grid-cols-2">
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
  </div>
</div>
