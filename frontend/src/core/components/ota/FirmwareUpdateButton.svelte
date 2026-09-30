<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Header firmware-update button (core, generic): red dot when a signed
  release is newer, opens the options dialog. Progress lives in
  UpdateProgressDialog; state in core/lib/firmwareUpdate. -->
<script>
  import { onMount, onDestroy } from 'svelte'
  import { Button } from '$lib/components/ui/button'
  import { ArrowDownToLine } from 'lucide-svelte'

  import {
    firmwareUpdateState,
    checkForUpdate,
    initializeFirmwareUpdate,
  } from 'src/core/lib/firmwareUpdate.svelte.js'
  import FirmwareUpdateDialog from 'src/core/components/ota/FirmwareUpdateDialog.svelte'
  import UpdateProgressDialog from 'src/core/components/ota/UpdateProgressDialog.svelte'

  let { ...restProps } = $props()

  let unsubscribe = $state(null)

  const updateAvailable = $derived(!!firmwareUpdateState.status?.available)

  onMount(() => {
    unsubscribe = initializeFirmwareUpdate()
    return () => {
      unsubscribe?.()
    }
  })

  onDestroy(() => {
    unsubscribe?.()
  })
</script>

<div {...restProps}>
  <Button
    variant="ghost"
    size="icon"
    aria-label="Firmware update"
    onclick={() => {
      firmwareUpdateState.optionsOpen = true
      checkForUpdate()
    }}
    class="relative"
  >
    <ArrowDownToLine class="h-4 w-4 text-muted-foreground" />
    {#if updateAvailable}
      <span class="absolute top-1 right-1 size-2 rounded-full bg-red-500"></span>
    {/if}
  </Button>
</div>

<FirmwareUpdateDialog bind:open={firmwareUpdateState.optionsOpen} />
<UpdateProgressDialog />
