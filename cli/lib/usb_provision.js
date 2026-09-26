// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Post-flash USB provisioning over Improv (replaces build-time secrets).
// Resolves the effective OTA password (explicit -p wins, else the device's
// own, else a fresh generated one) and prints it — always, so the value on
// the device is never a mystery. With a TTY it also sends WiFi credentials.
// Only serial installs have a USB port to talk to; OTA targets and skipped
// uploads return null silently. Password prompts need real interactive
// input, so WiFi setup is TTY-gated (like device selection, exempt from
// -y/--yes) while the password resolution runs headless too.

const { logger, colors } = require('./logger')
const { Spinner } = require('./spinner')

function isInteractive() {
  return Boolean(process.stdin.isTTY && process.stdout.isTTY)
}

// The OTA password on the device after this step. Explicit flag wins;
// otherwise keep the device's (even when open); generate + send one when
// the device has none. Always returns the effective value.
async function resolveDeviceOtaPassword(port, uploadPassword, deps) {
  const { getOtaPassword, setOtaPassword } = deps.improv
  const { generateOtaPassword } = deps.password
  const spinner = new Spinner('Reading device OTA password...')
  spinner.start()
  let devicePw = null
  try {
    devicePw = await getOtaPassword(port)
  } catch (error) {
    spinner.stop(false, 'Device did not answer (old firmware without Improv?)')
    return null
  }
  spinner.stop(true, 'Device answered over USB')

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

async function provisionOverUsb(args, { uploadPassword, backendUploaded } = {}, deps = {}) {
  const improv = deps.improv || require('./improv')
  const prompt = deps.prompt || require('./prompt')
  const password = deps.password || require('./password')

  if (!backendUploaded || !args.serialPort || args.otaIP) {
    return null
  }

  const otaPassword = await resolveDeviceOtaPassword(args.serialPort, uploadPassword, {
    improv,
    password,
  })
  if (otaPassword === null) {
    return null
  }
  logger.info(
    `OTA upload password: ${colors.yellow}${otaPassword || '(none — uploads are open)'}${colors.reset}`,
  )
  if (otaPassword) {
    logger.info('Keep it safe — it is used to authenticate OTA uploads.')
  }

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
    const res = await improv.provisionWifi(args.serialPort, { ssid, password: wifiPassword })
    spinner.stop(true, `Device is joining "${ssid}"`)
    if (res.url) {
      logger.info(`Web interface will be at ${res.url} once it joins.`)
    }
    return { ssid, url: res.url, otaPassword }
  } catch (error) {
    spinner.stop(false, 'USB provisioning failed')
    logger.error(error.message)
    logger.info('Retry over USB, or join the "SesameMaker" AP to configure WiFi.')
    return { ssid: null, url: null, otaPassword }
  }
}

module.exports = { provisionOverUsb }
