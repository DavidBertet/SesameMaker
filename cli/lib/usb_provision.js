// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Post-flash USB provisioning over Improv (replaces build-time secrets).
// Resolves the effective OTA password (explicit -p wins, else the device's
// own, else a fresh generated one) and prints it — always, so the value on
// the device is never a mystery. With a TTY it also sends WiFi credentials.
// Only serial installs have a USB port to talk to; OTA targets and skipped
// uploads return null silently. Password prompts need real interactive
// input, so WiFi setup is TTY-gated (like device selection, exempt from
// -y/--yes) while the password resolution runs headless too.

const { logger } = require('./logger')
const { Spinner } = require('./spinner')

function isInteractive() {
  return Boolean(process.stdin.isTTY && process.stdout.isTTY)
}

// The device reboots after flashing and may still be booting (or the port
// briefly busy after esptool lets go), so retry the first contact instead
// of one-shotting it. Returns the password string, '' when open, or null
// when the device never answers (old firmware without Improv).
async function readDeviceOtaPassword(port, improv, { attempts = 8, retryMs = 2000 } = {}) {
  for (let attempt = 1; attempt <= attempts; attempt++) {
    try {
      const pwd = await improv.getOtaPassword(port, { timeoutMs: 2500 })

      // If we got a string (even an empty string ''), return it
      if (pwd !== null) {
        return pwd
      }

      // If pwd is null, the device didn't answer (likely rebooting)
      if (attempt < attempts) {
        await new Promise((resolve) => setTimeout(resolve, retryMs))
      }
    } catch (error) {
      // If pwd is null, the device didn't answer (likely rebooting)
      if (attempt < attempts) {
        await new Promise((resolve) => setTimeout(resolve, retryMs))
      }
      logger.debug(`Usb read failed: ${error.message}`)
    }
  }

  return null // Exhausted all attempts
}

// The OTA password on the device after this step. Explicit flag wins;
// otherwise keep the device's (even when open); generate + send one when
// the device has none. Always returns the effective value.
async function resolveDeviceOtaPassword(port, uploadPassword, deps, retry) {
  const { setOtaPassword } = deps.improv
  const { generateOtaPassword } = deps.password
  const spinner = new Spinner('Reading device OTA password...')
  spinner.start()
  let devicePw = await readDeviceOtaPassword(port, deps.improv, retry)
  if (devicePw == null) {
    spinner.stop(false, 'Device did not answer (not booted yet, or old firmware without Improv?)')
    return null
  }
  spinner.stop(true)

  if (uploadPassword) {
    if (uploadPassword !== devicePw) {
      await setOtaPassword(port, uploadPassword)
      logger.info('Custom OTA password sent to the device.')
    }
    return uploadPassword
  }
  if (devicePw) {
    return devicePw
  }
  const generated = generateOtaPassword()
  await setOtaPassword(port, generated)
  return generated
}

async function provisionOverUsb(args, { uploadPassword, backendUploaded, retry } = {}, deps = {}) {
  const improv = deps.improv || require('./improv')
  const prompt = deps.prompt || require('./prompt')
  const password = deps.password || require('./password')

  if (!backendUploaded || args.otaIP) {
    return null
  }

  // Forced serial installs (-s) skip discovery, so no port is known yet —
  // and flashing can re-enumerate the device under a new node. Follow a
  // single USB-serial candidate when the known path is gone.
  const port = await improv.resolvePortPath(args.serialPort)
  if (!port) {
    logger.info('No USB serial port found — skipping USB provisioning.')
    return null
  }
  if (port !== args.serialPort) {
    logger.info(`Using serial port ${port} for USB provisioning.`)
  }

  const otaPassword = await resolveDeviceOtaPassword(
    port,
    uploadPassword,
    {
      improv,
      password,
    },
    retry,
  )
  if (otaPassword === null) {
    return null
  }
  // Printed once by the completion message — not here.

  if (!isInteractive()) {
    logger.info('Skipping WiFi setup (non-interactive session).')
    logger.info('Join the "SesameMaker" AP or re-run with a TTY to configure WiFi.')
    return { ssid: null, url: null, otaPassword }
  }

  const again = await prompt.askQuestion('Configure WiFi over USB now? (Y/n)', false)
  if (again.toLowerCase() === 'n' || again.toLowerCase() === 'no') {
    return { ssid: null, url: null, otaPassword }
  }

  let ssid = args.wifiSsid || ''
  if (!ssid) {
    ssid = (await prompt.askQuestion('WiFi SSID:')).trim()
  }
  if (!ssid || Buffer.byteLength(ssid, 'utf8') > 32) {
    logger.warning('WiFi setup aborted (SSID empty or over 32 bytes).')
    return { ssid: null, url: null, otaPassword }
  }
  const wifiPassword = await prompt.askPassword(`WiFi password for "${ssid}":`)
  if (!wifiPassword) {
    logger.warning('WiFi setup aborted (empty password).')
    return { ssid: null, url: null, otaPassword }
  }

  const spinner = new Spinner(`Provisioning "${ssid}" over USB...`)
  spinner.start()
  try {
    const res = await improv.provisionWifi(port, { ssid, password: wifiPassword })
    spinner.stop(true, `Device is joining "${ssid}"`)
    return { ssid, url: res.url, otaPassword }
  } catch (error) {
    spinner.stop(false, 'USB provisioning failed')
    logger.error(error.message)
    logger.info('Retry over USB, or join the "SesameMaker" AP to configure WiFi.')
    return { ssid: null, url: null, otaPassword }
  }
}

module.exports = { provisionOverUsb }
