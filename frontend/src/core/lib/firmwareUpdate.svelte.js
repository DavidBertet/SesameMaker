// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

import { sendMessage, onMessageType } from 'src/core/lib/ws.svelte.js'
import { toast } from 'svelte-sonner'

// Shared firmware-update state (core). The header button mounts twice
// (mobile + desktop, one CSS-hidden) — module state keeps a single machine.
// End of every successful flow is a page reload (like file uploads), so no
// state ever survives a reboot and nothing can stick.

export const firmwareUpdateState = $state({
  status: null, // update_status payload
  checking: false,
  blocking: null, // {phase: 'upload'|'download'|'install', pct, file} or null
  optionsOpen: false,
})

let subscribed = false

export function checkForUpdate() {
  firmwareUpdateState.checking = true
  sendMessage({ type: 'check_update' })
}

export function openBlocker(phase, extra = {}) {
  firmwareUpdateState.optionsOpen = false
  firmwareUpdateState.blocking = { phase, pct: 0, file: '', ...extra }
}

function closeBlocker(message, ok = true) {
  firmwareUpdateState.blocking = null
  if (message) {
    if (ok) toast.success(message)
    else toast.error(message)
  }
}

// Install confirmed: show the reboot phase, then reload the page (like file
// uploads). The fresh load re-checks from zero — no timers to manage, no
// reconnect watching, nothing that can stick.
export function waitForReboot() {
  if (firmwareUpdateState.blocking) {
    firmwareUpdateState.blocking = { ...firmwareUpdateState.blocking, phase: 'install' }
  }
  setTimeout(() => window.location.reload(), 8000)
}

export function startWebUpdate() {
  sendMessage({ type: 'start_update' })
  openBlocker('download')
}

// POST a .bin to the firmware flasher. Resolves on HTTP 200 (device flashes
// + reboots), rejects with a message otherwise (401 included).
export function postFirmwareFile(file, password, onXhrProgress) {
  return new Promise((resolve, reject) => {
    const xhttp = new XMLHttpRequest()
    xhttp.upload.onprogress = (e) => {
      if (e.lengthComputable && onXhrProgress) {
        onXhrProgress(Math.round((e.loaded * 100) / e.total))
      }
    }
    xhttp.onreadystatechange = () => {
      if (xhttp.readyState !== 4) return
      if (xhttp.status === 200) resolve()
      else if (xhttp.status === 401) reject(new Error('Authentication failed - invalid password'))
      else reject(new Error('Firmware upload failed.'))
    }
    xhttp.open('POST', '/upload/' + file.name, true)
    xhttp.setRequestHeader('X-OTA-Password', password)
    xhttp.send(file)
  })
}

export function initializeFirmwareUpdate() {
  if (subscribed) return () => {}
  subscribed = true
  const u1 = onMessageType('update_status', (data) => {
    firmwareUpdateState.checking = false
    firmwareUpdateState.status = data
  })
  const u2 = onMessageType('ota_progress', (data) => {
    if (data.phase === 'start') {
      if (!firmwareUpdateState.blocking) openBlocker('download')
    } else if (data.phase === 'download') {
      if (!firmwareUpdateState.blocking) openBlocker('download')
      if (firmwareUpdateState.blocking?.phase === 'download') {
        firmwareUpdateState.blocking = {
          ...firmwareUpdateState.blocking,
          file: data.file || firmwareUpdateState.blocking.file,
          pct: data.total
            ? Math.round((data.written / data.total) * 100)
            : firmwareUpdateState.blocking.pct,
        }
      }
    } else if (data.phase === 'done') {
      if (data.success) {
        waitForReboot()
      } else {
        closeBlocker(data.message || 'Update failed.', false)
      }
    }
  })
  const u3 = onMessageType('upload_progress', (data) => {
    // SPIFFS uploads report here; firmware .bin goes through XHR progress.
    if (firmwareUpdateState.blocking?.phase === 'upload' && data.total) {
      firmwareUpdateState.blocking = {
        ...firmwareUpdateState.blocking,
        pct: Math.round((data.loaded * 100) / data.total),
      }
    }
  })
  checkForUpdate()
  return () => {
    ;[u1, u2, u3].forEach((u) => u())
    subscribed = false
  }
}
