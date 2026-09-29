<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Destructive USB resets (core, generic): network reset (Wi-Fi + OTA
  password) and full factory reset (everything). Both reboot the device, so
  onGone runs after the ack to let the host release the dead session. -->
<script>
  import * as Card from '$lib/components/ui/card'
  import { Button } from '$lib/components/ui/button'
  import { Trash2 } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  let { session, onGone = () => {} } = $props()

  // Two-click confirms for the destructive resets below.
  let resetArmed = $state(false)
  let eraseArmed = $state(false)
  let busy = $state(false)

  async function run(fn) {
    if (!session || busy) return
    busy = true
    try {
      await fn()
    } catch (e) {
      toast.error(e.message || String(e))
    } finally {
      busy = false
    }
  }

  async function networkReset() {
    if (!resetArmed) {
      resetArmed = true
      return
    }
    resetArmed = false
    await run(async () => {
      await session.networkReset()
      // Device reboots into the setup AP; the cable session is gone.
      await onGone()
      toast.success('Device reset — rebooting into the setup AP.')
    })
  }

  async function factoryReset() {
    if (!eraseArmed) {
      eraseArmed = true
      return
    }
    eraseArmed = false
    await run(async () => {
      await session.factoryReset()
      // Everything erased; the reboot lands on first-boot defaults.
      await onGone()
      toast.success('Device erased — rebooting to first-boot defaults.')
    })
  }
</script>

<Card.Root class="border-destructive/40">
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <Trash2 class="size-4" />
      Network reset
    </Card.Title>
    <Card.Description>
      Forgets Wi-Fi, clears the OTA password and reboots into the setup AP. Device settings stay.
    </Card.Description>
  </Card.Header>
  <Card.Content class="flex flex-wrap items-center gap-3">
    <Button size="sm" variant="destructive" onclick={networkReset} disabled={busy}>
      {resetArmed ? 'Click again to confirm reset' : 'Network reset…'}
    </Button>
    {#if resetArmed}
      <Button size="sm" variant="ghost" onclick={() => (resetArmed = false)} disabled={busy}>
        Cancel
      </Button>
    {/if}
  </Card.Content>
</Card.Root>

<Card.Root class="border-destructive">
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <Trash2 class="size-4" />
      Factory reset
    </Card.Title>
    <Card.Description>
      Erases everything — Wi-Fi, all settings, Zigbee network, HomeKit pairing — and reboots to
      first boot. No undo.
    </Card.Description>
  </Card.Header>
  <Card.Content class="flex flex-wrap items-center gap-3">
    <Button size="sm" variant="destructive" onclick={factoryReset} disabled={busy}>
      {eraseArmed ? 'Click again to erase everything' : 'Factory reset…'}
    </Button>
    {#if eraseArmed}
      <Button size="sm" variant="ghost" onclick={() => (eraseArmed = false)} disabled={busy}>
        Cancel
      </Button>
    {/if}
  </Card.Content>
</Card.Root>
