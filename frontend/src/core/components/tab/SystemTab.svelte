<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onMount, onDestroy } from 'svelte'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import * as Card from '$lib/components/ui/card'
  import { Separator } from '$lib/components/ui/separator'
  import { Skeleton } from '$lib/components/ui/skeleton'
  import { Badge } from '$lib/components/ui/badge'
  import { cn } from '$lib/utils'

  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'

  let settingsUnsub = $state(null)
  let settings = $state([])
  let loading = $state(true)
  let scanPollTimeout = $state(null)

  // Task stack monitor: min-free floor + fill drift since boot, sampled on
  // device every 30 s. Polled here at the same cadence (slow-moving data).
  let taskmon = $state(null)
  let taskmonUnsub = $state(null)
  let taskmonPoll = $state(null)

  onMount(() => {
    settingsUnsub = onMessageType('system_info', (data) => {
      settings = data.settings
      loading = false

      scanPollTimeout = setTimeout(() => {
        sendMessage({ type: 'get_system_info' })
      }, 5000)
    })
    sendMessage({ type: 'get_system_info' })
    taskmonUnsub = onMessageType('taskmon', (data) => {
      taskmon = data
    })
    const pollTaskmon = () => {
      sendMessage({ type: 'get_taskmon' })
      taskmonPoll = setTimeout(pollTaskmon, 30000)
    }
    pollTaskmon()
    return () => {
      if (settingsUnsub) settingsUnsub()
      if (scanPollTimeout) clearTimeout(scanPollTimeout)
      if (taskmonUnsub) taskmonUnsub()
      if (taskmonPoll) clearTimeout(taskmonPoll)
    }
  })

  onDestroy(() => {
    if (settingsUnsub) settingsUnsub()
    if (scanPollTimeout) clearTimeout(scanPollTimeout)
    if (taskmonUnsub) taskmonUnsub()
    if (taskmonPoll) clearTimeout(taskmonPoll)
  })

  function formatKey(key) {
    return key
      .split('_')
      .map((word) => word[0].toUpperCase() + word.slice(1))
      .join(' ')
  }

  function formatValue(value) {
    if (typeof value === 'boolean') return value ? 'Yes' : 'No'
    if (typeof value === 'object') return JSON.stringify(value)
    return String(value)
  }
</script>

<SectionHeader title="System Information" subtitle="View device status and specifications" />

<div>
  {#if loading}
    <div class="space-y-8">
      {#each Array(2) as _}
        <div class="space-y-4">
          <Skeleton class="h-8 w-48" />
          <Separator />
          <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
            {#each Array(3) as _}
              <Card.Root>
                <Card.Header class="pb-3">
                  <Skeleton class="h-4 w-24" />
                </Card.Header>
                <Card.Content>
                  <Skeleton class="h-6 w-32" />
                </Card.Content>
              </Card.Root>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  {:else}
    <div class="space-y-8">
      {#each Object.entries(settings) as [sectionKey, sectionData]}
        <div class="space-y-4">
          <div class="flex items-center space-x-4">
            <Card.Title class="text-2xl font-semibold tracking-tight">
              {formatKey(sectionKey)}
            </Card.Title>
            <Separator class="flex-1" />
          </div>

          <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
            {#each Object.entries(sectionData) as [key, value]}
              <Card.Root class="transition-shadow hover:shadow-md">
                <Card.Header class="pb-3">
                  <Card.Description class="text-sm font-medium">
                    {formatKey(key)}
                  </Card.Description>
                </Card.Header>
                <Card.Content class="pt-0">
                  <div class="flex items-center space-x-2">
                    <p class="text-lg font-semibold break-words">
                      {formatValue(value)}
                    </p>
                  </div>
                </Card.Content>
              </Card.Root>
            {/each}
          </div>
        </div>
      {/each}
      {#if taskmon?.tasks?.length}
        <div class="space-y-4">
          <div class="flex items-center space-x-4">
            <Card.Title class="text-2xl font-semibold tracking-tight">Task Stacks</Card.Title>
            <Separator class="flex-1" />
          </div>
          <p class="text-sm text-muted-foreground">
            Min free words since boot + words consumed since monitoring began. A rising drift means
            the stack is filling up — grow that task's stack.
          </p>
          <div class="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-6">
            {#each [...taskmon.tasks].sort((a, b) => a.free_words - b.free_words) as task}
              <Card.Root
                class={cn(
                  'transition-shadow hover:shadow-md',
                  task.status === 'critical' && 'border-red-400',
                  task.status === 'watch' && 'border-yellow-400',
                )}
              >
                <Card.Header class="pb-3">
                  <div class="flex items-center justify-between">
                    <Card.Description class="text-sm font-medium font-mono">
                      {task.name}
                    </Card.Description>
                    <Badge
                      variant={task.status === 'critical' ? 'destructive' : 'default'}
                      class={task.status === 'ok'
                        ? 'bg-green-600 text-white border-transparent'
                        : task.status === 'watch'
                          ? 'bg-orange-500 text-white border-transparent'
                          : ''}
                    >
                      {task.status === 'ok' ? 'Healthy' : task.status}
                    </Badge>
                  </div>
                </Card.Header>
                <Card.Content class="pt-0">
                  <div class="flex items-center gap-4 text-sm">
                    <span><strong>{task.free_words}</strong> free</span>
                    <span class="text-muted-foreground">+{task.drift_words} since boot</span>
                    {#if task.rate_wph > 0}
                      <span class="text-muted-foreground">~{task.rate_wph}/h</span>
                    {/if}
                  </div>
                </Card.Content>
              </Card.Root>
            {/each}
          </div>
        </div>
      {/if}
    </div>
  {/if}
</div>
