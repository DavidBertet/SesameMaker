// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Door-event attention (app): Web Notification while the page is open.

function fmtElapsed(total) {
  const m = Math.floor(total / 60)
  const s = total % 60
  return `${m}:${String(s).padStart(2, '0')}`
}

// Exported for tests (browser globals are only touched inside functions).
export const __test = { fmtElapsed }

export function notificationPermission() {
  if (!('Notification' in window)) return 'unsupported'
  return Notification.permission
}

// Call from a user gesture (e.g. saving settings) so the prompt is allowed.
export async function ensureNotificationPermission() {
  if (!('Notification' in window)) return false
  if (Notification.permission === 'granted') return true
  if (Notification.permission === 'denied') return false
  return (await Notification.requestPermission()) === 'granted'
}

const COPY = {
  warn: (s) => [`Door left open — ${fmtElapsed(s)}`, 'The garage door has been open too long.'],
  auto_close: () => ['Auto-closing the door', 'Left open too long — closing now.'],
  blocked: () => ['Auto-close blocked', 'Obstruction detected — the door stays open.'],
  closed: () => ['Door closed', 'The auto-closed door is now shut.'],
}

export function notifyDoorEvent(event, elapsed_s = 0) {
  const make = COPY[event]
  if (!make) return
  const [title, body] = make(elapsed_s)
  try {
    if ('Notification' in window && Notification.permission === 'granted') {
      const n = new Notification(title, { body, tag: 'sesame-door', requireInteraction: true })
      n.onclick = () => window.focus()
    }
  } catch {
    // Notifications unavailable (or tab context gone) — nothing else to do.
  }
}
