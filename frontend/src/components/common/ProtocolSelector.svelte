<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->
<!-- Manual opener-protocol selection, MQTT-tab style: horizontal select cards
     plus a single save. The rest of the UI adapts from caps, so this component
     never gates anything itself. Dry settings ride along on set_protocol. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import { protocolState, dryCfg, saveProtocol } from 'src/lib/door.svelte.js'
  import { zigbeeState, initializeZigbee } from 'src/lib/zigbee.svelte.js'
  import { onMessageType } from 'src/lib/ws.svelte.js'
  import LoadingButton from 'src/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Input } from '$lib/components/ui/input'
  import { Label } from '$lib/components/ui/label'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Save } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  const PROTOCOLS = [
    {
      id: 'drycontact',
      label: 'Dry contact',
      desc: 'Relay pulse across the opener button terminals. Position comes from reed sensors below.',
    },
    {
      id: 'secplus1',
      label: 'Security+ 1.0',
      desc: 'Wall-bus control. Door, light, lock and obstruction come from the bus.',
    },
    {
      id: 'secplus2',
      label: 'Security+ 2.0',
      desc: 'Yellow learn-button openers. Coming soon.',
      disabled: true,
    },
  ]

  const SENSOR_MODES = [
    {
      mode: 0,
      label: 'Relay only',
      desc: 'Blind toggle. No position feedback — the door stays unknown.',
    },
    {
      mode: 1,
      label: 'Close reed',
      desc: 'Knows closed vs not-closed. Openings are assumed once travel time passes.',
    },
    {
      mode: 2,
      label: 'Both reeds',
      desc: 'Best: confirms fully open and fully closed, and infers opening/closing in between.',
    },
  ]

  let form = $state({ id: 'secplus1', dry: { ...dryCfg } })
  let saving = $state(false)
  // Unknown until the first `protocol` message arrives (same pattern as the
  // MQTT tab's configLoading): skeleton first, sections once known. The flag
  // lives on the shared store so the tab can hide the cards below as well.
  let unsub = $state(null)

  function syncForm() {
    form.id = protocolState.id
    form.dry = { ...dryCfg }
  }

  function submit() {
    saving = true
    if (form.id === 'drycontact') saveProtocol(form.id, { ...form.dry })
    else saveProtocol(form.id)
  }

  onMount(() => {
    syncForm()
    const unsubZigbee = initializeZigbee()
    // NOTE: read the message payload directly, not the stores. This listener
    // is registered before initializeGarage's (child onMount fires first),
    // so the stores still hold the previous protocol when this runs.
    unsub = onMessageType('protocol', (data) => {
      if (typeof data.id === 'string') form.id = data.id
      if (data.dry) form.dry = { ...data.dry }
      if (saving) {
        saving = false
        toast.success('Opener protocol saved')
      }
    })
    return () => {
      if (unsub) unsub()
      if (unsubZigbee) unsubZigbee()
    }
  })

  onDestroy(() => {
    if (unsub) unsub()
  })
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <div class="flex items-center justify-between">
      <div class="space-y-1">
        <Card.Title class="text-lg">Opener protocol</Card.Title>
        <Card.Description>
          Manual selection — the Garage Door tab adapts to what the protocol supports.
        </Card.Description>
      </div>
      <Badge variant="secondary">{PROTOCOLS.find((p) => p.id === form.id)?.label ?? form.id}</Badge>
    </div>
  </Card.Header>

  <Card.Content class="space-y-4">
    {#if !protocolState.loaded}
      <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
        {#each [1, 2, 3] as _}
          <Skeleton class="h-20 rounded-lg" />
        {/each}
      </div>
      <Skeleton class="h-10 w-full rounded-md" />
    {:else}
      <div class="space-y-2">
        <Label class="font-medium">Protocol</Label>
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
          {#each PROTOCOLS as p (p.id)}
            <button
              type="button"
              disabled={p.disabled}
              onclick={() => (form.id = p.id)}
              class="text-left rounded-lg border p-3 transition-colors disabled:opacity-50
            {form.id === p.id
                ? 'border-primary bg-primary/10 text-foreground'
                : 'border-border text-muted-foreground hover:bg-accent'}"
            >
              <div class="flex items-center gap-2 font-medium">
                <span
                  class="size-3 rounded-full border
              {form.id === p.id ? 'bg-primary border-primary' : 'border-border'}"
                ></span>
                {p.label}
              </div>
              <p class="mt-1 text-xs text-muted-foreground leading-snug">{p.desc}</p>
            </button>
          {/each}
        </div>
      </div>

      {#if form.id === 'drycontact'}
        <div class="space-y-2">
          <Label class="font-medium">Reed sensors</Label>
          <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
            {#each SENSOR_MODES as s (s.mode)}
              <button
                type="button"
                onclick={() => (form.dry.sensor_mode = s.mode)}
                class="text-left rounded-lg border p-3 transition-colors
              {form.dry.sensor_mode === s.mode
                  ? 'border-primary bg-primary/10 text-foreground'
                  : 'border-border text-muted-foreground hover:bg-accent'}"
              >
                <div class="flex items-center gap-2 font-medium">
                  <span
                    class="size-3 rounded-full border
                {form.dry.sensor_mode === s.mode ? 'bg-primary border-primary' : 'border-border'}"
                  ></span>
                  {s.label}
                </div>
                <p class="mt-1 text-xs text-muted-foreground leading-snug">{s.desc}</p>
              </button>
            {/each}
          </div>
        </div>

        <div class="grid grid-cols-1 sm:grid-cols-3 gap-4">
          <div class="space-y-2">
            <Label for="dry-pulse">Relay pulse (ms)</Label>
            <Input
              id="dry-pulse"
              type="number"
              min="100"
              max="5000"
              bind:value={form.dry.pulse_ms}
            />
          </div>
          <div class="space-y-2">
            <Label for="dry-debounce">Reeds debounce (ms)</Label>
            <Input
              id="dry-debounce"
              type="number"
              min="0"
              max="1000"
              bind:value={form.dry.debounce_ms}
            />
          </div>
          <div class="space-y-2">
            <Label for="dry-travel">Door travel time (s)</Label>
            <Input id="dry-travel" type="number" min="5" max="300" bind:value={form.dry.travel_s} />
          </div>
        </div>
      {/if}

      {#if (zigbeeState.config.joined || zigbeeState.config.commissioned) && form.id !== protocolState.id}
        <p class="text-xs text-amber-500 leading-snug">
          Zigbee is configured — saving a different protocol disconnects it and reboots. Re-pair
          afterwards.
        </p>
      {/if}

      <LoadingButton
        class="w-full"
        onclick={submit}
        disabled={saving}
        loading={saving}
        loadingLabel="Saving..."
        icon={Save}
      >
        Save protocol
      </LoadingButton>
    {/if}
  </Card.Content>
</Card.Root>
