<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Left-open escalation settings (app). Warns and optionally auto-closes a
  door left open too long. Durations edit in minutes, stored in seconds. -->
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
  import { Timer, Save } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'
  import { ensureNotificationPermission, notifyDoorEvent } from 'src/app/lib/notify.js'

  let loading = $state(true)
  let saving = $state(false)
  let unsubs = $state([])
  let ticker = $state(null)

  let form = $state({ enabled: false, warn_min: 10, close_min: 0, webhook: '' })
  // Webhook is the warning channel: required with warn armed (backend
  // rejects warn-without-webhook saves). Auto-close alone stays optional.
  // Disabled while warn is off — there is nothing to warn about.
  const webhookRequired = $derived(form.enabled && Number(form.warn_min) > 0)
  const webhookDisabled = $derived(Number(form.warn_min) <= 0)
  let live = $state({ open: false, elapsed_s: 0 })

  function fmtElapsed(total) {
    const m = Math.floor(total / 60)
    const s = total % 60
    return `${m}:${String(s).padStart(2, '0')}`
  }

  function syncFromConfig(data) {
    loading = false
    form.enabled = !!data.enabled
    form.warn_min = Math.round((data.warn_s || 0) / 60)
    form.close_min = Math.round((data.close_s || 0) / 60)
    form.webhook = data.webhook || ''
    live.open = !!data.open
    live.elapsed_s = data.elapsed_s || 0
  }

  function handleSaved(data) {
    saving = false
    if (data.success) {
      toast.success('Left-open settings saved')
    } else {
      toast.error(data.message || 'Failed to save left-open settings')
    }
    sendMessage({ type: 'get_left_open' })
  }

  function submit() {
    const warn_s = Math.max(0, Math.round(Number(form.warn_min) || 0) * 60)
    const close_s = Math.max(0, Math.round(Number(form.close_min) || 0) * 60)
    const webhook = (form.webhook || '').trim()
    if (form.enabled && warn_s > 0 && !webhook) {
      toast.error('Webhook URL is required when warnings are on.')
      return
    }
    saving = true
    // Ask once, on save, so warn notifications are allowed to show later.
    if (form.enabled) {
      ensureNotificationPermission()
    }
    sendMessage({
      type: 'set_left_open',
      enabled: form.enabled,
      warn_s,
      close_s,
      webhook,
    })
  }

  function handleEvent(data) {
    if (data.event === 'warn') {
      toast.warning(`Door open ${fmtElapsed(data.elapsed_s || 0)} — left-open warning`)
    } else if (data.event === 'auto_close') {
      toast.info('Auto-closing the door (left open too long)')
    } else if (data.event === 'blocked') {
      toast.error('Auto-close blocked — obstruction detected')
    } else if (data.event === 'closed') {
      toast.success('Auto-closed door is now shut')
    }
    notifyDoorEvent(data.event, data.elapsed_s || 0)
    sendMessage({ type: 'get_left_open' })
  }

  onMount(() => {
    const u1 = onMessageType('left_open_config', syncFromConfig)
    const u2 = onMessageType('left_open_saved', handleSaved)
    const u3 = onMessageType('door_event', handleEvent)
    unsubs = [u1, u2, u3]
    sendMessage({ type: 'get_left_open' })
    // Tick the open timer locally so the countdown feels live.
    ticker = setInterval(() => {
      if (live.open) live.elapsed_s += 1
    }, 1000)
    return () => {
      unsubs.forEach((u) => u())
      if (ticker) clearInterval(ticker)
    }
  })

  onDestroy(() => {
    unsubs.forEach((u) => u())
    if (ticker) clearInterval(ticker)
  })
</script>

<Card.Root class="mt-6">
  <Card.Header class="pb-3">
    <div class="flex items-center justify-between">
      <div class="space-y-1">
        <Card.Title class="text-lg flex items-center gap-2">
          <Timer class="size-4" />
          Left-open escalation
        </Card.Title>
        <Card.Description>Warn, then optionally close, a door left open too long.</Card.Description>
      </div>
      {#if !loading}
        {#if form.enabled}
          <Badge variant="secondary">On</Badge>
        {:else}
          <Badge variant="outline">Off</Badge>
        {/if}
      {/if}
    </div>
  </Card.Header>

  <Card.Content class="space-y-4">
    {#if loading}
      <Skeleton class="h-10 w-full rounded-md" />
    {:else}
      <div class="flex items-center justify-between rounded-lg border p-3">
        <div class="space-y-0.5">
          <Label class="font-medium">Enabled</Label>
          <p class="text-xs text-muted-foreground">
            {#if live.open}
              Door open for {fmtElapsed(live.elapsed_s)}
            {/if}
          </p>
        </div>
        <Switch
          checked={form.enabled}
          onCheckedChange={(v) => (form.enabled = v)}
          aria-label="Enable left-open escalation"
        />
      </div>

      <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
        <div class="space-y-2">
          <Label for="leftopen-warn">Warn after (minutes, 0 = never)</Label>
          <Input id="leftopen-warn" type="number" min="0" max="1440" bind:value={form.warn_min} />
        </div>
        <div class="space-y-2">
          <Label for="leftopen-close">Auto-close after (minutes, 0 = never)</Label>
          <Input id="leftopen-close" type="number" min="0" max="1440" bind:value={form.close_min} />
        </div>
      </div>

      <div class="space-y-2">
        <Label for="leftopen-webhook"
          >Webhook URL {webhookRequired ? '(required)' : '(optional)'}</Label
        >
        <Input
          id="leftopen-webhook"
          bind:value={form.webhook}
          disabled={webhookDisabled}
          placeholder="https://ntfy.sh/my-topic"
        />
        <p class="text-xs text-muted-foreground">
          Warnings only go here (e.g. ntfy topic + phone app).
        </p>
      </div>

      <LoadingButton
        class="w-full"
        onclick={submit}
        disabled={saving}
        loading={saving}
        loadingLabel="Saving..."
        icon={Save}
      >
        Save escalation settings
      </LoadingButton>
    {/if}
  </Card.Content>
</Card.Root>
