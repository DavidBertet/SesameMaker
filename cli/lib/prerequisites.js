// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const fs = require('fs')
const { execPromise } = require('./system')
const { testESP32Connection } = require('./esp32')
const { discoverNetworkDevices, discoverUsbDevices, selectDevice } = require('./discover')
const { Spinner } = require('./spinner')
const { logger, colors } = require('./logger')
const { askQuestion } = require('./prompt')
const { findPIOExecutable } = require('./pio')

async function checkOtaIpFormat(args) {
  if (!args.otaIP) {
    return
  }
  const ipRegex = /^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$/

  if (!ipRegex.test(args.otaIP)) {
    logger.warning(`IP address format may be invalid: ${args.otaIP}`)
    logger.info('Expected format: 192.168.1.10')

    const shouldChange = await askQuestion('Continue anyway? (y/N)', args.autoYes)

    if (shouldChange.toLowerCase() !== 'y' && shouldChange.toLowerCase() !== 'yes') {
      logger.info('Installation cancelled by user')
      process.exit(0)
    }
  }
}

function checkProjectStructure() {
  const requiredPaths = ['frontend', 'backend', 'backend/platformio.ini']

  for (const reqPath of requiredPaths) {
    if (!fs.existsSync(reqPath)) {
      logger.error(`Required path not found: ${reqPath}`)
      logger.info("Make sure you're running this script from the project root directory.")
      process.exit(1)
    }
  }

  logger.success('Project structure verified')
}

// Discover + select the deployment target when the user didn't give an explicit
// --ota <IP> or --serial/--usb flag. Resolves args.otaIP (OTA) or args.usbOnly
// (serial) accordingly so the rest of the pipeline is unchanged.
async function resolveDiscoveryTarget(args) {
  const scanSpinner = new Spinner('Scanning local network and USB for SesameMaker devices...')
  scanSpinner.start()

  let networkDevices = []
  let usbDevices = []

  try {
    if (!args.usbOnly) {
      networkDevices = await discoverNetworkDevices()
    }
    if (!args.otaDiscoveryOnly) {
      usbDevices = await discoverUsbDevices()
    }
  } catch (error) {
    scanSpinner.stop(false, 'Device discovery failed')
    logger.error(error.message)
    process.exit(1)
  }

  scanSpinner.stop()

  // Device selection is always interactive — exempt from -y/--yes. Silently
  // auto-picking (even a single result) flashed the wrong device. In a truly
  // non-interactive session (no TTY) there is nobody to confirm, so require an
  // explicit target instead of guessing.
  if (args.autoYes && !(process.stdin.isTTY && process.stdout.isTTY)) {
    logger.error('Non-interactive mode (-y) with device discovery is ambiguous.')
    logger.info('Specify the target explicitly: --ota <IP> or --serial/--usb.')
    process.exit(1)
  }

  const target = await selectDevice(networkDevices, usbDevices)
  if (!target) {
    logger.info('No device selected. Install aborted.')
    process.exit(0)
  }

  if (target.kind === 'ota') {
    args.otaIP = target.ip
    args.discoveredKind = 'ota'
    logger.success(`Selected network device at ${target.ip}`)
  } else {
    args.usbOnly = true
    args.serialPort = target.port
    args.discoveredKind = 'serial'
    logger.success(`Selected USB device on ${target.port}`)
  }
  console.log()
}

async function checkPrerequisites(args) {
  logger.step('Checking other prerequisites...')
  logger.separator()

  // Check PlatformIO
  const spinner = new Spinner('Checking PlatformIO...')
  spinner.start()

  const pioCmd = await findPIOExecutable()

  if (pioCmd) {
    try {
      spinner.stop(true, `PlatformIO found in: ${pioCmd}`)
      spinner.start()
      const { stdout } = await execPromise(`"${pioCmd}" --version`)
      spinner.stop(true, `PlatformIO info (${stdout.trim()})`)
    } catch {}
  } else {
    spinner.stop(false, 'PlatformIO not found')
    logger.separator()
    logger.error('PlatformIO is required but not installed.')
    console.log()
    console.log(`${colors.yellow}Install PlatformIO:${colors.reset}`)
    console.log('  Option 1 - Using VSCode: install PlatformIO extension')
    console.log('  Option 2 - Using pip: pip install platformio')
    console.log('  Option 3 - Using brew: brew install platformio')
    console.log('Visit: https://platformio.org/install for details')
    console.log()
    logger.error('Please install PlatformIO and run this script again.')
    process.exit(1)
  }

  // Discover + select the target device unless the user pinned one explicitly
  // (--ota <IP>) or forced USB-only (--serial/--usb).
  if (args.discover) {
    await resolveDiscoveryTarget(args)
  }

  // Test OTA connection if IP provided
  if (args.otaIP) {
    await checkOtaIpFormat(args)

    const otaSpinner = new Spinner(`Testing OTA connection to ${args.otaIP}...`)
    otaSpinner.start()

    const result = await testESP32Connection(args.otaIP)
    if (result.connected) {
      otaSpinner.stop(true, `OTA connection to ${args.otaIP} successful`)
    } else {
      otaSpinner.stop(false, `Cannot connect to ${args.otaIP} (${result.error})`)
      logger.error('Make sure the ESP32 is powered on and connected to the network')
      process.exit(0)
    }
  }

  checkProjectStructure()

  logger.success('Prerequisites check completed!')
  console.log()
}

module.exports = { checkPrerequisites }
