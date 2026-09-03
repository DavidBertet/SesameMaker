<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import { sendMessage, onMessageType } from 'src/lib/ws.svelte.js'
  import LoadingButton from 'src/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Input } from '$lib/components/ui/input'
  import { Label } from '$lib/components/ui/label'
  import { Badge } from '$lib/components/ui/badge'
  import { Skeleton } from '$lib/components/ui/skeleton'

  import { Radio, Save } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  import SectionHeader from 'src/components/common/SectionHeader.svelte'
  import { mqttState, initializeMqtt, saveMqttConfig } from 'src/lib/mqtt.svelte.js'

  // Used to indicate a password is already saved without ever showing it.
  const PASSWORD_SENTINEL = '••••••••'

  let saving = $state(false)
  let configLoading = $state(true)
  let configuredUnsub = $state(null)
  let savedUnsub = $state(null)

  let form = $state({
    mode: 'off', // 'off' | 'master' | 'ha'
    uri: '',
    username: '',
    password: '',
    topic_prefix: 'home/sesame',
  })

  let configured = $derived(mqttState.config.uri !== '' && mqttState.config.mode !== 'off')

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

  onMount(() => {
    configuredUnsub = initializeMqtt()
    // Re-sync the form from the first mqtt_config response.
    const syncUnsub = onMessageType('mqtt_config', syncFormFromConfig)
    savedUnsub = onMessageType('mqtt_config_saved', handleSaved)

    return () => {
      configuredUnsub()
      syncUnsub()
      savedUnsub()
    }
  })

  onDestroy(() => {
    if (configuredUnsub) configuredUnsub()
    if (savedUnsub) savedUnsub()
  })
</script>

<SectionHeader title="MQTT" subtitle="Publish door state and receive commands over MQTT" />

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
