// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// OTA password handling. Gitignored secrets.h is the single source of truth:
// explicit -p wins, otherwise reuse the baked value, otherwise generate one.

const fs = require('fs')
const crypto = require('crypto')

const {
  SECRETS_HEADER,
  secretsTemplate,
  upsertSecret,
  readSecret,
  escapeCString,
} = require('./secrets')

function generateOtaPassword(length = 16) {
  return crypto
    .randomBytes(Math.ceil((length * 3) / 4))
    .toString('base64url')
    .slice(0, length)
}

function readSecretsPassword(secretsPath = SECRETS_HEADER) {
  try {
    const password = readSecret(fs.readFileSync(secretsPath, 'utf8'), 'OTA_PASSWORD')
    return password || null
  } catch {
    return null
  }
}

function resolveOtaPassword(providedPassword, secretsPath = SECRETS_HEADER) {
  if (providedPassword) {
    injectOtaPassword(providedPassword, secretsPath)
    return providedPassword
  }
  const stored = readSecretsPassword(secretsPath)
  if (stored) {
    return stored
  }
  const generated = generateOtaPassword()
  injectOtaPassword(generated, secretsPath)
  return generated
}

function injectOtaPassword(password, headerPath = SECRETS_HEADER) {
  let content
  try {
    content = fs.readFileSync(headerPath, 'utf8')
  } catch {
    content = secretsTemplate()
  }
  const updated = upsertSecret(content, 'OTA_PASSWORD', password)
  if (updated !== content) {
    fs.writeFileSync(headerPath, updated)
    return true
  }
  return false
}

module.exports = {
  SECRETS_HEADER,
  generateOtaPassword,
  readSecretsPassword,
  resolveOtaPassword,
  escapeCString,
  injectOtaPassword,
}
