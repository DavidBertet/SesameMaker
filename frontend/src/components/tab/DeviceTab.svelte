<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Input } from '$lib/components/ui/input'
  import { Label } from '$lib/components/ui/label'
  import { Badge } from '$lib/components/ui/badge'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Switch } from '$lib/components/ui/switch'
  import { Select, SelectTrigger, SelectContent, SelectItem } from '$lib/components/ui/select'

  import { Radio, RadioTower, Save, Link, LogOut, Trash2, Info } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import { mqttState, initializeMqtt, saveMqttConfig } from 'src/core/lib/mqtt.svelte.js'
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

  // Used to indicate a password is already saved without ever showing it.
  const PASSWORD_SENTINEL = '••••••••'

  let saving = $state(false)
  let configLoading = $state(true)
  let zigLoading = $state(true)
  let zigBusy = $state(false)
  let unsubs = $state([])

  let form = $state({
    mode: 'off', // 'off' | 'master' | 'ha'
    uri: '',
    username: '',
    password: '',
    topic_prefix: 'home/sesame',
  })

  let configured = $derived(mqttState.config.uri !== '' && mqttState.config.mode !== 'off')
  let showZigbee = $derived(zigbeeSupported(settingsState))
  let zigMeta = $derived(zigbeeStatusMeta(zigbeeState.config))

  const MODES = [
    { value: 'off', label: 'Off', desc: 'MQTT disabled' },
    { value: 'master', label: 'Master', desc: 'Plain topics, any MQTT client' },
    { value: 'ha', label: 'Home Assistant', desc: 'Auto-discovery via MQTT' },
  ]

  function modeMeta(m) {
    return MODES.find((x) => x.value === m) || MODES[0]
  }

  function syncFormFromConfig() {
    configLoading = false
    form.mode = mqttState.config.mode
    form.uri = mqttState.config.uri
    form.username = mqttState.config.username
    form.topic_prefix = mqttState.config.topic_prefix
    // Only mask the password: the real value never leaves the device. If one is
    // saved, we show a sentinel and send an empty string unless it is edited.
    form.password = mqttState.config.password_set ? PASSWORD_SENTINEL : ''
  }

  function handleSaved(data) {
    if (data.success) {
      toast.success('MQTT configuration saved')
    } else {
      toast.error(data.message || 'Failed to save MQTT configuration')
    }
    saving = false
    // Re-sync in case the server normalized anything.
    sendMessage({ type: 'get_mqtt_config' })
  }

  function submitMqtt() {
    saving = true
    // If the user never touched the password field, send empty string so the
    // backend keeps the stored password.
    const password = form.password === PASSWORD_SENTINEL ? '' : form.password
    saveMqttConfig({ ...form, password })
  }

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

  onMount(() => {
    const u1 = initializeMqtt()
    const u2 = onMessageType('mqtt_config', syncFormFromConfig)
    const u3 = onMessageType('mqtt_config_saved', handleSaved)
    const u4 = initializeZigbee()
    const u5 = onMessageType('zigbee_config', () => (zigLoading = false))
    const u6 = onMessageType('zigbee_saved', handleZigResult('saved'))
    const u7 = onMessageType('zigbee_pair', handleZigResult('pair'))
    const u8 = onMessageType('zigbee_leave', handleZigResult('leave'))
    const u9 = onMessageType('zigbee_reset', handleZigResult('reset'))
    // Backend rejections come as generic errors (e.g. Zigbee unsupported on
    // this build) — unlock the buttons instead of spinning forever.
    const u10 = onMessageType('error', () => setZigBusy(false))
    unsubs = [u1, u2, u3, u4, u5, u6, u7, u8, u9, u10]

    return () => {
      unsubs.forEach((u) => u())
    }
  })

  onDestroy(() => {
    if (zigTimeout) clearTimeout(zigTimeout)
  })
</script>

<SectionHeader title="Device Settings" subtitle="MQTT bridge and Zigbee connectivity" />

<Card.Root>
  <Card.Header class="pb-3">
    <div class="flex items-center justify-between">
      <div class="space-y-1">
        <Card.Title class="text-lg flex items-center gap-2">
          <Radio class="size-4" />
          MQTT bridge
        </Card.Title>
        <Card.Description>
          Publish door state and subscribe to commands. Topics:&nbsp;
          <code>{mqttState.config.topic_prefix}/state</code>
          and <code>{mqttState.config.topic_prefix}/set</code>.
        </Card.Description>
      </div>
      {#if configLoading}
        <Skeleton class="h-6 w-20" />
      {:else if configured}
        <Badge variant="secondary">{modeMeta(mqttState.config.mode).label}</Badge>
      {/if}
    </div>
  </Card.Header>

  <Card.Content class="space-y-4">
    {#if configLoading}
      <div class="space-y-4">
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
          {#each [1, 2, 3] as _}
            <Skeleton class="h-20 rounded-lg" />
          {/each}
        </div>
        <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
          {#each [1, 2, 3, 4] as _}
            <div class="space-y-2">
              <Skeleton class="h-4 w-24" />
              <Skeleton class="h-10 w-full rounded-md" />
            </div>
          {/each}
        </div>
        <Skeleton class="h-10 w-full rounded-md" />
      </div>
    {:else}
      <div class="space-y-2">
        <Label class="font-medium">Mode</Label>
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-2">
          {#each MODES as m (m.value)}
            <button
              type="button"
              onclick={() => (form.mode = m.value)}
              class="text-left rounded-lg border p-3 transition-colors
              {form.mode === m.value
                ? 'border-primary bg-primary/10 text-foreground'
                : 'border-border text-muted-foreground hover:bg-accent'}"
            >
              <div class="flex items-center gap-2 font-medium">
                <span
                  class="size-3 rounded-full border
                {form.mode === m.value ? 'bg-primary border-primary' : 'border-border'}"
                ></span>
                {m.label}
              </div>
              <p class="mt-1 text-xs text-muted-foreground leading-snug">{m.desc}</p>
            </button>
          {/each}
        </div>
      </div>

      <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
        <div class="space-y-2">
          <Label for="mqtt-uri">Broker URI</Label>
          <Input id="mqtt-uri" bind:value={form.uri} placeholder="mqtt://192.168.1.50:1883" />
        </div>
        <div class="space-y-2">
          <Label for="mqtt-prefix">Topic prefix</Label>
          <Input id="mqtt-prefix" bind:value={form.topic_prefix} placeholder="home/sesame" />
        </div>
        <div class="space-y-2">
          <Label for="mqtt-user">Username</Label>
          <Input id="mqtt-user" bind:value={form.username} placeholder="Optional" />
        </div>
        <div class="space-y-2">
          <Label for="mqtt-pass">Password</Label>
          <Input
            id="mqtt-pass"
            type="password"
            bind:value={form.password}
            placeholder={mqttState.config.password_set ? 'Set (leave as-is to keep)' : 'Optional'}
          />
        </div>
      </div>

      <LoadingButton
        class="w-full"
        onclick={submitMqtt}
        disabled={saving || configLoading}
        loading={saving}
        loadingLabel="Saving..."
        icon={Save}
      >
        Save configuration
      </LoadingButton>
    {/if}
  </Card.Content>
</Card.Root>

{#if showZigbee}
  <Card.Root class="mt-6">
    <Card.Header class="pb-3">
      <div class="flex items-center justify-between">
        <div class="space-y-1">
          <Card.Title class="text-lg flex items-center gap-2">
            <RadioTower class="size-4" />
            Zigbee
          </Card.Title>
          <Card.Description>
            Join a Zigbee network as plain switches: door, light and remote lockout. Endpoints
            follow the opener protocol.
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
                )}
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

        <p class="text-xs text-muted-foreground leading-snug">
          No coordinator nearby? Hold BOOT 3s to open pairing, 10s+ to factory reset. Same actions
          as the buttons above.
        </p>
      {/if}
    </Card.Content>
  </Card.Root>
{/if}
