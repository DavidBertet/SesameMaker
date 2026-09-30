<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Blocking update progress (core, generic): blurred lock from the first
  byte through reboot. Non-dismissable — the interface is locked while the
  device flashes. Closes on reconnect (see firmwareUpdate). -->
<script>
  import { Progress } from '$lib/components/ui/progress'
  import * as Dialog from '$lib/components/ui/dialog'
  import { ArrowDownToLine, LoaderCircle } from 'lucide-svelte'

  import { firmwareUpdateState } from 'src/core/lib/firmwareUpdate.svelte.js'

  const blocking = $derived(firmwareUpdateState.blocking)
</script>

<Dialog.Root open={!!blocking}>
  <Dialog.Overlay class="z-[110] bg-black/50 backdrop-blur-sm" />
  <Dialog.Content
    class="z-[111] sm:max-w-md [&_[data-dialog-close]]:hidden"
    escapeKeydownBehavior="ignore"
    interactOutsideBehavior="ignore"
  >
    <Dialog.Header>
      <Dialog.Title class="flex items-center gap-2">
        {#if blocking?.phase === 'install'}
          <LoaderCircle class="size-5 animate-spin" />
          Installing update — rebooting…
        {:else}
          <ArrowDownToLine class="size-5" />
          {blocking?.phase === 'upload' ? 'Uploading firmware…' : 'Downloading update…'}
        {/if}
      </Dialog.Title>
    </Dialog.Header>
    <div class="space-y-4">
      {#if blocking?.file}
        <p class="text-sm text-muted-foreground font-mono truncate">{blocking.file}</p>
      {/if}
      {#if blocking?.phase !== 'install'}
        <Progress value={blocking?.pct || 0} class="w-full" />
        <p class="text-center text-sm text-muted-foreground">{blocking?.pct || 0}%</p>
      {:else}
        <p class="text-sm text-muted-foreground">
          The device is rebooting into the new firmware. This dialog closes on reconnect.
        </p>
      {/if}
    </div>
  </Dialog.Content>
</Dialog.Root>
