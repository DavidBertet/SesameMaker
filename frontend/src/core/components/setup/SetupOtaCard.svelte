<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- OTA upload password editor over a USB Improv session (core, generic).
  The value is lifted ({value, revealed}); the edit form is local. -->
<script>
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Button } from '$lib/components/ui/button'
  import { Input } from '$lib/components/ui/input'
  import { KeyRound } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  let { session, ota = { value: null, revealed: false }, onOtaChange = () => {} } = $props()

  // Override field hidden until "Change password".
  let otaEditing = $state(false)
  let otaNew = $state('')
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

  async function setOta() {
    await run(async () => {
      await session.setOtaPassword(otaNew)
      otaNew = ''
      const pw = await session.getOtaPassword()
      onOtaChange({ value: pw, revealed: false })
      otaEditing = false
      toast.success('OTA password updated.')
    })
  }

  function changeOta() {
    otaEditing = true
  }

  function cancelOtaEdit() {
    otaEditing = false
    otaNew = ''
  }
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <KeyRound class="size-4" />
      OTA password
    </Card.Title>
    <Card.Description>Required for wireless uploads. Stored on the device.</Card.Description>
  </Card.Header>
  <Card.Content class="space-y-4">
    <div class="flex flex-wrap items-center gap-3">
      <p class="text-sm font-mono">
        <span class="text-muted-foreground">Current: </span>
        {#if ota.value === null}
          <span class="text-muted-foreground">unreadable (old firmware?)</span>
        {:else if ota.value === ''}
          <span class="text-muted-foreground">none — uploads are open</span>
        {:else if ota.revealed}
          {ota.value}
          <Button size="sm" variant="ghost" onclick={() => onOtaChange({ ...ota, revealed: false })}
            >Hide</Button
          >
        {:else}
          {'•'.repeat(Math.min(ota.value.length, 16))}
          <Button size="sm" variant="ghost" onclick={() => onOtaChange({ ...ota, revealed: true })}
            >Show</Button
          >
        {/if}
      </p>
      {#if !otaEditing}
        <Button size="sm" variant="outline" onclick={changeOta} disabled={busy}>
          Change password
        </Button>
      {/if}
    </div>
    {#if otaEditing}
      <div class="space-y-2">
        <label class="text-sm text-muted-foreground" for="setup-ota-password">New password</label>
        <Input
          id="setup-ota-password"
          type="password"
          bind:value={otaNew}
          placeholder="Empty clears it (uploads open)"
          disabled={busy}
        />
      </div>
      <div class="flex flex-col gap-2">
        <LoadingButton
          class="w-full"
          variant="outline"
          onclick={setOta}
          disabled={busy}
          loading={busy}
          loadingLabel="Saving..."
          icon={KeyRound}
        >
          Set password
        </LoadingButton>
        <Button size="sm" variant="ghost" onclick={cancelOtaEdit} disabled={busy}>Cancel</Button>
      </div>
    {/if}
  </Card.Content>
</Card.Root>
