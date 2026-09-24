// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Deployment-target picker: turns network + USB candidates into one
// confirmed { kind, ip|port } target. `discover.js` re-exports this module
// so existing callers keep working.

const { selectFromList } = require('./prompt')
const { logger, colors } = require('./logger')

// Pick a device from the discovered candidates. ALWAYS asks the user to
// confirm — even when exactly one candidate is found. Auto-picking a single
// result silently flashed the wrong device (e.g. an online spare while the
// intended target was offline). Device selection is exempt from -y/--yes.
async function selectDevice(networkDevices, usbDevices) {
  const netCount = networkDevices.length
  const usbCount = usbDevices.length
  const usbIsFallback = Boolean(usbDevices.noEsp)

  // `noEsp` means USB enumeration fell back to "every serial port" because no
  // clearly-ESP device was found. When a SesameMaker IS on the network, those
  // unrelated serial ports are not worth offering: prefer the network device
  // and move on. The fallback is still honoured when the network is empty.
  const usableUsb = usbIsFallback && netCount > 0 ? [] : usbDevices

  if (netCount === 0 && usableUsb.length === 0) {
    logger.warning('No SesameMaker device found on the network or over USB.')
    return null
  }

  const total = netCount + usableUsb.length
  logger.info(
    total === 1
      ? 'Found 1 SesameMaker device — please confirm it is the one to use:'
      : 'Multiple SesameMaker devices detected:',
  )
  if (usableUsb.length > 0 && usbIsFallback) {
    logger.warning('No ESP device detected over USB - showing all serial ports.')
  }
  const options = []
  for (let i = 0; i < netCount; i++) {
    const ip = networkDevices[i].ip
    options.push({
      label: `${colors.cyan}Network (OTA)${colors.reset} — ${ip}`,
      value: { kind: 'ota', ip },
    })
  }
  for (let i = 0; i < usableUsb.length; i++) {
    const dev = usableUsb[i]
    const port = dev.port
    const hint = dev.description ? ` · ${dev.description}` : ''
    options.push({
      label: `${colors.cyan}USB (Serial)${colors.reset} — ${port}${colors.dim}${hint}${colors.reset}`,
      value: { kind: 'serial', port },
    })
  }

  const target = await selectFromList('Which device do you want to use?', options)
  return target || null
}

module.exports = {
  selectDevice,
}
