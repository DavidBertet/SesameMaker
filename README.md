# SesameMaker: ESP32 Garage Door Controller

<div align="center">
  <img src="https://img.shields.io/badge/version-1.0.0-blue" alt="Version">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="License">
  <img src="https://img.shields.io/badge/status-active-brightgreen" alt="Status">
</div><br/>

Open sesame - an ESP32 that speaks Security+ 1.0 to your LiftMaster/Chamberlain garage door opener over the **2-wire wall-control bus**, and gives you a web UI with door state, light and lock control, plus a raw protocol inspector.

## 🌟 What it does

- **Door control**: open / close / stop / toggle from the web UI, over WebSocket
- **Door state**: open / closed / opening / closing / stopped, decoded from the bus
- **Light & lock**: turn the opener light on/off, lock out wireless remotes
- **Obstruction & motion**: photo-eye obstruction events and panel motion
- **Wall panel aware**: snoops the bus when a real wall panel is present; automatically **emulates one** if none is found after 35 s
- **Protocol inspector**: live RX/TX byte log with decoded meaning - confirm your wiring end-to-end from the browser

Try the [web sample](https://davidbertet.github.io/SesameMaker/) to see what it is capable of!

[![Web Sample](./assets/programs_preview.jpg)](https://davidbertet.github.io/SesameMaker/)

## ⚙️ How it works

Security+ 1.0 (2-wire wall control) is a **1200 baud, 8E1, half-duplex UART**:

- Panel → opener commands are single bytes: `0x30/0x31` door press/release, `0x32/0x33` light, `0x34/0x35` lock, `0x38` query door status, `0x3A` query light/lock, `0x39` obstruction
- Opener → panel replies are two bytes; e.g. door state = `resp & 0x7` (0/6 stopped, 1 opening, 2 open, 4 closing, 5 closed)
- Protocol details from the open-source [ratgdo](https://github.com/ratgdo/esphome-ratgdo) project; this firmware is an independent ESP-IDF implementation

## 🔌 Wiring

The wall bus is **not** 3.3 V logic - never connect an ESP32 GPIO to it directly.

- **TX**: open-collector driver (NPN transistor or optocoupler) that pulls the wall line low; GPIO4 → base through ~1 kΩ
- **RX**: voltage divider scaling the wall line down to ≤ 3.3 V into GPI16 (measure your line voltage first - these are typically in the 5–24 V range)
- Connect to the same two terminals as your existing wall button (leave the button connected - the bus is shared)

## 🚀 Quick Start

1. **Build the frontend** (outputs to `backend/data/`)

   ```shell
   cd frontend && npm install && npm run build
   ```

2. **Install on your ESP32-C3**

   ```shell
   ./install.sh
   ```

3. **Update over the air**

   ```shell
   ./install.sh --ota <IP>
   ```

4. **Verify**: connect to the device's WiFi ("SesameMaker"), open the web UI. The **Protocol** tab shows live bus traffic; the **Garage Door** tab gives you door/light/lock controls.

## 🧪 Tests

- `npm run test` - CLI unit tests (node:test), from the repo root
- `npm run test:backend` - host-side unit tests for the secplus1 protocol (decode, framing, door pursuit, panel emulation)
- `cd frontend && npm test` - helpers, status parsing, mock transitions

## 🎨 Linting

The CLI follows the repo's `.prettierrc` (no semicolons, single quotes). Format and check it with:

- `npm run format` - apply Prettier to the `cli/` code
- `npm run lint` - check that `cli/` matches Prettier style (fails CI if not)

Both the CLI lint/format and CLI tests run in CI (`lint-and-test` job) on every push to `main`.

## 🛠️ Tech Stack

- Svelte 5 + shadcn-svelte frontend
- ESP-IDF backend (PlatformIO) with static file serving
- WebSocket interface for real-time status

## 📁 Folder Architecture

```
SesameMaker/
├── frontend/    # Svelte web application
├── backend/     # ESP-IDF backend code
├── cli/         # CLI tool for install/update
└── assets/      # Images and media for docs
```

## 📝 License

This project is licensed under the MIT License.
