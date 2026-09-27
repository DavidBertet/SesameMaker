// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Shared WiFi display helpers (used by the WiFi page and the USB setup tab).

// -30 dBm and up is excellent, -100 is effectively gone.
export function getSignalStrength(rssi) {
  if (rssi >= -30) return 'Excellent'
  if (rssi >= -50) return 'Good'
  if (rssi >= -60) return 'Fair'
  if (rssi >= -70) return 'Weak'
  return 'Very Weak'
}
