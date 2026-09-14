<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import { onDestroy } from 'svelte'
  import { slide } from 'svelte/transition'
  import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
  import { Terminal, X, Trash2 } from 'lucide-svelte'

  let open = $state(false)
  let logs = $state([])
  let logUnsub = $state(null)
  let logContainer = $state(null)
  let autoScroll = $state(true)

  const MAX_LOGS = 200

  function fmtClock(ms) {
    return new Date(ms).toTimeString().slice(0, 8)
  }

  function fmtUptime(ms) {
    if (!ms) return ''
    const s = ms / 1000
    if (s < 60) return `${s.toFixed(1)}s`
    return `${Math.floor(s / 60)}m${Math.round(s % 60)}s`
  }

  function parseLogEntry(raw) {
    const match = raw.match(/^\x1b\[[\d;]*m([VDIWE]) \((\d+)\) ([^:]+): (.+)\x1b\[0m$/)
    if (match) {
      return { level: match[1], tag: match[3], message: match[4], time: parseInt(match[2]) }
    }
    const plain = raw.match(/^([VDIWE]) \((\d+)\) ([^:]+): (.+)$/)
    if (plain) {
      return { level: plain[1], tag: plain[3], message: plain[4], time: parseInt(plain[2]) }
    }
    return { level: 'I', tag: '', message: raw, time: 0 }
  }

  function levelColor(level) {
    switch (level) {
      case 'E':
        return 'text-red-400'
      case 'W':
        return 'text-amber-400'
      case 'I':
        return 'text-green-400'
      case 'D':
        return 'text-sky-400'
      case 'V':
        return 'text-zinc-500'
      default:
        return 'text-zinc-400'
    }
  }

  function levelLabel(level) {
    switch (level) {
      case 'E':
        return 'ERROR'
      case 'W':
        return 'WARN '
      case 'I':
        return 'INFO '
      case 'D':
        return 'DEBUG'
      case 'V':
        return 'TRACE'
      default:
        return level + '    '
    }
  }

  function toggle() {
    if (open) {
      closeConsole()
    } else {
      openConsole()
    }
  }

  function openConsole() {
    open = true
    startLogs()
  }

  function closeConsole() {
    open = false
    stopLogs()
  }

  function startLogs() {
    sendMessage({ type: 'log_start' })
    logUnsub = onMessageType('log', (data) => {
      if (data.message) {
        const entry = parseLogEntry(data.message)
        // Reception time on this client (the ESP uptime is in entry.time).
        entry.received = fmtClock(Date.now())
        logs = [...logs.slice(-MAX_LOGS + 1), entry]
      }
    })
  }

  function stopLogs() {
    sendMessage({ type: 'log_stop' })
    if (logUnsub) {
      logUnsub()
      logUnsub = null
    }
  }

  function clear() {
    logs = []
  }

  function onScroll() {
    if (!logContainer) return
    const el = logContainer
    autoScroll = el.scrollTop + el.clientHeight >= el.scrollHeight - 20
  }

  $effect(() => {
    if (autoScroll && logContainer) {
      logContainer.scrollTop = logContainer.scrollHeight
    }
  })

  // Fixed panel overlays the page bottom: pad the page by the panel
  // height while open so scrolled-to-end content lands above it.
  $effect(() => {
    document.body.style.paddingBottom = open ? '40vh' : ''
    return () => {
      document.body.style.paddingBottom = ''
    }
  })

  onDestroy(() => {
    if (open) stopLogs()
  })
</script>

<!-- Stealth debug toggle, docked bottom-right. Rides the console panel edge when open. -->
<div
  class="fixed z-50 flex flex-col items-center cursor-pointer select-none transition-[bottom] duration-300"
  style="right: 6px; bottom: {open ? '40vh' : '0'};"
  role="button"
  aria-label="Debug console"
  aria-pressed={open}
  tabindex="0"
  onclick={toggle}
  onkeydown={(e) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault()
      toggle()
    }
  }}
>
  <div
    class="flex size-8 items-center justify-center rounded-t-md bg-white/80 text-zinc-800 shadow-[0_0_12px_2px_color-mix(in_oklab,var(--color-primary)_45%,transparent)] hover:bg-zinc-100 hover:text-zinc-950 transition-colors dark:bg-zinc-800/40 dark:text-zinc-400 dark:hover:bg-zinc-700/60 dark:hover:text-zinc-200"
  >
    <Terminal class="size-4" />
  </div>
</div>

{#if open}
  <div
    class="fixed bottom-0 left-0 right-0 z-50 bg-zinc-900 text-zinc-100 shadow-2xl border-t border-zinc-700 flex flex-col"
    style="height: 40vh;"
    transition:slide={{ duration: 250 }}
  >
    <div class="flex items-center justify-between px-4 py-2 border-b border-zinc-700 shrink-0">
      <div class="flex items-center gap-2 text-sm font-medium">
        <Terminal class="size-4" />
        <span>Device Logs</span>
        <span class="text-xs text-zinc-500">({logs.length})</span>
      </div>
      <div class="flex items-center gap-1">
        <button
          onclick={clear}
          class="p-1.5 rounded hover:bg-zinc-700 text-zinc-400 hover:text-zinc-200 transition-colors cursor-pointer"
          title="Clear logs"
        >
          <Trash2 class="size-4" />
        </button>
        <button
          onclick={closeConsole}
          class="p-1.5 rounded hover:bg-zinc-700 text-zinc-400 hover:text-zinc-200 transition-colors cursor-pointer"
          title="Close console"
        >
          <X class="size-4" />
        </button>
      </div>
    </div>

    <div
      bind:this={logContainer}
      onscroll={onScroll}
      class="flex-1 overflow-y-auto font-mono text-xs leading-relaxed p-3"
    >
      {#if logs.length === 0}
        <div class="text-zinc-600 italic">Waiting for logs...</div>
      {/if}
      {#each logs as entry}
        <div class="flex gap-2 py-px">
          <span class="text-zinc-600 shrink-0 tabular-nums">{entry.received}</span>
          <span class="text-zinc-600/70 shrink-0 tabular-nums">{fmtUptime(entry.time)}</span>
          <span class="{levelColor(entry.level)} shrink-0 select-none font-bold"
            >{levelLabel(entry.level)}</span
          >
          {#if entry.tag}
            <span class="text-zinc-500 shrink-0">[{entry.tag}]</span>
          {/if}
          <span class="text-zinc-300 break-all">{entry.message}</span>
        </div>
      {/each}
    </div>
  </div>
{/if}
