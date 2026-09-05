// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const fs = require('fs')
const path = require('path')
const { askQuestion } = require('./prompt')
const { logger, colors } = require('./logger')
const { injectOtaPassword } = require('./password')

async function configureBackend(args, otaPassword) {
  logger.step('Configuring backend...')
  logger.separator()

  if (otaPassword) {
    injectOtaPassword(otaPassword)
    logger.info(`OTA password configured (${otaPassword.length} characters)`)
  }

  const platformioPath = path.resolve('backend/platformio.ini')
  let platformioContent = fs.readFileSync(platformioPath, 'utf8')

  // Extract the current environment name (section header [env:...]).
  // PlatformIO derives the sdkconfig file and build dir from it, so it must
  // stay in sync with the board or the target (esp32 / esp32c3 / ...) mismatches.
  const envMatch = platformioContent.match(/\[env:([^\]]+)\]/)
  const currentEnv = envMatch ? envMatch[1].trim() : null

  // Extract current board
  const boardMatch = platformioContent.match(/board\s*=\s*(.+)/)
  const currentBoard = boardMatch ? boardMatch[1].trim() : 'esp32-c3-devkitm-1'

  logger.info(`Current board: ${colors.yellow}${currentBoard}${colors.reset}`)

  const shouldChange = await askQuestion('Do you want to change the board? (y/N)', args.autoYes)

  if (shouldChange.toLowerCase() === 'y' || shouldChange.toLowerCase() === 'yes') {
    console.log()
    logger.info('Common ESP32 boards:')
    console.log('  • esp32-c3-devkitm-1 (ESP32-C3 DevKit)')
    console.log('  • esp32doit-devkit-v1 (ESP32 DevKit V1)')
    console.log('  • esp32dev (Generic ESP32)')
    console.log('  • nodemcu-32s (NodeMCU-32S)')
    console.log('  • esp32-s3-devkitc-1 (ESP32-S3 DevKit)')
    console.log()
    logger.info(
      'Full list: https://docs.platformio.org/en/latest/platforms/espressif32.html#boards',
    )
    console.log()

    const newBoard = await askQuestion('Enter board name:', args.autoYes)

    if (newBoard && newBoard !== currentBoard) {
      // Rename the env section to match the new board so the generated
      // sdkconfig (and build dir) match the new target.
      if (currentEnv) {
        platformioContent = platformioContent.replace(/\[env:[^\]]+\]/, `[env:${newBoard}]`)
      }
      platformioContent = platformioContent.replace(/board\s*=\s*.+/, `board = ${newBoard}`)
      fs.writeFileSync(platformioPath, platformioContent)
      logger.success(`Board updated to: ${newBoard}`)

      // Purge stale sdkconfig + build artifacts so the new target regenerates cleanly.
      cleanupBackendConfig(currentEnv)
    }
  }

  console.log()
}

// Remove generated sdkconfig files and stale build dirs. Keeping sdkconfig.defaults
// (it holds the required manual settings and is regenerated into the real sdkconfig).
function cleanupBackendConfig(oldEnv) {
  const backendPath = path.resolve('backend')

  // Delete generated sdkconfig files, but keep sdkconfig.defaults*
  const files = fs.existsSync(backendPath) ? fs.readdirSync(backendPath) : []
  for (const file of files) {
    if (file.startsWith('sdkconfig') && !file.startsWith('sdkconfig.defaults')) {
      fs.unlinkSync(path.join(backendPath, file))
      logger.info(`Removed stale config: ${file}`)
    }
  }

  // Remove the old env's build directory
  const buildDir = path.join(backendPath, '.pio', 'build')
  if (fs.existsSync(buildDir)) {
    const dirs = fs.readdirSync(buildDir)
    for (const dir of dirs) {
      if (dir === 'project.checksum' || (oldEnv && dir !== oldEnv)) {
        continue
      }
      fs.rmSync(path.join(buildDir, dir), { recursive: true, force: true })
      logger.info(`Removed stale build dir: ${dir}`)
    }
  }
}

function findBuildFiles() {
  const backendPath = path.resolve('backend')

  // Look for firmware file
  const pioenvDirs = fs
    .readdirSync(path.join(backendPath, '.pio', 'build'))
    .filter((dir) => fs.statSync(path.join(backendPath, '.pio', 'build', dir)).isDirectory())

  if (pioenvDirs.length === 0) {
    throw new Error('No build environment found. Run build first.')
  }

  const envDir = pioenvDirs[0] // Use first environment
  const firmwarePath = path.join(backendPath, '.pio', 'build', envDir, 'firmware.bin')

  if (!fs.existsSync(firmwarePath)) {
    throw new Error('Firmware binary not found. Build may have failed.')
  }

  return {
    firmware: firmwarePath,
    envDir,
  }
}

module.exports = { configureBackend, cleanupBackendConfig, findBuildFiles }
