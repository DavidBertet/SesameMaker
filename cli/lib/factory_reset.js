// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// USB factory reset: erases everything (WiFi, all app settings, Zigbee
// network, HomeKit pairing) and reboots to first-boot defaults. Serial-only
// — the physical cable is the auth. Point of no return: asks for typed
// confirmation (ERASE, not RESET) unless -y/--yes.

const { logger } = require('./logger')
const { Spinner } = require('./spinner')

async function runFactoryReset(args, deps = {}) {
  const improv = deps.improv || require('./improv')
  const prompt = deps.prompt || require('./prompt')

  const port = await improv.resolvePortPath(args.serialPort)
  if (!port) {
    logger.error('No USB serial port found — connect the device first.')
    return false
  }

  if (!args.autoYes) {
    const answer = await prompt.askQuestion(
      `Factory reset the device on ${port}? Erases WiFi, all settings, Zigbee network and HomeKit pairing. Type ERASE to confirm:`,
      false,
    )
    if (answer.trim() !== 'ERASE') {
      logger.info('Factory reset aborted.')
      return false
    }
  }

  const spinner = new Spinner(`Erasing everything on ${port}...`)
  spinner.start()
  try {
    await improv.factoryReset(port)
    spinner.stop(true, 'Device erased — rebooting to first-boot defaults.')
    return true
  } catch (error) {
    spinner.stop(false, 'Factory reset failed')
    logger.error(error.message)
    return false
  }
}

module.exports = { runFactoryReset }
