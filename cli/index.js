#!/usr/bin/env node

// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const { parseArgs } = require('./lib/args')
const { logger, colors, setDebug } = require('./lib/logger')
const { checkPrerequisites } = require('./lib/prerequisites')
const { buildFrontend } = require('./lib/frontend')
const { buildBackendPIO } = require('./lib/pio')
const { configureBackend } = require('./lib/backend')
const { uploadToDevice } = require('./lib/upload')
const { provisionOverUsb } = require('./lib/usb_provision')
const { runNetworkReset } = require('./lib/network_reset')

async function main() {
  try {
    const args = parseArgs()
    setDebug(args.debug)

    if (args.debug) {
      logger.debug('Debug mode enabled')
    }

    // OTA password: explicit flag, else whatever USB provisioning resolves
    // (device's own, or freshly generated). OTA targets carry what was set
    // before, so an empty flag means "try open".
    args.uploadPassword = args.uploadPassword || ''
    let otaPassword = args.uploadPassword

    await checkPrerequisites(args)

    // Recovery op: no build, no upload — reset and exit.
    if (args.networkReset) {
      const ok = await runNetworkReset(args)
      process.exit(ok ? 0 : 1)
    }

    if (!args.frontendOnly) {
      await configureBackend(args)
      await buildBackendPIO(args)
    }
    if (!args.backendOnly) {
      await buildFrontend(args)
    }
    const finalResults = await uploadToDevice(args)
    console.log()

    // Serial flash: provision WiFi/OTA over USB straight into the running
    // firmware (no build-time secrets). On success the device reports its
    // own URL — no network sweep needed.
    let provisioned = null
    if (!args.otaIP && (finalResults.backendUploaded || finalResults.frontendUploaded)) {
      provisioned = await provisionOverUsb(args, {
        uploadPassword: args.uploadPassword,
        backendUploaded: finalResults.backendUploaded,
        frontendUploaded: finalResults.frontendUploaded,
      })
      if (provisioned && provisioned.otaPassword) {
        otaPassword = provisioned.otaPassword
      }
      console.log()
    }

    showCompletionMessage(
      args,
      finalResults,
      otaPassword,
      provisioned && provisioned.url,
      !!(provisioned && !provisioned.ssid && !provisioned.url),
    )
  } catch (error) {
    logger.error('An unexpected error occurred:')
    console.error(error)
    process.exit(1)
  }
}

function showCompletionMessage(args, results, otaPassword, deviceUrl = null, wifiPending = false) {
  logger.separator()

  if (otaPassword) {
    logger.info(`🔑 OTA upload password: ${colors.yellow}${otaPassword}${colors.reset}`)
    logger.info('Keep it safe — it is used to authenticate OTA uploads.')
  } else {
    logger.warning('No OTA password set — uploads are open to the local network.')
  }

  if (results.backendUploaded && results.frontendUploaded) {
    if (args.otaIP) {
      logger.success('🎉 OTA deployment completed successfully!')
      logger.info(
        `Check the web interface at ${colors.yellow}http://${args.otaIP}${colors.reset} to verify the update.`,
      )
    } else if (deviceUrl) {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running.')
      logger.info(
        `Device is online at ${colors.yellow}${deviceUrl}${colors.reset} — check it to verify the update.`,
      )
    } else if (wifiPending) {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running.')
      logger.info('WiFi is not set up yet — join the "SesameMaker" AP to configure it.')
    } else {
      logger.success('🎉 Setup and deployment completed successfully!')
      logger.info('Your ESP32 parking assistant is now running.')
      logger.info('Check your wifi for "SesameMaker" if you haven\'t set it up yet.')
    }
  } else if (results.backendUploaded || results.frontendUploaded) {
    logger.success('🔧 Setup completed with partial deployment')
    if (results.backendUploaded) {
      if (!args.otaIP && deviceUrl) {
        logger.info(
          `✓ Firmware uploaded - device is online at ${colors.yellow}${deviceUrl}${colors.reset}.`,
        )
      } else {
        logger.info('✓ Firmware uploaded - device should be running')
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
