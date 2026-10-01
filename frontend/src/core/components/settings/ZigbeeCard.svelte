<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Zigbee network management (core, generic). Product-specific copy
  (what the endpoints expose, hardware pairing hints) comes via props. -->
<script>
  import { onMount, onDestroy } from 'svelte'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Label } from '$lib/components/ui/label'
  import { Badge } from '$lib/components/ui/badge'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Switch } from '$lib/components/ui/switch'
  import { Select, SelectTrigger, SelectContent, SelectItem } from '$lib/components/ui/select'

  import { RadioTower, Link, LogOut, Trash2, Info } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  import {
    zigbeeState,
    initializeZigbee,
    saveZigbeeConfig,
    saveZigbeeChannel,
    zigbeePair,
    zigbeeLeave,
    zigbeeReset,
  } from 'src/core/lib/zigbee.svelte.js'
  import {
    zigbeeSupported,
    zigbeeStatusMeta,
    formatPanId,
    zigbeeLqiLabel,
    zigbeeParentLabel,
    ZIGBEE_CHANNEL_OPTIONS,
    zigbeeChannelLabel,
  } from 'src/core/lib/zigbee.js'
  import { settingsState } from 'src/core/lib/settings.svelte.js'

  let { description = 'Join a Zigbee network as plain switches.', pairingHint = '' } = $props()

  let zigLoading = $state(true)
  let zigBusy = $state(false)

  let showCard = $derived(zigbeeSupported(settingsState))
  let zigMeta = $derived(zigbeeStatusMeta(zigbeeState.config))

  // Safety net: a lost reply or backend error must never leave the buttons
  // spinning forever. Every action arms an 8s timeout; any result clears it.
  let zigTimeout = $state(null)

  function setZigBusy(on) {
    zigBusy = on
    if (zigTimeout) {
      clearTimeout(zigTimeout)
      zigTimeout = null
    }
    if (on) {
      zigTimeout = setTimeout(() => {
        zigBusy = false
        zigTimeout = null
        toast.error('Zigbee action timed out — no reply from the device')
      }, 8000)
    }
  }

  function handleZigResult(type) {
    return (data) => {
      setZigBusy(false)
      if (data.success) {
        toast.success(
          type === 'pair'
            ? 'Pairing window open — put your coordinator in permit-join'
            : type === 'leave'
              ? 'Left Zigbee network'
              : type === 'reset'
                ? 'Zigbee factory reset done'
                : 'Zigbee configuration saved',
        )
      } else {
        toast.error('Zigbee action failed')
      }
      // No re-get on saved-success: the broadcast already pushed the fresh
      // state (no-change needs no echo, the requester holds current values).
      // Re-fetch only to resync after a rejection.
      if (!(type === 'saved' && data.success)) {
        sendMessage({ type: 'get_zigbee_config' })
      }
    }
  }

  function toggleZigbee(enabled) {
    setZigBusy(true)
    saveZigbeeConfig(enabled)
  }

  function pairZigbee() {
    setZigBusy(true)
    zigbeePair(60)
  }

  function leaveZigbee() {
    setZigBusy(true)
    zigbeeLeave()
  }

  function confirmReset() {
    if (window.confirm('Factory reset Zigbee? Leaves the network and clears all bindings.')) {
      setZigBusy(true)
      zigbeeReset()
    }
  }

  function handleChannelChange(value) {
    setZigBusy(true)
    saveZigbeeChannel(Number(value))
  }

  let unsubs = $state([])
  onMount(() => {
    const u1 = initializeZigbee()
    const u2 = onMessageType('zigbee_config', () => (zigLoading = false))
    const u3 = onMessageType('zigbee_saved', handleZigResult('saved'))
    const u4 = onMessageType('zigbee_pair', handleZigResult('pair'))
    const u5 = onMessageType('zigbee_leave', handleZigResult('leave'))
    const u6 = onMessageType('zigbee_reset', handleZigResult('reset'))
    // Backend rejections come as generic errors (e.g. Zigbee unsupported on
    // this build) — unlock the buttons instead of spinning forever.
    const u7 = onMessageType('error', () => {
      setZigBusy(false)
    })
    unsubs = [u1, u2, u3, u4, u5, u6, u7]
    return () => {
      unsubs.forEach((u) => u())
    }
  })

  onDestroy(() => {
    if (zigTimeout) clearTimeout(zigTimeout)
  })
</script>

{#if showCard}
  <Card.Root class="mt-6">
    <Card.Header class="pb-3">
      <div class="flex items-center justify-between">
        <div class="space-y-1">
          <Card.Title class="text-lg flex items-center gap-2">
            <RadioTower class="size-4" />
            Zigbee
          </Card.Title>
          <Card.Description>
            {description}
          </Card.Description>
        </div>
        {#if zigLoading}
          <Skeleton class="h-6 w-20" />
        {:else}
          <Badge variant={zigMeta.variant}>{zigMeta.label}</Badge>
        {/if}
      </div>
    </Card.Header>

    <Card.Content class="space-y-4">
      {#if zigLoading}
        <Skeleton class="h-10 w-full rounded-md" />
      {:else if !zigbeeState.config.supported}
        <p class="text-sm text-muted-foreground">
          Zigbee is not available on this build (needs an ESP32-C6).
        </p>
      {:else}
        <div class="flex items-center justify-between rounded-lg border p-3">
          <div class="space-y-0.5">
            <Label class="font-medium">Zigbee radio</Label>
            <p class="text-xs text-muted-foreground">
              {#if zigbeeState.config.joined}
                Joined ch {zigbeeState.config.channel} · PAN {formatPanId(
                  zigbeeState.config.pan_id,
                )} · short {formatPanId(zigbeeState.config.short_addr)}
                {#if zigbeeState.config.lqi_valid}
                  · Link {zigbeeState.config.lqi} ({zigbeeLqiLabel(zigbeeState.config.lqi)})
                  {zigbeeParentLabel(zigbeeState.config)}
                {/if}
              {:else if zigbeeState.config.commissioned}
                Saved network — reconnecting… Leave then pair to join a new network
              {:else}
                Turn on, then pair with your coordinator
              {/if}
            </p>
          </div>
          <Switch
            checked={zigbeeState.config.enabled}
            onCheckedChange={toggleZigbee}
            disabled={zigBusy}
            aria-label="Enable Zigbee"
          />
        </div>

        <Card.Enabled enabled={zigbeeState.config.enabled}>
          <div class="flex items-center justify-between rounded-lg border p-3">
            <div class="space-y-0.5">
              <Label class="font-medium">Scan channel</Label>
              <p class="text-xs text-muted-foreground">
                Pinned channel scans faster and reaches farther on a weak link; Auto scans all 16.
                {#if zigbeeState.config.joined}
                  Leave the network to change it.
                {/if}
              </p>
            </div>
            <span
              title={zigbeeState.config.joined
                ? 'Leave the network to change the scan channel'
                : undefined}
            >
              <Select
                type="single"
                value={`${zigbeeState.config.channel_cfg}`}
                onValueChange={handleChannelChange}
                disabled={zigBusy || zigbeeState.config.joined}
              >
                <SelectTrigger class="w-44" aria-label="Scan channel">
                  {zigbeeChannelLabel(zigbeeState.config.channel_cfg)}
                </SelectTrigger>
                <SelectContent>
                  {#each ZIGBEE_CHANNEL_OPTIONS as ch}
                    <SelectItem value={`${ch}`}>{zigbeeChannelLabel(ch)}</SelectItem>
                  {/each}
                </SelectContent>
              </Select>
            </span>
          </div>

          {#if zigbeeState.config.pairing_remaining_s > 0}
            <div
              class="flex items-center gap-3 rounded-lg border border-sky-300 bg-sky-50 dark:border-sky-900 dark:bg-sky-950 p-4"
            >
              <Info class="size-5 shrink-0 text-sky-600 dark:text-sky-400" />
              <p class="text-sm text-sky-700 dark:text-sky-300">
                Pairing is open — put your coordinator (ZHA / Zigbee2MQTT) in permit-join now.
              </p>
            </div>
          {/if}

          <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
            <LoadingButton
              onclick={pairZigbee}
              disabled={zigBusy ||
                !zigbeeState.config.enabled ||
                zigbeeState.config.joined ||
                zigbeeState.config.commissioned}
              loading={zigBusy}
              loadingLabel="Working…"
              icon={Link}
            >
              {#if zigbeeState.config.pairing_remaining_s > 0}
                Pairing ({zigbeeState.config.pairing_remaining_s}s)
              {:else}
                Pair (60s)
              {/if}
            </LoadingButton>
            <LoadingButton
              variant="outline"
              onclick={leaveZigbee}
              disabled={zigBusy || !(zigbeeState.config.joined || zigbeeState.config.commissioned)}
              loading={zigBusy}
              loadingLabel="Working…"
              icon={LogOut}
            >
              Leave network
            </LoadingButton>
            <LoadingButton
              variant="destructive"
              onclick={confirmReset}
              disabled={zigBusy}
              loading={zigBusy}
              loadingLabel="Working…"
              icon={Trash2}
            >
              Factory reset
            </LoadingButton>
          </div>

          {#if pairingHint}
            <p class="text-xs text-muted-foreground leading-snug">
              {pairingHint}
            </p>
          {/if}
        </Card.Enabled>
      {/if}
    </Card.Content>
  </Card.Root>
{/if}
