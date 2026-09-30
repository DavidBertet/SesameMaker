<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- Install firmware from release binaries over WebSerial (core, generic).
  Product wiring comes via props; the burn itself lives in core/lib/firmware.js.
  onInstalled fires with the flashed (released) port so the host can hand it
  straight to a setup session without a second picker. -->
<script>
  import { onMount } from 'svelte'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Badge } from '$lib/components/ui/badge'
  import { Button } from '$lib/components/ui/button'
  import { Switch } from '$lib/components/ui/switch'
  import { Label } from '$lib/components/ui/label'
  import { Progress } from '$lib/components/ui/progress'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Select, SelectTrigger, SelectContent, SelectItem } from '$lib/components/ui/select'
  import { Download, PackageCheck } from 'lucide-svelte'
  import { toast } from 'svelte-sonner'

  import { listFirmwareReleases, pickLatestRelease, installRelease } from 'src/core/lib/firmware.js'

  let {
    owner,
    repo,
    // One firmware per chip: [{id, label, expectedChip, manifestName}].
    // manifestName is stable across releases (version lives inside).
    variants = [],
    onInstalled = () => {},
  } = $props()

  let releases = $state([])
  let releasesLoading = $state(true)
  let releasesError = $state('')
  let pickedTag = $state('')
  let pickedVariantId = $state(variants[0]?.id || '')
  let eraseAll = $state(false)
  let flashBusy = $state(false)
  let flashProgress = $state(null) // {phase, file, written, total}
  let flashDone = $state(false)

  const pickedRelease = $derived(releases.find((r) => r.tag === pickedTag) || null)
  const pickedVariant = $derived(variants.find((v) => v.id === pickedVariantId) || null)
  const flashPct = $derived(
    flashProgress && flashProgress.phase === 'flash' && flashProgress.total
      ? Math.round((flashProgress.written / flashProgress.total) * 100)
      : 0,
  )

  onMount(() => {
    loadReleases()
  })

  async function loadReleases() {
    releasesLoading = true
    releasesError = ''
    try {
      releases = await listFirmwareReleases({ owner, repo })
      const latest = pickLatestRelease(releases)
      if (latest) pickedTag = latest.tag
      else releasesError = 'No releases published yet.'
    } catch (e) {
      releasesError = e.message || String(e)
    } finally {
      releasesLoading = false
    }
  }

  // Burn the picked release, then hand the released port back. A reboot
  // re-enumeration kills the port object; the host then falls back to a
  // manual connect.
  async function install() {
    if (!pickedTag || !pickedVariant || flashBusy) return
    flashBusy = true
    flashDone = false
    flashProgress = null
    try {
      const { port } = await installRelease({
        owner,
        repo,
        tag: pickedTag,
        manifestName: pickedVariant.manifestName,
        expectedChip: pickedVariant.expectedChip || null,
        eraseAll,
        onProgress: (p) => (flashProgress = p),
      })
      flashDone = true
      toast.success(`Installed ${pickedTag} — waiting for the device to boot…`)
      await new Promise((r) => setTimeout(r, 3000))
      await port.open({ baudRate: 115200 })
      await onInstalled(port)
      toast.success('Connected — finish the setup below.')
    } catch (e) {
      if (e.name === 'NotFoundError') {
        toast.error('No port picked.')
      } else if (/re-enumerat|open|lost|detach|Gone/i.test(e.message || '')) {
        toast.info('Device rebooted — hit Connect and pick it again to finish setup.')
      } else {
        toast.error(e.message || String(e))
      }
      flashDone = false
    } finally {
      flashBusy = false
      flashProgress = null
    }
  }
</script>

<Card.Root>
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <Download class="size-4" />
      New board? Install the firmware
    </Card.Title>
    <Card.Description
      >Pick a release, plug the board in, burn. Takes about a minute.</Card.Description
    >
  </Card.Header>
  <Card.Content class="space-y-4">
    {#if releasesLoading}
      <Skeleton class="h-10 w-full rounded-md" />
    {:else if releasesError}
      <p class="text-sm text-muted-foreground">
        {releasesError}
        <Button size="sm" variant="ghost" onclick={loadReleases}>Retry</Button>
      </p>
    {:else}
      {#if variants.length > 1}
        <div class="space-y-2">
          <Label class="font-medium">Board</Label>
          <Select
            type="single"
            value={pickedVariantId}
            onValueChange={(v) => (pickedVariantId = v)}
            disabled={flashBusy}
          >
            <SelectTrigger aria-label="Board variant">
              {pickedVariant ? pickedVariant.label : 'Select your board…'}
            </SelectTrigger>
            <SelectContent>
              {#each variants as v}
                <SelectItem value={v.id}>{v.label}</SelectItem>
              {/each}
            </SelectContent>
          </Select>
        </div>
      {/if}
      <div class="space-y-2">
        <Label class="font-medium">Release</Label>
        <Select
          type="single"
          value={pickedTag}
          onValueChange={(v) => (pickedTag = v)}
          disabled={flashBusy}
        >
          <SelectTrigger aria-label="Firmware release">
            {pickedRelease ? pickedRelease.name : 'Select a version…'}
            {#if pickedRelease?.prerelease}
              <Badge variant="outline">beta</Badge>
            {/if}
          </SelectTrigger>
          <SelectContent>
            {#each releases as r}
              <SelectItem value={r.tag}>
                {r.name}{r.prerelease ? ' (beta)' : ''}{r.publishedAt ? ` — ${r.publishedAt}` : ''}
              </SelectItem>
            {/each}
          </SelectContent>
        </Select>
      </div>
    {/if}
    <div class="flex items-center justify-between rounded-lg border p-3">
      <div class="space-y-0.5">
        <Label class="font-medium">Erase everything first</Label>
        <p class="text-xs text-muted-foreground">
          Fresh start: wipes Wi-Fi, settings and pairings.
        </p>
      </div>
      <Switch
        checked={eraseAll}
        onCheckedChange={(v) => (eraseAll = v)}
        aria-label="Erase flash before installing"
      />
    </div>
    {#if flashProgress}
      <div class="space-y-2">
        <p class="text-sm text-muted-foreground">
          {#if flashProgress.phase === 'download'}
            Downloading {flashProgress.file}…
          {:else}
            Flashing {flashProgress.file} — {flashPct}%
          {/if}
        </p>
        {#if flashProgress.phase === 'flash'}
          <Progress value={flashPct} />
        {/if}
      </div>
    {/if}
    <LoadingButton
      class="w-full"
      onclick={install}
      disabled={flashBusy || !pickedTag}
      loading={flashBusy}
      loadingLabel="Installing…"
      icon={Download}
    >
      Connect & install {pickedTag || ''}
    </LoadingButton>
    {#if flashDone}
      <p class="text-sm text-muted-foreground flex items-center gap-2">
        <PackageCheck class="size-4" /> Installed — finishing setup below.
      </p>
    {/if}
  </Card.Content>
</Card.Root>
