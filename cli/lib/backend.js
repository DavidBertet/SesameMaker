// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const fs = require('fs')
const path = require('path')
const { askQuestion } = require('./prompt')
const { logger, colors } = require('./logger')
const { injectOtaPassword } = require('./password')

// Parse the single [env:...] section and its board from platformio.ini.
// Line-anchored: comments may mention [env:...] names.
// Returns { env, board } or null. Pure (no fs) so it is unit tested.
function parsePioEnv(iniContent) {
  const headers = [...iniContent.matchAll(/^\[env:([^\]]+)\]/gm)]
  if (headers.length === 0) return null
  const body = iniContent.slice(
    headers[0].index,
    headers.length > 1 ? headers[1].index : iniContent.length,
  )
  const boardMatch = body.match(/^\s*board\s*=\s*(.+?)\s*$/m)
  return { env: headers[0][1].trim(), board: boardMatch ? boardMatch[1].trim() : null }
}

// Rewrite the `board = ...` line inside the [env:...] section.
// Pure (no fs) so it is unit tested.
function setEnvBoard(iniContent, envName, board) {
  const parsed = parsePioEnv(iniContent)
  if (!parsed || parsed.env !== envName) return iniContent
  return iniContent.replace(/^\s*board\s*=.*$/m, `board = ${board}`)
}

async function configureBackend(args, otaPassword) {
  logger.step('Configuring backend...')
  logger.separator()

  if (otaPassword) {
    injectOtaPassword(otaPassword)
    logger.info(`OTA password configured (${otaPassword.length} characters)`)
  }

  const platformioPath = path.resolve('backend/platformio.ini')
  let platformioContent = fs.readFileSync(platformioPath, 'utf8')
  const parsed = parsePioEnv(platformioContent)

  if (!parsed || !parsed.board) {
    logger.error('No [env:...] section with a board found in backend/platformio.ini')
    process.exit(1)
  }

  const envName = parsed.env
  const current = parsed.board
  logger.info(`Current board: ${colors.yellow}${current}${colors.reset}`)
  console.log()
  logger.info('Examples: esp32doit-devkit-v1, esp32-c3-devkitm-1, esp32-c6-devkitc-1')
  console.log()

  let board = current
  if (!args.autoYes) {
    const answer = await askQuestion(`Board [default: ${current}]:`, false)
    if (answer && answer.trim()) board = answer.trim()
  } else {
    logger.warning(`Non-interactive, using board: ${board}`)
  }

  if (board !== current) {
    platformioContent = setEnvBoard(platformioContent, envName, board)
    fs.writeFileSync(platformioPath, platformioContent)
    logger.success(`Board updated to: ${board}`)
    // Target changed: stale sdkconfig + build artifacts would mismatch.
    cleanupBackendConfig(envName)
  }

  logger.success(`Target: ${colors.yellow}${board}${colors.reset}`)
  console.log()
}

// Remove generated sdkconfig files and stale build dirs. Keeping sdkconfig.defaults
// (it holds the required manual settings and is regenerated into the real sdkconfig).
function cleanupBackendConfig(envName) {
  const backendPath = path.resolve('backend')

  // Delete generated sdkconfig files, but keep sdkconfig.defaults*
  const files = fs.existsSync(backendPath) ? fs.readdirSync(backendPath) : []
  for (const file of files) {
    if (file.startsWith('sdkconfig') && !file.startsWith('sdkconfig.defaults')) {
      fs.unlinkSync(path.join(backendPath, file))
      logger.info(`Removed stale config: ${file}`)
    }
  }

  // Remove the env's build directory so the new target regenerates cleanly
  const buildDir = path.join(backendPath, '.pio', 'build')
  if (fs.existsSync(buildDir)) {
    const dirs = fs.readdirSync(buildDir)
    for (const dir of dirs) {
      if (dir === 'project.checksum' || (envName && dir !== envName)) {
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

module.exports = {
  configureBackend,
  cleanupBackendConfig,
  findBuildFiles,
  parsePioEnv,
  setEnvBoard,
}
