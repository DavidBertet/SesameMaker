<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- NTP server + timezone editor (core, generic). Timezone is picked by
  IANA name from the full list and translated to the POSIX rule the
  firmware's setenv(TZ) understands; raw POSIX still works as escape hatch. -->
<script>
  import { onMount, onDestroy } from 'svelte'
  import { tick } from 'svelte'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import * as Card from '$lib/components/ui/card'
  import { Input } from '$lib/components/ui/input'
  import { Label } from '$lib/components/ui/label'
  import { Button } from '$lib/components/ui/button'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import * as Command from '$lib/components/ui/command/index.js'
  import * as Popover from '$lib/components/ui/popover/index.js'
  import { cn } from '$lib/utils.js'

  import { Info, Save } from 'lucide-svelte'
  import CheckIcon from '@lucide/svelte/icons/check'
  import ChevronsUpDownIcon from '@lucide/svelte/icons/chevrons-up-down'
  import { toast } from 'svelte-sonner'

  import { settingsState, saveSettings } from 'src/core/lib/settings.svelte.js'
  import { TIMEZONES, TIMEZONE_NAMES } from 'src/core/lib/timezones.js'

  // Time form holds the IANA name (or a raw POSIX string as escape hatch);
  // the device only understands POSIX, translated on save.
  let timeForm = $state({ ntp_server: '', tz: '' })
  let timeSaving = $state(false)
  // Set on any manual edit: stops the auto-sync below from snap-backing
  // the form to the last device value mid-edit. Cleared on save.
  let timeEdited = $state(false)
  let browserTz = $state('')
  try {
    browserTz = Intl.DateTimeFormat().resolvedOptions().timeZone || ''
  } catch {
    browserTz = ''
  }

  function tzToPosix(name) {
    const t = (name || '').trim()
    return TIMEZONES[t] || t
  }

  function posixToName(posix) {
    return TIMEZONE_NAMES.find((n) => TIMEZONES[n] === posix) || posix || ''
  }

  let tzOpen = $state(false)
  let tzTriggerRef = $state(null)

  // Command score: separators are interchangeable ("america los" matches
  // "America/Los_Angeles"). Prefix hits rank above substring hits.
  function tzFilter(value, search) {
    const norm = (s) => s.toLowerCase().replace(/[/_-]/g, ' ').trim()
    const q = norm(search)
    if (!q) return 1
    const v = norm(value)
    if (v.startsWith(q)) return 1
    if (v.includes(q)) return 0.5
    return 0
  }
  // Refocus the trigger after picking so keyboard users keep their place.
  function closeTzAndFocusTrigger() {
    tzOpen = false
    tick().then(() => {
      tzTriggerRef?.focus()
    })
  }

  function syncTimeForm() {
    const t = settingsState.time
    if (!t) return
    // No reads of timeForm here: this runs inside an $effect, and reading
    // form state would re-trigger it on every keystroke/selection.
    timeForm.ntp_server = t.ntp_server || ''
    // Prefer the stored display name (cities share POSIX rules); fall back
    // to reverse-mapping the rule itself.
    timeForm.tz = t.tz_name || posixToName(t.tz || '')
  }

  function handleTimeSaved(data) {
    timeSaving = false
    timeEdited = false
    if (data.success) {
      toast.success('Time settings saved')
    } else {
      toast.error(data.message || 'Failed to save time settings')
      // Re-sync in case the server normalized anything.
      sendMessage({ type: 'get_settings' })
    }
  }

  function submitTime() {
    if (!timeForm.ntp_server.trim()) {
      toast.error('NTP server cannot be empty.')
      return
    }
    const tz = tzToPosix(timeForm.tz)
    if (!tz) {
      toast.error('Pick a timezone from the list.')
      return
    }
    timeSaving = true
    saveSettings({
      time: { ntp_server: timeForm.ntp_server.trim(), tz, tz_name: timeForm.tz.trim() },
    })
  }

  function useBrowserTz() {
    if (browserTz && TIMEZONES[browserTz]) {
      timeForm.tz = browserTz
      timeEdited = true
    } else {
      toast.info('Browser timezone is not in the list — pick the closest one.')
    }
  }

  // Sync the time form from the common settings (initial load and the
  // broadcast that follows a save). Skipped while saving or mid-edit so
  // user input is never clobbered.
  $effect(() => {
    if (settingsState.time && !timeSaving && !timeEdited) syncTimeForm()
  })

  let unsubs = $state([])
  onMount(() => {
    const u1 = onMessageType('settings_saved', handleTimeSaved)
    unsubs = [u1]
    return () => {
      unsubs.forEach((u) => u())
    }
  })

  onDestroy(() => {
    unsubs.forEach((u) => u())
  })
</script>

<Card.Root class="mt-6">
  <Card.Header class="pb-3">
    <Card.Title class="text-lg flex items-center gap-2">
      <Info class="size-4" />
      Time
    </Card.Title>
    <Card.Description
      >NTP info for timestamps and logs. (works only while connected to Wifi)</Card.Description
    >
  </Card.Header>

  <Card.Content class="space-y-4">
    {#if !settingsState.time}
      <Skeleton class="h-10 w-full rounded-md" />
    {:else}
      <div class="grid grid-cols-1 sm:grid-cols-2 gap-4">
        <div class="space-y-2">
          <Label for="time-server">NTP server</Label>
          <Input
            id="time-server"
            bind:value={timeForm.ntp_server}
            oninput={() => (timeEdited = true)}
            placeholder="pool.ntp.org"
          />
        </div>
        <div class="space-y-2">
          <Label>Timezone</Label>
          <div class="flex gap-2">
            <div class="min-w-0 flex-1">
              <Popover.Root bind:open={tzOpen}>
                <Popover.Trigger bind:ref={tzTriggerRef}>
                  {#snippet child({ props })}
                    <Button
                      {...props}
                      variant="outline"
                      class="w-full justify-between overflow-hidden font-normal"
                      role="combobox"
                      aria-expanded={tzOpen}
                    >
                      <span class="truncate">{timeForm.tz || 'Select a timezone…'}</span>
                      <ChevronsUpDownIcon class="shrink-0 opacity-50" />
                    </Button>
                  {/snippet}
                </Popover.Trigger>
                <Popover.Content class="p-0">
                  <Command.Root filter={tzFilter}>
                    <Command.Input placeholder="Search timezones…" />
                    <Command.List>
                      <Command.Empty>No timezone found.</Command.Empty>
                      <Command.Group value="timezones">
                        {#each TIMEZONE_NAMES as n (n)}
                          <Command.Item
                            value={n}
                            onSelect={() => {
                              timeForm.tz = n
                              timeEdited = true
                              closeTzAndFocusTrigger()
                            }}
                          >
                            <CheckIcon class={cn(timeForm.tz !== n && 'text-transparent')} />
                            {n}
                          </Command.Item>
                        {/each}
                      </Command.Group>
                    </Command.List>
                  </Command.Root>
                </Popover.Content>
              </Popover.Root>
            </div>
            {#if browserTz && TIMEZONES[browserTz]}
              <Button variant="outline" onclick={useBrowserTz} disabled={timeSaving}>
                Use mine
              </Button>
            {/if}
          </div>
        </div>
      </div>

      <LoadingButton
        class="w-full"
        onclick={submitTime}
        disabled={timeSaving || !settingsState.time}
        loading={timeSaving}
        loadingLabel="Saving..."
        icon={Save}
      >
        Save time settings
      </LoadingButton>
    {/if}
  </Card.Content>
</Card.Root>
