// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  dim: '\x1b[2m',
  red: '\x1b[31m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m',
  cyan: '\x1b[36m',
  white: '\x1b[37m',
  // 256-color orange/amber, used for the picker highlight so it stands out.
  orange: '\x1b[38;5;208m',
}

let debugEnabled = false

function setDebug(enabled) {
  debugEnabled = Boolean(enabled)
}

function isDebug() {
  return debugEnabled
}

const logger = {
  info: (msg) => console.log(`${colors.blue}ℹ${colors.reset} ${msg}`),
  success: (msg) => console.log(`${colors.green}✓${colors.reset} ${msg}`),
  error: (msg) => console.log(`${colors.red}✗${colors.reset} ${msg}`),
  warning: (msg) => console.log(`${colors.yellow}⚠${colors.reset} ${msg}`),
  step: (msg) => console.log(`${colors.cyan}${colors.bright}🚀 ${msg}${colors.reset}`),
  separator: () => console.log(`${colors.magenta}${'─'.repeat(60)}${colors.reset}`),
  debug: (msg) => {
    if (debugEnabled) console.log(`${colors.dim}🐛 ${msg}${colors.reset}`)
  },
}

module.exports = { logger, colors, setDebug, isDebug }
