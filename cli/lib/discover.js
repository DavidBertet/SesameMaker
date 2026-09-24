// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Facade over the split discovery modules — `net_scan.js` (subnet math,
// ARP pre-filter, HTTP probing), `usb_list.js` (`pio device list` parsing,
// dedupe) and `select.js` (target picker). Kept so existing
// `require('./discover')` callers (prerequisites, index, tests) keep working
// unchanged; new code should require the focused module directly.

const netScan = require('./net_scan')
const usbList = require('./usb_list')
const select = require('./select')

module.exports = {
  ...netScan,
  ...usbList,
  ...select,
}
