// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const fs = require('fs')
const path = require('path')
const crypto = require('crypto')

const DEFAULT_STORAGE_PATH = path.resolve(__dirname, '..', '..', '.ota_password')
const CONSTANTS_HEADER = path.resolve(__dirname, '..', '..', 'backend', 'src', 'constants.h')
const OTA_PASSWORD_REGEX = /#define\s+OTA_PASSWORD\s+"[^"]*"/

function generateOtaPassword(length = 16) {
  return crypto
    .randomBytes(Math.ceil((length * 3) / 4))
    .toString('base64url')
    .slice(0, length)
}

function readStoredPassword(storagePath = DEFAULT_STORAGE_PATH) {
  try {
    const password = fs.readFileSync(storagePath, 'utf8').trim()
    return password || null
  } catch (error) {
    return null
  }
}

function writeStoredPassword(password, storagePath = DEFAULT_STORAGE_PATH) {
  fs.mkdirSync(path.dirname(storagePath), { recursive: true })
  fs.writeFileSync(storagePath, password + '\n', { mode: 0o600 })
}

function resolveOtaPassword(providedPassword, storagePath = DEFAULT_STORAGE_PATH) {
  if (providedPassword) {
    writeStoredPassword(providedPassword, storagePath)
    return providedPassword
  }
  const stored = readStoredPassword(storagePath)
  if (stored) {
    return stored
  }
  const generated = generateOtaPassword()
  writeStoredPassword(generated, storagePath)
  return generated
}

function escapeCString(value) {
  return value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')
}

function injectOtaPassword(password, headerPath = CONSTANTS_HEADER) {
  const content = fs.readFileSync(headerPath, 'utf8')
  const escaped = escapeCString(password)
  let updated
  if (OTA_PASSWORD_REGEX.test(content)) {
    updated = content.replace(OTA_PASSWORD_REGEX, `#define OTA_PASSWORD "${escaped}"`)
  } else {
    updated = content + `\n#define OTA_PASSWORD "${escaped}"\n`
  }
  fs.writeFileSync(headerPath, updated)
  return updated !== content
}

module.exports = {
  DEFAULT_STORAGE_PATH,
  CONSTANTS_HEADER,
  generateOtaPassword,
  readStoredPassword,
  writeStoredPassword,
  resolveOtaPassword,
  escapeCString,
  injectOtaPassword,
}
