#!/usr/bin/env node

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const { parseArgs } = require('./lib/args')
const { logger, colors, setDebug } = require('./lib/logger')
const { checkPrerequisites } = require('./lib/prerequisites')
const { buildFrontend } = require('./lib/frontend')
const { buildBackendPIO } = require('./lib/pio')
const { configureBackend } = require('./lib/backend')
const { uploadToDevice } = require('./lib/upload')
const { resolveOtaPassword } = require('./lib/password')
const { provisionOverUsb } = require('./lib/usb_provision')
const { waitForDeviceOnNetwork } = require('./lib/discover')
const { Spinner } = require('./lib/spinner')

async function main() {
  try {
    const args = parseArgs()
    setDebug(args.debug)

    if (args.debug) {
      logger.debug('Debug mode enabled')
    }

    const otaPassword = resolveOtaPassword(args.uploadPassword)
    args.uploadPassword = otaPassword

    await checkPrerequisites(args)
    if (!args.frontendOnly) {
      await configureBackend(args)
      await buildBackendPIO(args)
    }
    if (!args.backendOnly) {
      await buildFrontend(args)
    }
    const finalResults = await uploadToDevice(args)

    // Serial flash: provision WiFi/OTA over USB straight into the running
    // firmware (no build-time secrets), then find where it landed on WiFi.
    let provisioned = null
    if (!args.otaIP && finalResults.backendUploaded) {
      provisioned = await provisionOverUsb(args, {
        otaPassword,
        backendUploaded: finalResults.backendUploaded,
      })
    }

    // Serial flash with WiFi: the device reboots, joins WiFi via DHCP under a
    // new unknown IP. Poll the network so we can print where it landed.
    let discoveredDevices = []
    const joinSsid = provisioned && provisioned.ssid
    if (
      !args.otaIP &&
      joinSsid &&
      (finalResults.backendUploaded || finalResults.frontendUploaded)
    ) {
      const spinner = new Spinner(
        `Waiting for device to join "${joinSsid}" (up to ~60s)...`,
      )
      spinner.start()
      discoveredDevices = await waitForDeviceOnNetwork()
      if (discoveredDevices.length > 0) {
        spinner.stop(true, 'Device found on the network!')
      } else {
        spinner.stop()
        logger.warning('Device did not show up on the network yet.')
      }
      console.log()
    }

    showCompletionMessage(args, finalResults, otaPassword, discoveredDevices)
  } catch (error) {
    logger.error('An unexpected error occurred:')
    console.error(error)
    process.exit(1)
  }
}

function showCompletionMessage(args, results, otaPassword, discoveredDevices = []) {
  logger.separator()

  if (otaPassword) {
    logger.info(`🔑 OTA upload password: ${colors.yellow}${otaPassword}${colors.reset}`)
    logger.info('Keep it safe — it is used to authenticate OTA uploads.')
  }

  if (results.backendUploaded && results.frontendUploaded) {
    if (args.otaIP) {
      logger.success('🎉 OTA deployment completed successfully!')
      logger.info(
        `Check the web interface at ${colors.yellow}http://${args.otaIP}${colors.reset} to verify the update.`,
      )
    } else if (discoveredDevices.length === 1) {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running.')
      logger.info(
        `Device is online at ${colors.yellow}http://${discoveredDevices[0].ip}${colors.reset} — check it to verify the update.`,
      )
    } else if (discoveredDevices.length > 1) {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running. Devices found on the network:')
      for (const dev of discoveredDevices) {
        logger.info(`  • ${colors.yellow}http://${dev.ip}${colors.reset}`)
      }
    } else {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running.')
      logger.info('Check your wifi for "SesameMaker" if you haven\'t set it up yet.')
    }
  } else if (results.backendUploaded || results.frontendUploaded) {
    logger.success('🔧 Setup completed with partial deployment')
    if (results.backendUploaded) {
      logger.info('✓ Firmware uploaded - device should be running')
      if (!args.otaIP && discoveredDevices.length === 1) {
        logger.info(
          `Device is online at ${colors.yellow}http://${discoveredDevices[0].ip}${colors.reset}.`,
        )
      } else if (!args.otaIP && discoveredDevices.length > 1) {
        for (const dev of discoveredDevices) {
          logger.info(`  • ${colors.yellow}http://${dev.ip}${colors.reset}`)
        }
      }
      if (!results.frontendUploaded && !args.backendOnly) {
        logger.warning(
          args.otaIP
            ? '⚠ Filesystem not uploaded via OTA'
            : '⚠ Web interface files not uploaded - use: pio run -t uploadfs',
        )
      }
    }
    if (results.frontendUploaded) {
      logger.info('✓ Web files uploaded')
      if (!results.backendUploaded && !args.frontendOnly) {
        logger.warning(
          args.otaIP
            ? '⚠ Firmware not uploaded via OTA'
            : '⚠ Firmware not uploaded - use: pio run -t upload',
        )
      }
    }
  } else {
    logger.success('🔨 Build completed successfully!')
    let webfilePrepared = !args.backendOnly ? 'Web files prepared' : ''
    let firmwareBuilt = !args.frontendOnly ? 'Firmware built' : ''
    logger.info([firmwareBuilt, webfilePrepared].filter(Boolean).join(' and '))
    if (args.otaIP) {
      logger.info('To deploy via OTA, run this command again and choose to upload.')
    } else {
      logger.info('To deploy to ESP32:')
      logger.info('  • Upload firmware: pio run -t upload')
      logger.info('  • Upload web files: pio run -t uploadfs')
      logger.info('  • Or use OTA: ./setup.sh --ota <ESP32_IP>')
    }
  }
  console.log()
}

// Run the main function
if (require.main === module) {
  main()
}

module.exports = { main }
