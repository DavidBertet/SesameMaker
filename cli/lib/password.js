// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// OTA password handling. No files involved: explicit -p wins, otherwise
// generate one for the session. Serial installs send it over USB
// (usb_provision.js); OTA targets already carry whatever was provisioned
// before, and the upload uses it for auth either way.

const crypto = require('crypto')

function generateOtaPassword(length = 16) {
  return crypto
    .randomBytes(Math.ceil((length * 3) / 4))
    .toString('base64url')
    .slice(0, length)
}

function resolveOtaPassword(providedPassword) {
  return providedPassword || generateOtaPassword()
}

module.exports = {
  generateOtaPassword,
  resolveOtaPassword,
}
