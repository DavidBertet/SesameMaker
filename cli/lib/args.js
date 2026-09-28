// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

function getParameterValue(args, index) {
  const nextArg = args[index + 1]
  // Check if next argument exists and doesn't start with '-' (isn't another flag)
  return nextArg && !nextArg.startsWith('-') ? nextArg : null
}

function validateArgs(parsedArgs) {
  if (parsedArgs.frontendOnly && parsedArgs.backendOnly) {
    console.error('Error: Cannot specify both --frontend-only and --backend-only')
    process.exit(1)
  }

  if (parsedArgs.serialFlag && parsedArgs.usbFlag) {
    console.error('Error: Cannot specify both --serial and --usb (they are the same)')
    process.exit(1)
  }

  // --ota with an explicit IP is a direct OTA target. --ota without a value, or
  // no flag at all, triggers device discovery. --serial/--usb forces USB-only.
  if (parsedArgs.hasOta && parsedArgs.serialFlag) {
    console.error('Error: Cannot combine --ota with --serial/--usb')
    process.exit(1)
  }

  // Resets are serial-only recovery ops (cable is the auth).
  if (parsedArgs.networkReset && parsedArgs.otaIP) {
    console.error('Error: --network-reset cannot target --ota (use USB/serial)')
    process.exit(1)
  }
  if (parsedArgs.factoryReset && parsedArgs.otaIP) {
    console.error('Error: --factory-reset cannot target --ota (use USB/serial)')
    process.exit(1)
  }

  if (parsedArgs.otaIP) {
    const ipv4Regex = /^(\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3})$/
    if (!ipv4Regex.test(parsedArgs.otaIP)) {
      console.error('Error: --ota flag requires a valid IPv4 address')
      process.exit(1)
    }
  }

  if (parsedArgs.hasUploadPassword && !parsedArgs.uploadPassword) {
    console.error('Error: --upload-password flag requires a password')
    process.exit(1)
  }

  if (parsedArgs.hasWifiSsid && !parsedArgs.wifiSsid) {
    console.error('Error: --wifi-ssid flag requires an SSID')
    process.exit(1)
  }

  if (parsedArgs.wifiSsid && Buffer.byteLength(parsedArgs.wifiSsid, 'utf8') > 32) {
    console.error('Error: --wifi-ssid must be at most 32 bytes')
    process.exit(1)
  }
}

function parseArgs() {
  const args = process.argv.slice(2)
  const otaIndex = args.findIndex((arg) => arg === '-o' || arg === '--ota')
  const passwordIndex = args.findIndex((arg) => arg === '-p' || arg === '--upload-password')
  const wifiSsidIndex = args.findIndex((arg) => arg === '--wifi-ssid')

  // Discovery replaces needing to know the device IP:
  //   • --ota <IP>          → direct OTA to that IP (no discovery)
  //   • --ota (no IP)       → discover OTA targets on the network only
  //   • --serial / --usb    → force USB/serial build (no discovery)
  //   • (no flag)           → discover on both network and USB, pick one
  const serialFlag = args.includes('--serial') || args.includes('-s')
  const usbFlag = args.includes('--usb')
  const hasOta = otaIndex !== -1
  const otaIP = hasOta ? getParameterValue(args, otaIndex) : null
  const usbOnly = serialFlag || usbFlag
  const otaDiscoveryOnly = hasOta && !otaIP
  const discover = !usbOnly && !otaIP

  const parsedArgs = {
    // -h help is managed by the shell script
    hasOta: hasOta,
    otaIP: otaIP,
    serialFlag: serialFlag,
    usbFlag: usbFlag,
    usbOnly: usbOnly,
    otaDiscoveryOnly: otaDiscoveryOnly,
    discover: discover,
    autoYes: args.includes('-y') || args.includes('--yes'),
    frontendOnly: args.includes('-f') || args.includes('--frontend-only'),
    backendOnly: args.includes('-b') || args.includes('--backend-only'),
    networkReset: args.includes('--network-reset'),
    factoryReset: args.includes('--factory-reset'),
    hasUploadPassword: passwordIndex !== -1,
    uploadPassword: passwordIndex !== -1 ? getParameterValue(args, passwordIndex) : null,
    wifiSsid: wifiSsidIndex !== -1 ? getParameterValue(args, wifiSsidIndex) : null,
    hasWifiSsid: wifiSsidIndex !== -1,
    debug: args.includes('-d') || args.includes('--debug'),
    args: args,
  }

  validateArgs(parsedArgs)

  return parsedArgs
}

module.exports = { parseArgs }
