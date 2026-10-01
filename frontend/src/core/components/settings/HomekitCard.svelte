<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- HomeKit pairing management (core, generic). Product/accessory copy
  comes via props. -->
<script>
  import { onMount, onDestroy } from 'svelte'
  import { onMessageType } from 'src/core/lib/ws.svelte.js'
  import * as Card from '$lib/components/ui/card'
  import { Label } from '$lib/components/ui/label'
  import { Badge } from '$lib/components/ui/badge'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Switch } from '$lib/components/ui/switch'

  import { House } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'
  import QRCode from 'qrcode'

  import HomekitSetupLabel from 'src/core/components/common/HomekitSetupLabel.svelte'
  import { homekitSetupCodeFromUri, formatHomekitSetupCode } from 'src/core/lib/homekit.js'
  import {
    homekitState,
    initializeHomekit,
    saveHomekitConfig,
  } from 'src/core/lib/homekit.svelte.js'

  let { productName = 'this device', accessoryKind = 'accessory' } = $props()

  let hkLoading = $state(true)
  let hkBusy = $state(false)
  let hkQr = $state('')
  let unsubs = $state([])
  let hkCode = $derived(homekitSetupCodeFromUri(homekitState.config.setup_uri))
  let hkCodeDisplay = $derived(formatHomekitSetupCode(hkCode))

  // HomeKit enable flip. Same safety net as Zigbee: the broadcast carries
  // the fresh state, the timeout guards a lost reply.
  let hkTimeout = $state(null)

  function setHkBusy(on) {
    hkBusy = on
    if (hkTimeout) {
      clearTimeout(hkTimeout)
      hkTimeout = null
    }
    if (on) {
      hkTimeout = setTimeout(() => {
        hkBusy = false
        hkTimeout = null
        toast.error('HomeKit action timed out — no reply from the device')
      }, 8000)
    }
  }

  function toggleHomekit(enabled) {
    setHkBusy(true)
    saveHomekitConfig(enabled)
  }

  function handleHkSaved(data) {
    setHkBusy(false)
    if (data.success) {
      toast.success(homekitState.config.enabled ? 'HomeKit enabled' : 'HomeKit disabled')
    } else {
      toast.error('HomeKit action failed')
    }
  }

  onMount(() => {
    const u1 = initializeHomekit()
    const u2 = onMessageType('homekit_config', () => (hkLoading = false))
    const u3 = onMessageType('homekit_saved', handleHkSaved)
    // Backend rejections come as generic errors — unlock instead of
    // spinning forever.
    const u4 = onMessageType('error', () => {
      setHkBusy(false)
    })
    unsubs = [u1, u2, u3, u4]
    return () => {
      unsubs.forEach((u) => u())
    }
  })

  onDestroy(() => {
    if (hkTimeout) clearTimeout(hkTimeout)
  })

  // Render the HomeKit pairing QR whenever the setup URI arrives. The
  // token guards the async race (a newer URI wins).
  let hkQrToken = 0
  $effect(() => {
    const uri = homekitState.config.setup_uri
    const paired = homekitState.config.paired
    if (!uri || paired) {
      hkQr = ''
      return
    }
    const token = ++hkQrToken
    QRCode.toDataURL(uri, {
      width: 480,
      margin: 1,
      errorCorrectionLevel: 'M',
      color: { dark: '#000000', light: '#ffffff' },
    }).then((url) => {
      if (token === hkQrToken) hkQr = url
    })
  })
</script>

<Card.Root class="mt-6">
  <Card.Header class="pb-3">
    <div class="flex items-center justify-between">
      <div class="space-y-1">
        <Card.Title class="text-lg flex items-center gap-2">
          <House class="size-4" />
          HomeKit
        </Card.Title>
        <Card.Description>
          Add {productName} to the Apple Home app as a {accessoryKind}.
        </Card.Description>
      </div>
      {#if hkLoading}
        <Skeleton class="h-6 w-20" />
      {:else if !homekitState.config.enabled}
        <Badge variant="outline">Disabled</Badge>
      {:else if homekitState.config.paired}
        <Badge variant="secondary">Paired</Badge>
      {:else if homekitState.config.started}
        <Badge variant="outline">Ready to pair</Badge>
      {:else}
        <Badge variant="outline">Starting…</Badge>
      {/if}
    </div>
  </Card.Header>

  <Card.Content class="space-y-4">
    {#if hkLoading}
      <Skeleton class="h-10 w-full rounded-md" />
    {:else}
      <div class="flex items-center justify-between rounded-lg border p-3">
        <div class="space-y-0.5">
          <Label class="font-medium">HomeKit accessory</Label>
          <p class="text-xs text-muted-foreground">Turn on, then pair with your Apple device</p>
        </div>
        <Switch
          checked={homekitState.config.enabled}
          onCheckedChange={toggleHomekit}
          disabled={hkBusy}
          aria-label="Enable HomeKit"
        />
      </div>
    {/if}
    {#if !hkLoading}
      <Card.Enabled enabled={homekitState.config.enabled}>
        {#if homekitState.config.paired}
          <p class="text-sm text-muted-foreground">
            Paired with Apple Home. The setup code is hidden while paired.
          </p>
        {:else if hkQr}
          <div class="flex items-start gap-4">
            <HomekitSetupLabel setupUri={homekitState.config.setup_uri} qrSrc={hkQr} />
            <div class="space-y-2 pt-1">
              <p class="text-sm text-muted-foreground">
                Scan with the Home app, or enter the code manually.
              </p>
              {#if hkCodeDisplay}
                <code class="text-sm tracking-widest">{hkCodeDisplay}</code>
              {/if}
            </div>
          </div>
        {:else}
          <p class="text-sm text-muted-foreground">
            {#if !homekitState.config.enabled}
              Turn on above to start pairing.
            {:else if homekitState.config.started}
              Waiting for the setup code…
            {:else}
              HomeKit is still starting — this refreshes automatically.
            {/if}
          </p>
        {/if}
      </Card.Enabled>
    {/if}
  </Card.Content>
</Card.Root>
