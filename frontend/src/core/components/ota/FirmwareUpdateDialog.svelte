<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Firmware update options (core, generic): firmware file drop space,
  web update, web files drop space. Normal dismissable dialog — the
  blur-lock only starts with progress (see UpdateProgressDialog). -->
<script>
  import { Button } from '$lib/components/ui/button'
  import { Label } from '$lib/components/ui/label'
  import * as Dialog from '$lib/components/ui/dialog'
  import { Select, SelectTrigger, SelectContent, SelectItem } from '$lib/components/ui/select'
  import { FileUp, Globe, Wifi } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  import { settingsState } from 'src/core/lib/settings.svelte.js'
  import {
    firmwareUpdateState,
    checkForUpdate,
    startWebUpdate,
    postFirmwareFile,
    openBlocker,
    waitForReboot,
  } from 'src/core/lib/firmwareUpdate.svelte.js'
  import PasswordDialog from 'src/core/components/ota/PasswordDialog.svelte'
  import UploadDialog from 'src/core/components/ota/UploadDialog.svelte'

  let { open = $bindable(false) } = $props()

  // Firmware file (.bin) picked from the shared drop space below.
  let fileInputRef = $state(null)
  let dragOver = $state(false)
  let pendingFile = $state(null)
  let password = $state('')
  let showPasswordDialog = $state(false)
  let passwordDialogRef

  // Web files (SPIFFS) reuse the stock uploader.
  let spiffsFiles = $state(null)

  function pickInstallFiles() {
    fileInputRef?.click()
  }

  function takeInstallFiles(list) {
    const files = Array.from(list || [])
    if (files.length === 0) return
    const bins = files.filter((f) => /\.bin$/i.test(f.name))
    const rest = files.filter((f) => !/\.bin$/i.test(f.name))
    if (bins.length > 1) {
      toast.error('One firmware file at a time — taking the first.')
    }
    if (bins.length > 0) {
      pendingFile = bins[0]
    }
    if (rest.length > 0) {
      spiffsFiles = rest
    }
  }

  function uploadPendingFile() {
    const file = pendingFile
    if (!file) return
    if (settingsState.ota?.requiresPassword && !password) {
      showPasswordDialog = true
      return
    }
    pendingFile = null
    openBlocker('upload', { file: file.name })
    postFirmwareFile(file, password, (pct) => {
      if (firmwareUpdateState.blocking) {
        firmwareUpdateState.blocking = { ...firmwareUpdateState.blocking, pct }
      }
    }).then(
      () => {
        if (firmwareUpdateState.blocking) {
          firmwareUpdateState.blocking = { ...firmwareUpdateState.blocking, pct: 100 }
        }
        waitForReboot()
      },
      (e) => {
        if (e.message.includes('Authentication failed')) {
          firmwareUpdateState.blocking = null
          showPasswordDialog = true
          passwordDialogRef?.setError(e.message)
        } else {
          firmwareUpdateState.blocking = null
          toast.error(e.message)
        }
      },
    )
  }

  function handlePasswordSubmit(submittedPassword) {
    password = submittedPassword
    showPasswordDialog = false
    uploadPendingFile()
  }

  function handlePasswordCancel() {
    showPasswordDialog = false
    password = ''
    pendingFile = null
  }
</script>

<Dialog.Root bind:open>
  <Dialog.Content class="sm:max-w-md">
    <Dialog.Header>
      <Dialog.Title>Firmware update</Dialog.Title>
    </Dialog.Header>

    <div class="space-y-6">
      <div class="space-y-2">
        <p class="text-sm font-medium flex items-center gap-2">
          <FileUp class="size-4" /> Install files
        </p>
        <p class="text-xs text-muted-foreground">
          Firmware (.bin) flashes the chip; anything else refreshes the web UI files.
        </p>
        <!-- svelte-ignore a11y_click_events_have_key_events, a11y_no_static_element_interactions -->
        <div
          onclick={pickInstallFiles}
          ondragover={(e) => e.preventDefault()}
          ondragenter={(e) => {
            e.preventDefault()
            dragOver = true
          }}
          ondragleave={(e) => {
            e.preventDefault()
            dragOver = false
          }}
          ondrop={(e) => {
            e.preventDefault()
            dragOver = false
            takeInstallFiles(e.dataTransfer.files)
          }}
          class="flex cursor-pointer flex-col items-center justify-center rounded-lg border-2 border-dashed p-6 text-center transition-colors {dragOver
            ? 'border-primary bg-primary/10'
            : 'border-border text-muted-foreground hover:bg-accent'}"
        >
          <FileUp class="mb-2 size-6" />
          <p class="text-sm">Drop files here, or click to pick</p>
        </div>
        <input
          bind:this={fileInputRef}
          type="file"
          class="sr-only"
          multiple
          onchange={(e) => {
            takeInstallFiles(e.target.files)
            e.target.value = ''
          }}
        />
        {#if pendingFile}
          <p class="text-sm">
            Firmware: <code class="font-mono">{pendingFile.name}</code>
            <Button size="sm" variant="ghost" onclick={uploadPendingFile}>Upload</Button>
          </p>
        {/if}
      </div>

      <div class="space-y-2">
        <p class="text-sm font-medium flex items-center gap-2">
          <Globe class="size-4" /> Update from the web
        </p>
        <p class="text-xs text-muted-foreground">
          Signed releases only — the device verifies the signature before installing.
        </p>
        {#if firmwareUpdateState.checking && !firmwareUpdateState.status}
          <p class="text-sm text-muted-foreground">Checking releases…</p>
        {:else if firmwareUpdateState.status?.error}
          <div
            class="flex items-start gap-3 rounded-lg border border-dashed border-border p-4 text-sm"
          >
            <Wifi class="mt-0.5 size-4 shrink-0 text-muted-foreground" />
            <div class="space-y-1">
              <p class="font-medium">Couldn't reach the update server</p>
              <p class="text-xs text-muted-foreground">
                {#if /404/.test(firmwareUpdateState.status.error)}
                  No firmware releases are published yet — check back later.
                {:else if /no release key/i.test(firmwareUpdateState.status.error)}
                  This firmware wasn't built for web updates. Install a release build instead.
                {:else}
                  Make sure the device is online and can reach the internet, then try again. ({firmwareUpdateState
                    .status.error})
                {/if}
              </p>
              <Button size="sm" variant="ghost" onclick={checkForUpdate}>Retry</Button>
            </div>
          </div>
        {:else if firmwareUpdateState.status}
          <div class="flex items-center justify-between rounded-lg border p-3">
            <p class="text-sm">
              <code class="font-mono">{firmwareUpdateState.status.current}</code>
              {#if firmwareUpdateState.status.available}
                <span class="text-muted-foreground"> → </span>
                <code class="font-mono">{firmwareUpdateState.status.latest}</code>
              {:else}
                <span class="text-muted-foreground"> — up to date</span>
              {/if}
            </p>
            {#if firmwareUpdateState.status.available}
              <Button size="sm" onclick={startWebUpdate} class="relative">
                Install
                <span class="absolute -top-0.5 -right-0.5 size-2 rounded-full bg-red-500"></span>
              </Button>
            {/if}
          </div>
        {/if}
      </div>
    </div>
  </Dialog.Content>
</Dialog.Root>

<PasswordDialog
  bind:this={passwordDialogRef}
  open={showPasswordDialog}
  onSubmit={handlePasswordSubmit}
  onCancel={handlePasswordCancel}
/>

{#if spiffsFiles}
  <UploadDialog files={spiffsFiles} onUploadComplete={() => (spiffsFiles = null)} />
{/if}
