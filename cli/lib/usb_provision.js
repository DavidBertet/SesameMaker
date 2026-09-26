// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Post-flash USB provisioning over Improv (replaces build-time secrets):
// after a serial upload, send WiFi credentials (+ OTA password) straight to
// the running firmware. Only serial installs have a USB port to talk to;
// OTA targets and skipped uploads return null silently.

const { logger } = require('./logger')
const { Spinner } = require('./spinner')

// TTY-gated: passwords need real interactive input, so without a TTY there
// is nobody to type them (like device selection, exempt from -y/--yes).
function isInteractive() {
  return Boolean(process.stdin.isTTY && process.stdout.isTTY)
}

async function provisionOverUsb(args, { otaPassword, backendUploaded } = {}, deps = {}) {
  const { provisionWifi } = deps.improv || require('./improv')
  const prompt = deps.prompt || require('./prompt')

  if (!backendUploaded || !args.serialPort || args.otaIP) {
    return null
  }
  if (!isInteractive()) {
    logger.info('Skipping USB provisioning (non-interactive session).')
    logger.info('Join the "SesameMaker" AP or re-run with a TTY to configure WiFi.')
    logger.info('Note: first boot generated a random OTA password (see the USB serial log).')
    return null
  }

  const again = await prompt.askQuestion('Configure WiFi over USB now? (Y/n)', false)
  if (again.toLowerCase() === 'n' || again.toLowerCase() === 'no') {
    return null
  }

  let ssid = args.wifiSsid || ''
  if (!ssid) {
    ssid = (await prompt.askQuestion('WiFi SSID:')).trim()
  }
  if (!ssid || Buffer.byteLength(ssid, 'utf8') > 32) {
    logger.warning('WiFi setup aborted (SSID empty or over 32 bytes).')
    return null
  }
  const password = await prompt.askPassword(`WiFi password for "${ssid}":`)
  if (!password) {
    logger.warning('WiFi setup aborted (empty password).')
    return null
  }

  const spinner = new Spinner(`Provisioning "${ssid}" over USB...`)
  spinner.start()
  try {
    const res = await provisionWifi(args.serialPort, { ssid, password, otaPassword })
    spinner.stop(true, `Device is joining "${ssid}"`)
    if (res.url) {
      logger.info(`Web interface will be at ${res.url} once it joins.`)
    }
    return res
  } catch (error) {
    spinner.stop(false, 'USB provisioning failed')
    logger.error(error.message)
    logger.info('Retry over USB, or join the "SesameMaker" AP to configure WiFi.')
    return null
  }
}

module.exports = { provisionOverUsb }
