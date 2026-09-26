const path = require('path')
const fs = require('fs')
const os = require('os')
const { commandExists, execPromise, execWithOutput } = require('./system')
const { askQuestion } = require('./prompt')
const { Spinner } = require('./spinner')
const { logger, colors } = require('./logger')
const { drawProgressBar } = require('./progressbar')

/**
 * Find PlatformIO executable, checking VS Code extension if not in PATH
 */
async function findPIOExecutable() {
  const pioExists = await commandExists('pio')
  if (pioExists) {
    return 'pio'
  }

  // Check for VS Code PlatformIO extension
  const vscodeExtensionPaths = getVSCodeExtensionPaths()

  for (const extensionPath of vscodeExtensionPaths) {
    const pioPath = findPIOInExtension(extensionPath)
    if (pioPath) {
      return pioPath
    }
  }

  throw new Error('PlatformIO not found in PATH or VS Code extension')
}

/**
 * Get possible VS Code extension paths based on OS
 */
function getVSCodeExtensionPaths() {
  const homeDir = os.homedir()
  const platform = os.platform()
  const paths = []

  if (platform === 'win32') {
    // Windows paths
    paths.push(path.join(homeDir, '.vscode', 'extensions'))
    paths.push(path.join(homeDir, 'AppData', 'Roaming', 'Code', 'User', 'extensions'))
    paths.push(path.join(homeDir, 'AppData', 'Roaming', 'Code - Insiders', 'User', 'extensions'))
  } else if (platform === 'darwin') {
    // macOS paths
    paths.push(path.join(homeDir, '.vscode', 'extensions'))
    paths.push(path.join(homeDir, 'Library', 'Application Support', 'Code', 'User', 'extensions'))
    paths.push(
      path.join(homeDir, 'Library', 'Application Support', 'Code - Insiders', 'User', 'extensions'),
    )
  } else {
    // Linux paths
    paths.push(path.join(homeDir, '.vscode', 'extensions'))
    paths.push(path.join(homeDir, '.config', 'Code', 'User', 'extensions'))
    paths.push(path.join(homeDir, '.config', 'Code - Insiders', 'User', 'extensions'))
  }

  return paths.filter((p) => fs.existsSync(p))
}

/**
 * Find PlatformIO executable in VS Code extension directory
 */
function findPIOInExtension(extensionPath) {
  try {
    const extensionDirs = fs.readdirSync(extensionPath)
    const pioExtension = extensionDirs.find((dir) => dir.startsWith('platformio.platformio-ide-'))

    if (!pioExtension) {
      return null
    }

    const pioExtensionPath = path.join(extensionPath, pioExtension)
    const platform = os.platform()

    // Look for PlatformIO Core in the extension
    const possiblePaths = []

    if (platform === 'win32') {
      possiblePaths.push(
        path.join(pioExtensionPath, 'piocore', 'penv', 'Scripts', 'pio.exe'),
        path.join(pioExtensionPath, 'piocore', 'penv', 'Scripts', 'platformio.exe'),
      )
    } else {
      possiblePaths.push(
        path.join(pioExtensionPath, 'piocore', 'penv', 'bin', 'pio'),
        path.join(pioExtensionPath, 'piocore', 'penv', 'bin', 'platformio'),
      )
    }

    for (const pioPath of possiblePaths) {
      if (fs.existsSync(pioPath)) {
        return pioPath
      }
    }

    // Also check for global PlatformIO installation managed by the extension
    const globalPioPath = findGlobalPIOFromExtension()
    if (globalPioPath) {
      return globalPioPath
    }

    return null
  } catch (error) {
    return null
  }
}

/**
 * Find global PlatformIO installation that might be managed by VS Code extension
 */
function findGlobalPIOFromExtension() {
  const homeDir = os.homedir()
  const platform = os.platform()
  const possiblePaths = []

  if (platform === 'win32') {
    possiblePaths.push(
      path.join(homeDir, '.platformio', 'penv', 'Scripts', 'pio.exe'),
      path.join(homeDir, '.platformio', 'penv', 'Scripts', 'platformio.exe'),
    )
  } else {
    possiblePaths.push(
      path.join(homeDir, '.platformio', 'penv', 'bin', 'pio'),
      path.join(homeDir, '.platformio', 'penv', 'bin', 'platformio'),
    )
  }

  for (const pioPath of possiblePaths) {
    if (fs.existsSync(pioPath)) {
      return pioPath
    }
  }

  return null
}

function managePIONewLine(line, spinner) {
  // Look for serial port information
  const portMatch = line.match(/Serial port ([^\r\n]+)/)
  if (portMatch) {
    if (spinner) spinner.stop(false)
    logger.info(`Using serial port: ${colors.yellow}${portMatch[1]}${colors.reset}`)
    if (spinner) spinner.start()
  }

  // Handle writing progress with progress bar
  const progressMatch = line.match(/Writing at 0x[0-9a-fA-F]+\.\.\.\s*\((\d+)\s*%\)/)
  if (progressMatch) {
    const progress = parseInt(progressMatch[1])
    if (spinner) spinner.stop(false)
    drawProgressBar(progress, 'Writing')
    if (progress === 100) {
      // Clear the progress bar line and move cursor to beginning
      process.stdout.write('\r' + ' '.repeat(50) + '\r')
      if (spinner) spinner.start()
    }
    return
  }

  // Show other relevant output (process each line individually)
  if (line.includes('Connecting')) {
    if (spinner) spinner.stop(false)
    logger.info('Connected')
    if (spinner) spinner.start()
  }
}

// Print the underlying pio failure (the spinner/progress view swallows it).
// Detects the classic "serial monitor still open" port conflict explicitly.
function reportUploadFailure(error, what) {
  const output = `${error.stdout || ''}\n${error.stderr || ''}`.trim()
  if (output) {
    const tail = output.split('\n').slice(-25).join('\n')
    logger.error(`${what} output:`)
    console.log(tail)
  } else if (error.error) {
    logger.error(`${what} error: ${error.error.message}`)
  }
  if (/busy|already in use|could not open port|Permission denied/i.test(output)) {
    logger.warning('The serial port looks busy — close any serial monitor / console')
    logger.warning('(VS Code monitor, screen, esptool) holding it, then retry.')
  }
}

// Firmware identity baked via -D (platformio.ini passes FW_VERSION /
// FW_GIT_SHA env through). Best-effort git read; missing values stay unset
// and the firmware reports dev/unknown.
async function firmwareVersionEnv() {
  try {
    const { stdout: version } = await execPromise('git describe --tags --always --dirty')
    const { stdout: sha } = await execPromise('git rev-parse --short HEAD')
    return { FW_VERSION: version.trim(), FW_GIT_SHA: sha.trim() }
  } catch {
    return {}
  }
}

async function pioEnv() {
  return { ...process.env, ...(await firmwareVersionEnv()) }
}

// Stream raw pio output (debug) or spinner + progress parsing (normal).
async function runUpload({ args, label, target, spinner, doneMessage }) {
  const backendPath = path.resolve('backend')
  const pioCmd = await findPIOExecutable()
  const portArg = args.serialPort ? ` --upload-port "${args.serialPort}"` : ''
  const command = `"${pioCmd}" run -t ${target}${portArg}`
  const env = await pioEnv()

  if (args.debug) {
    logger.debug(`Command: ${command}`)
    console.log(colors.dim + `--- pio ${target} output (debug) ---` + colors.reset)
    try {
      await execWithOutput(command, { cwd: backendPath, env }, (line) => {
        process.stdout.write(colors.dim + line + colors.reset + '\n')
      })
    } catch (error) {
      reportUploadFailure(error, label)
      throw error
    }
    console.log()
    return
  }

  spinner.start()
  try {
    await execWithOutput(command, { cwd: backendPath, env }, (line) =>
      managePIONewLine(line, spinner),
    )
    spinner.stop(true, doneMessage)
  } catch (error) {
    spinner.stop(false, `${label} failed`)
    reportUploadFailure(error, label)
    throw error
  }
}

async function uploadBackendPIO(args) {
  let backendUploaded = false

  const shouldUpload = await askQuestion('Upload firmware now? (Y/n)', args.autoYes)

  if (shouldUpload.toLowerCase() === 'n' || shouldUpload.toLowerCase() === 'no') {
    logger.warning('Firmware upload skipped by user')
    logger.info('You can upload later with: pio run -t upload')
    backendUploaded = false
  } else {
    await runUpload({
      args,
      label: 'Firmware upload',
      target: 'upload',
      spinner: new Spinner('Uploading firmware...'),
      doneMessage: 'Firmware uploaded successfully',
    })
    backendUploaded = true

    // For serial upload, we can't easily detect restart, so just inform the user
    logger.info('ESP32 should restart automatically with the new firmware')
  }

  return backendUploaded
}

async function uploadFrontendPIO(args) {
  let frontendUploaded = false

  const shouldUploadFS = await askQuestion('Upload frontend (web files)? (Y/n)', args.autoYes)

  if (shouldUploadFS.toLowerCase() === 'n' || shouldUploadFS.toLowerCase() === 'no') {
    logger.warning('Frontend upload skipped by user')
    logger.info('You can upload later with: pio run -t uploadfs')
    frontendUploaded = false
  } else {
    await runUpload({
      args,
      label: 'Frontend upload',
      target: 'uploadfs',
      spinner: new Spinner('Uploading frontend...'),
      doneMessage: 'Frontend uploaded successfully',
    })
    frontendUploaded = true
  }

  return frontendUploaded
}

async function buildBackendPIO(args = {}) {
  logger.step('Building backend...')
  logger.separator()

  const backendPath = path.resolve('backend')
  const pioCmd = await findPIOExecutable()
  const command = `"${pioCmd}" run`
  const env = await pioEnv()

  logger.debug(`Command: ${command}`)
  logger.debug(`Working directory: ${backendPath}`)

  const startTime = Date.now()

  if (args.debug) {
    // Debug mode: stream all compiler/cmake output live
    console.log(colors.dim + '--- pio output (debug) ---' + colors.reset)
    try {
      await execWithOutput(command, { cwd: backendPath, env }, (line) => {
        process.stdout.write(colors.dim + line + colors.reset + '\n')
      })
    } catch (error) {
      reportBuildFailure(error, backendPath, startTime)
    }
    logger.debug(`Build took ${formatDuration(Date.now() - startTime)}`)
    console.log()
  } else {
    // Build firmware behind a spinner
    const buildSpinner = new Spinner('Building firmware...')
    buildSpinner.start()

    try {
      await execPromise(command, { cwd: backendPath, env })
      buildSpinner.stop(
        true,
        `Firmware built successfully (${formatDuration(Date.now() - startTime)})\n`,
      )
    } catch (error) {
      buildSpinner.stop(false, 'Firmware build failed')
      reportBuildFailure(error, backendPath, startTime)
    }
  }
}

function formatDuration(ms) {
  if (ms < 1000) return `${ms}ms`
  const seconds = ms / 1000
  return seconds < 60
    ? `${seconds.toFixed(1)}s`
    : `${Math.floor(seconds / 60)}m${Math.round(seconds % 60)}s`
}

function reportBuildFailure(error, backendPath, startTime) {
  logger.error('Build error:')
  console.log(error.stderr || error.stdout || 'Unknown error')

  const output = `${error.stderr || ''}\n${error.stdout || ''}`

  if (
    /Directory not empty.*managed_components|managed_components.*Directory not empty/.test(output)
  ) {
    logger.warning(
      `Corrupted component cache detected. Fix with: ${colors.yellow}rm -rf ${path.join(backendPath, 'managed_components')} ${path.join(backendPath, '.pio', 'build')}${colors.reset} then rebuild (stale CMake cache won't re-sync deps otherwise).`,
    )
  }

  logger.error(`Build failed after ${formatDuration(Date.now() - startTime)}`)
  process.exit(1)
}

module.exports = {
  findPIOExecutable,
  uploadBackendPIO,
  uploadFrontendPIO,
  buildBackendPIO,
  formatDuration,
  reportBuildFailure,
  firmwareVersionEnv,
}
