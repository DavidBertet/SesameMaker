<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<script>
  import SystemTab from 'src/core/components/tab/SystemTab.svelte'
  import DoorTab from 'src/app/components/tab/DoorTab.svelte'
  import PinInspectorTab from 'src/app/components/tab/PinInspectorTab.svelte'
  import ProtocolTab from 'src/app/components/tab/ProtocolTab.svelte'
  import WifiTab from 'src/core/components/tab/WifiTab.svelte'
  import DeviceTab from 'src/components/tab/DeviceTab.svelte'
  import WebsocketStatus from 'src/core/components/common/WebsocketStatus.svelte'
  import SystemStatus from 'src/core/components/nav/SystemStatus.svelte'
  import FirmwareUpdateButton from 'src/core/components/ota/FirmwareUpdateButton.svelte'
  import SidebarNav from 'src/core/components/nav/SidebarNav.svelte'
  import DarkModeToggle from 'src/core/components/common/DarkModeToggle.svelte'
  import DisconnectedOverlay from 'src/core/components/nav/DisconnectedOverlay.svelte'
  import DemoPill from 'src/core/components/common/DemoPill.svelte'
  import DebugConsole from 'src/core/components/common/DebugConsole.svelte'

  import { onMount, onDestroy } from 'svelte'
  import {
    wsState,
    connectWebSocket,
    closeWebSocket,
    sendMessage,
    onMessageType,
  } from 'src/core/lib/ws.svelte.js'
  import { initializeSettings } from 'src/core/lib/settings.svelte.js'

  import * as Sidebar from '$lib/components/ui/sidebar'
  import * as AlertDialog from '$lib/components/ui/alert-dialog'
  import Sonner from '$lib/components/ui/sonner/sonner.svelte'
  import { cn } from '$lib/utils'
  import { Wifi, Info, Heart } from 'lucide-svelte'
  import { DoorOpen, Activity, Radio, Cpu, Usb } from 'lucide-svelte'

  const isDev = import.meta.env.DEV
  // USB setup tab: demo builds only (dev server + GitHub Pages). Loaded via
  // dynamic import so the device bundle never includes it (see the
  // SetupTab-* exclusion in package.json build).
  const showSetupTab = import.meta.env.DEV || import.meta.env.MODE === 'github'
  let SetupTab = $state(null)

  let rawTab = $state(new URLSearchParams(window.location.search).get('tab') || 'door')
  // Backward compat: ?tab=mqtt now lives inside Device Settings.
  let activeTab = $state(rawTab === 'mqtt' ? 'device' : rawTab)
  let errorMessage = $state(null)
  let errorUnsub = $state(null)
  let settingsUnsub = $state(null)

  let delayDisconnectedState = $state(true)
  const shouldShowDisconnected = $derived(!wsState.isConnected && !delayDisconnectedState)

  let systemBannerVisible = $state(false)

  onMount(() => {
    if (showSetupTab) {
      import('src/app/components/tab/SetupTab.svelte').then((m) => (SetupTab = m.default))
    }
    let wsUrl
    if (isDev) {
      wsUrl = `ws://localhost:8080`
    } else {
      const wsHost = window.location.hostname
      const wsPort = window.location.port || '80'
      wsUrl = `ws://${wsHost}:${wsPort}/ws`
    }
    connectWebSocket(wsUrl)

    // Slight delay to avoid showing blurry animation on opening
    setTimeout(() => {
      delayDisconnectedState = false
    }, 500)

    sendMessage({ type: 'time_update', time: (Date.now() / 1000) | 0 })

    // Initialize settings
    settingsUnsub = initializeSettings()

    errorUnsub = onMessageType('error', (data) => {
      errorMessage = data.message || 'Unknow error'
    })

    return () => {
      if (errorUnsub) errorUnsub()
      if (settingsUnsub) settingsUnsub()
    }
  })

  onDestroy(() => {
    closeWebSocket()
    if (errorUnsub) errorUnsub()
    if (settingsUnsub) settingsUnsub()
  })

  // Retrieve current tab from URL
  $effect(() => {
    const url = new URL(window.location)
    url.searchParams.set('tab', activeTab)
    window.history.replaceState({}, '', url)
  })

  const baseTabs = [
    {
      id: 'door',
      label: 'Garage Door',
      icon: DoorOpen,
      component: DoorTab,
    },
    {
      id: 'protocol',
      label: 'Protocol',
      icon: Activity,
      component: ProtocolTab,
    },
    {
      id: 'device',
      label: 'Device Settings',
      icon: Radio,
      component: DeviceTab,
    },
    {
      id: 'wifi',
      label: 'WiFi Settings',
      icon: Wifi,
      component: WifiTab,
    },
    {
      id: 'system',
      label: 'System Info',
      icon: Info,
      component: SystemTab,
    },
    {
      id: 'inspector',
      label: 'Pin Inspector',
      icon: Cpu,
      component: PinInspectorTab,
    },
  ]

  const deviceTabs = baseTabs

  const localTabs = $derived(
    SetupTab ? [{ id: 'setup', label: 'USB Setup', icon: Usb, component: SetupTab }] : [],
  )

  const tabs = $derived([...deviceTabs, ...localTabs])

  function setActiveTab(tab) {
    activeTab = tab
  }

  const activeTabData = $derived(tabs.find((tab) => tab.id === activeTab))
</script>

{#if shouldShowDisconnected}
  <DisconnectedOverlay />
{/if}

<div
  class={cn(
    'transition-all duration-300',
    shouldShowDisconnected && 'blur-sm pointer-events-none select-none',
  )}
>
  <Sidebar.Provider>
    <Sidebar.Root class="border-r border-border/40">
      <Sidebar.Content
        class="bg-gradient-to-b from-violet-100/60 via-violet-50/30 to-white dark:from-violet-950 dark:via-violet-900/40 dark:to-stone-950"
      >
        <!-- Sidebar Header -->
        <div class="flex items-center">
          <div class="flex items-center gap-3 px-6 py-4 border-b border-border/40">
            <div
              class="flex items-center justify-center w-8 h-8 rounded-lg bg-primary text-primary-foreground shadow-sm"
            >
              <DoorOpen class="size-5" />
            </div>
            <div class="flex flex-col">
              <span class="font-semibold text-sm text-foreground">SesameMaker</span>
              <span class="text-xs text-muted-foreground">Garage Door Controller</span>
            </div>
          </div>

          <div class="flex flex-1 justify-end pe-3">
            <FirmwareUpdateButton class="inline sm:hidden" />
          </div>
        </div>

        <SidebarNav tabs={deviceTabs} {localTabs} {activeTab} onTabSelect={setActiveTab} />
      </Sidebar.Content>
    </Sidebar.Root>

    <main class="flex-1 flex flex-col min-h-screen">
      <SystemStatus {activeTab} />

      <div class="bg-background">
        <!-- Header -->
        <header
          class="sticky top-0 z-50 w-full border-b bg-background/95 backdrop-blur supports-[backdrop-filter]:bg-background/60"
        >
          <div class="flex h-14 items-center px-6">
            <div class="md:hidden flex items-center gap-4 mr-4">
              <Sidebar.Trigger />
              <div class="flex items-center gap-2">
                <div class="size-6 rounded bg-primary flex items-center justify-center">
                  <DoorOpen class="size-4 text-primary-foreground" />
                </div>
                <h1 class="text-lg font-semibold">SesameMaker</h1>
              </div>
            </div>

            <div class="flex flex-1 justify-end items-center gap-3">
              <DarkModeToggle />
              <FirmwareUpdateButton class="hidden sm:inline" />
              <WebsocketStatus />
            </div>
          </div>
        </header>

        <!-- Main Content -->
        <div class="flex-1 p-6">
          <div class="max-w-7xl mx-auto">
            {#if activeTabData}
              <activeTabData.component />
            {/if}
          </div>
        </div>

        <!-- Footer -->
        <footer>
          <div class="max-w-7xl mx-auto px-6 py-4">
            <div class="flex items-center justify-center text-sm text-muted-foreground">
              <span class="flex items-center gap-1">Built with <Heart class="size-3" /> by</span>
              <a
                href="https://david.bertet.fr"
                target="_blank"
                rel="noopener"
                class="ml-1 inline-flex items-center gap-1 text-primary hover:text-primary/80 transition-colors duration-200 hover:underline"
              >
                David Bertet
              </a>
            </div>
          </div>
        </footer>
      </div>
    </main>
  </Sidebar.Provider>

  <AlertDialog.Root bind:open={errorMessage}>
    <AlertDialog.Content>
      <AlertDialog.Header>
        <AlertDialog.Title>An error occured</AlertDialog.Title>
        <AlertDialog.Description>
          {errorMessage}
        </AlertDialog.Description>
      </AlertDialog.Header>
      <AlertDialog.Footer>
        <AlertDialog.Action
          onclick={() => {
            errorMessage = null
          }}
        >
          OK
        </AlertDialog.Action>
      </AlertDialog.Footer>
    </AlertDialog.Content>
  </AlertDialog.Root>
</div>

<Sonner />

{#if import.meta.env.MODE === 'github'}
  <div class="h-20"></div>
  <DemoPill live={activeTab === 'setup'} />
{/if}

<DebugConsole {activeTab} />
