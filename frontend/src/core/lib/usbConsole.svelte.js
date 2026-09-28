// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Bridge between the USB Setup Improv session (demo builds only) and the
// device console. Deliberately inert: plain reactive state, no WebSerial or
// Improv imports, so the shipped console component can reference it at zero
// on-device cost. All USB work stays in serialImprov.js, which only ships
// inside the dynamically-imported SetupTab chunk (stripped from dist).

const MAX_LINES = 200

export const usbConsoleState = $state({ live: false, lines: [] })

let detach = null

export function attachUsbConsole(session) {
  detachUsbConsole()
  usbConsoleState.lines = []
  usbConsoleState.live = true
  detach = session.onLog((message) => {
    usbConsoleState.lines = [
      ...usbConsoleState.lines.slice(-MAX_LINES + 1),
      { message, received: Date.now() },
    ]
  })
}

export function detachUsbConsole() {
  detach?.()
  detach = null
  usbConsoleState.live = false
}

export function clearUsbConsole() {
  usbConsoleState.lines = []
}
