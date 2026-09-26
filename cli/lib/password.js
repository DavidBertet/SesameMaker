// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// OTA password handling. No files involved: the effective value is resolved
// per run (explicit -p, else the device's own over USB, else generated).
// This module only mints random ones.

const crypto = require('crypto')

function generateOtaPassword(length = 16) {
  return crypto
    .randomBytes(Math.ceil((length * 3) / 4))
    .toString('base64url')
    .slice(0, length)
}

module.exports = {
  generateOtaPassword,
}
