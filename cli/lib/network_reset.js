// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// USB network reset: forgets WiFi, clears the OTA password, reboots the
// device into the setup AP. Serial-only — the physical cable is the auth.
// Destructive: asks for typed confirmation unless -y/--yes.

const { logger } = require('./logger')
const { Spinner } = require('./spinner')

async function runNetworkReset(args, deps = {}) {
  const improv = deps.improv || require('./improv')
  const prompt = deps.prompt || require('./prompt')

  const port = await improv.resolvePortPath(args.serialPort)
  if (!port) {
    logger.error('No USB serial port found — connect the device first.')
    return false
  }

  if (!args.autoYes) {
    const answer = await prompt.askQuestion(
      `Network reset the device on ${port}? Forgets WiFi, clears the OTA password, reboots. Type RESET to confirm:`,
      false,
    )
    if (answer.trim() !== 'RESET') {
      logger.info('Network reset aborted.')
      return false
    }
  }

  const spinner = new Spinner(`Resetting network on ${port}...`)
  spinner.start()
  try {
    await improv.networkReset(port)
    spinner.stop(true, 'Device reset — rebooting into the "SesameMaker" setup AP.')
    return true
  } catch (error) {
    spinner.stop(false, 'Network reset failed')
    logger.error(error.message)
    return false
  }
}

module.exports = { runNetworkReset }
