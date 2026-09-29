<!-- Copyright (c) 2026 David Bertet. Licensed under the MIT License. -->

<!-- USB getting started (app): board intro, install-from-release or connect
  an existing board, then the generic core setup cards. Demo builds only
  (dev server + GitHub Pages) — never shipped on the device itself. -->
<script>
  import { onDestroy } from 'svelte'
  import SectionHeader from 'src/core/components/common/SectionHeader.svelte'
  import LoadingButton from 'src/core/components/common/LoadingButton.svelte'
  import InstallCard from 'src/core/components/setup/InstallCard.svelte'
  import SetupConnectionCard from 'src/core/components/setup/SetupConnectionCard.svelte'
  import SetupInfoCard from 'src/core/components/setup/SetupInfoCard.svelte'
  import SetupWifiCard from 'src/core/components/setup/SetupWifiCard.svelte'
  import SetupOtaCard from 'src/core/components/setup/SetupOtaCard.svelte'
  import SetupResetsCard from 'src/core/components/setup/SetupResetsCard.svelte'
  import { ImprovSession, requestDevicePort } from 'src/core/lib/serialImprov.js'
  import { attachUsbConsole, detachUsbConsole } from 'src/core/lib/usbConsole.svelte.js'
  import { toast } from 'svelte-sonner'
  import * as Card from '$lib/components/ui/card'
  import { PlugZap, Usb } from 'lucide-svelte'

  // Release binaries come from GitHub releases (see .github/workflows/release.yml).
  // One firmware per chip — the picker offers every variant the release builds.
  const FIRMWARE_VARIANTS = [
    {
      id: 'esp32c6',
      label: 'ESP32-C6 (Zigbee)',
      expectedChip: 'ESP32-C6',
      manifestNameFor: (tag) => `sesamemaker-esp32c6-${tag}-manifest.json`,
    },
    {
      id: 'esp32c3',
      label: 'ESP32-C3',
      expectedChip: 'ESP32-C3',
      manifestNameFor: (tag) => `sesamemaker-esp32c3-${tag}-manifest.json`,
    },
    {
      id: 'esp32s3',
      label: 'ESP32-S3',
      expectedChip: 'ESP32-S3',
      manifestNameFor: (tag) => `sesamemaker-esp32s3-${tag}-manifest.json`,
    },
  ]

  const supported = typeof navigator !== 'undefined' && 'serial' in navigator

  let session = $state(null)
  let portLabel = $state('')
  let info = $state(null) // [firmware, version, chip, name]
  let ota = $state({ value: null, revealed: false })
  let net = $state(null) // {flags, urls}
  let busy = $state(false)

  async function connect() {
    busy = true
    try {
      await connectWithPort(await requestDevicePort())
    } catch (e) {
      toast.error(e.message || String(e))
      detachUsbConsole()
      session = null
    } finally {
      busy = false
    }
  }

  // Shared tail: label the port, attach the console, read identity. Used by
  // both the existing-device path and right after a fresh flash (same port,
  // reopened at Improv baud).
  async function connectWithPort(port) {
    portLabel = `USB${port.getInfo ? ` (${port.getInfo().usbVendorId || '?'}:${port.getInfo().usbProductId || '?'})` : ''}`
    session = new ImprovSession(port)
    attachUsbConsole(session)
    ota = { value: null, revealed: false }
    await fetchInfos()
  }

  async function disconnect() {
    const s = session
    session = null
    detachUsbConsole()
    info = null
    ota = { value: null, revealed: false }
    net = null
    if (s) {
      try {
        await s.close()
      } catch {
        // already gone
      }
    }
  }

  onDestroy(() => {
    detachUsbConsole()
    if (session) {
      const s = session
      session = null
      s.close().catch(() => {})
    }
  })

  async function fetchInfos() {
    info = await session.getInfo()
    const pw = await session.getOtaPassword()
    ota = { value: pw, revealed: false }
    net = await session.getNetworkState()
  }

  async function refresh() {
    if (!session || busy) return
    busy = true
    try {
      await fetchInfos()
    } catch (e) {
      toast.error(e.message || String(e))
    } finally {
      busy = false
    }
  }
</script>

<SectionHeader
  title="Getting started"
  subtitle="Plug a board in over USB — install the firmware or set up an existing device, straight from your browser."
/>

{#if !supported}
  <Card.Root>
    <Card.Content class="pt-6">
      <p class="text-sm text-muted-foreground">
        WebSerial is not available here — it needs a Chromium-based browser over HTTPS or localhost.
      </p>
    </Card.Content>
  </Card.Root>
{:else}
  <div class="space-y-6">
    {#if !session}
      <Card.Root>
        <Card.Header class="pb-3">
          <Card.Title class="text-lg flex items-center gap-2">
            <Usb class="size-4" />
            What you need
          </Card.Title>
          <Card.Description>
            An ESP32 board with a USB port (C6, C3 or S3 — pick yours below), a USB cable, and this
            page.
          </Card.Description>
        </Card.Header>
        <Card.Content>
          <ul class="grid grid-cols-1 sm:grid-cols-2 gap-2 text-sm">
            <li>🚪 Door control + live state from the wall bus</li>
            <li>📻 Zigbee (C6 boards), 🏠 HomeKit and MQTT bridge</li>
            <li>🔌 100% local — no cloud account</li>
            <li>💡 Opener light & remote lockout</li>
            <li>📶 Wi-Fi web UI + password-protected OTA</li>
            <li>🕵️ Protocol inspector for wiring checks</li>
          </ul>
        </Card.Content>
      </Card.Root>

      <div class="grid grid-cols-1 md:grid-cols-2 gap-6">
        <InstallCard
          owner="DavidBertet"
          repo="SesameMaker"
          variants={FIRMWARE_VARIANTS}
          onInstalled={connectWithPort}
        />

        <Card.Root>
          <Card.Header class="pb-3">
            <Card.Title class="text-lg flex items-center gap-2">
              <PlugZap class="size-4" />
              Board already flashed? Connect it
            </Card.Title>
            <Card.Description
              >Jump straight to Wi-Fi, OTA password and device info.</Card.Description
            >
          </Card.Header>
          <Card.Content>
            <LoadingButton
              class="w-full"
              onclick={connect}
              disabled={busy}
              loading={busy}
              loadingLabel="Connecting…"
              icon={PlugZap}
            >
              Connect device
            </LoadingButton>
          </Card.Content>
        </Card.Root>
      </div>
    {/if}

    {#if session}
      <SetupConnectionCard {portLabel} {busy} onRefresh={refresh} onDisconnect={disconnect} />
      <SetupInfoCard {info} />
      <SetupWifiCard {session} {net} onNetChange={(v) => (net = v)} />
      <SetupOtaCard {session} {ota} onOtaChange={(v) => (ota = v)} />
      <SetupResetsCard {session} onGone={disconnect} />
    {/if}
  </div>
{/if}
