// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Build-time WiFi provisioning. Credentials go to gitignored secrets.h
// (via secrets.js) so tracked constants.h stays committable.

const fs = require('fs')
const crypto = require('crypto')
const { askPassword, askQuestion } = require('./prompt')
const { logger } = require('./logger')
const { SECRETS_HEADER, secretsTemplate, upsertSecret, readSecret } = require('./secrets')

function generateWifiGen() {
  return crypto.randomBytes(4).toString('hex')
}

function readDefaultWifi(headerPath = SECRETS_HEADER) {
  let content
  try {
    content = fs.readFileSync(headerPath, 'utf8')
  } catch {
    return { ssid: null, hasPassword: false, gen: null }
  }
  const password = readSecret(content, 'DEFAULT_WIFI_PASSWORD')
  return {
    ssid: readSecret(content, 'DEFAULT_WIFI_SSID'),
    hasPassword: Boolean(password && password.length > 0),
    gen: readSecret(content, 'DEFAULT_WIFI_GEN'),
  }
}

function injectWifiCredentials(ssid, password, headerPath = SECRETS_HEADER) {
  let content
  try {
    content = fs.readFileSync(headerPath, 'utf8')
  } catch {
    content = secretsTemplate()
  }
  let updated = upsertSecret(content, 'DEFAULT_WIFI_SSID', ssid)
  updated = upsertSecret(updated, 'DEFAULT_WIFI_PASSWORD', password)
  // Bump the generation marker so firmware can tell fresh provisioning apart
  // from a plain rebuild: a new gen overrides NVS + forget flag once.
  updated = upsertSecret(updated, 'DEFAULT_WIFI_GEN', generateWifiGen())
  fs.writeFileSync(headerPath, updated)
  return true
}

// Runs after USB/network target selection (checkPrerequisites) and before the
// backend build. Only acts when --wifi-ssid was passed:
//   • New SSID (or stored without password) → secure hidden password prompt.
//   • SSID already stored with a password → ask whether to reuse it; on "no"
//     (password changed since last install) prompt securely and re-inject,
//     which bumps DEFAULT_WIFI_GEN so firmware overrides once.
// Password is never accepted as a CLI flag so it stays out of shell history.
async function configureWifi(
  args,
  headerPath = SECRETS_HEADER,
  ask = askPassword,
  confirm = askQuestion,
) {
  if (!args.wifiSsid) {
    return null
  }

  const stored = readDefaultWifi(headerPath)
  const fresh = stored.ssid !== args.wifiSsid || !stored.hasPassword

  if (!fresh) {
    console.log()
    let reuse = args.autoYes
    if (!args.autoYes) {
      const answer = await confirm(`Reuse stored password for "${args.wifiSsid}"? (Y/n)`)
      reuse = answer.toLowerCase() !== 'n' && answer.toLowerCase() !== 'no'
    }
    if (reuse) {
      logger.info(`WiFi SSID "${args.wifiSsid}" already configured, reusing stored password`)
      return { ssid: args.wifiSsid, injected: false }
    }
  } else if (args.autoYes) {
    logger.error('--wifi-ssid needs an interactive password prompt (remove -y/--yes)')
    process.exit(1)
  }
  logger.info(`Configuring WiFi for SSID "${args.wifiSsid}" (hidden input, empty aborts)`)
  let password = ''
  for (let attempt = 0; attempt < 3 && !password; attempt++) {
    password = await ask(`Enter WiFi password for "${args.wifiSsid}":`)
    if (!password && attempt < 2) {
      logger.warning('Empty password, try again (Ctrl+C to abort)')
    }
  }
  if (!password) {
    logger.error('No WiFi password entered, aborting')
    process.exit(1)
  }

  if (args.frontendOnly) {
    return { ssid: args.wifiSsid, injected: false }
  }

  injectWifiCredentials(args.wifiSsid, password, headerPath)
  console.log()
  logger.info(`WiFi credentials configured for "${args.wifiSsid}"`)
  return { ssid: args.wifiSsid, injected: true }
}

module.exports = {
  SECRETS_HEADER,
  readDefaultWifi,
  injectWifiCredentials,
  configureWifi,
  generateWifiGen,
}
