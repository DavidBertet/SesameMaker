// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const fs = require('fs')
const path = require('path')
const { execPromise, execWithOutput } = require('./system')
const { Spinner } = require('./spinner')
const { logger, colors } = require('./logger')

// Run a step behind a spinner, or stream its output live in debug mode.
async function runStep(label, successMsg, command, options, debug) {
  if (debug) {
    logger.debug(`Command: ${command}`)
    console.log(colors.dim + `--- ${label} (debug) ---` + colors.reset)
    await execWithOutput(command, options, (line) => {
      process.stdout.write(colors.dim + line + colors.reset + '\n')
    })
    console.log()
    return
  }

  const spinner = new Spinner(`${label}...`)
  spinner.start()
  try {
    await execPromise(command, options)
    spinner.stop(true, successMsg)
  } catch (error) {
    spinner.stop(false, `${label} failed`)
    throw error
  }
}

async function buildFrontend(args = {}) {
  logger.step('Building frontend...')
  logger.separator()

  const frontendPath = path.resolve('frontend')
  const debug = args.debug

  // Check if node_modules exists
  if (!fs.existsSync(path.join(frontendPath, 'node_modules'))) {
    try {
      await runStep(
        'Installing frontend dependencies',
        'Frontend dependencies installed',
        'npm install',
        { cwd: frontendPath },
        debug,
      )
    } catch (error) {
      logger.error(error.stderr || error.stdout || 'Unknown error')
      process.exit(1)
    }
  } else {
    logger.info('Frontend dependencies already installed')
  }

  // Build frontend
  try {
    await runStep(
      'Building frontend',
      'Frontend built successfully',
      'npm run build',
      { cwd: frontendPath },
      debug,
    )
  } catch (error) {
    logger.error(error.stderr || error.stdout || 'Unknown error')
    process.exit(1)
  }

  // Copy built files to backend/data
  const copySpinner = new Spinner('Copying files to backend/data...')
  copySpinner.start()

  try {
    // Ensure backend/data directory exists
    const dataDir = path.resolve('backend/data')
    if (!fs.existsSync(dataDir)) {
      fs.mkdirSync(dataDir, { recursive: true })
    }

    // Copy dist folder contents to backend/data
    const distPath = path.join(frontendPath, 'dist')
    if (fs.existsSync(distPath)) {
      await execPromise(`cp -r ${path.join(distPath, '*')} ${dataDir}/`)
      copySpinner.stop(true, 'Files copied to backend/data')
    } else {
      copySpinner.stop(false, 'Frontend dist folder not found')
      process.exit(1)
    }
  } catch (error) {
    copySpinner.stop(false, 'Failed to copy files')
    logger.error(error.stderr || error.stdout || 'Unknown error')
    process.exit(1)
  }

  console.log()
}

module.exports = { buildFrontend }
