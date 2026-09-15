// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Pure HomeKit helpers (no Svelte runes, unit tested under node:test).

const BASE36 = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ'
const SETUP_CODE_MASK = 0x7ffffff

function base36Decode(s) {
  let v = 0
  for (const ch of s) {
    const i = BASE36.indexOf(ch)
    if (i < 0) return NaN
    v = v * 36 + i
  }
  return v
}

// Decode the 8-digit setup code from an X-HM:// setup payload URI.
// Payload layout (esp_hap_get_setup_payload): "X-HM://" + "00" +
// base36(payload) + setupId(4 chars), where payload low 27 bits = code.
export function homekitSetupCodeFromUri(uri) {
  if (typeof uri !== 'string') return ''
  const prefix = 'X-HM://'
  if (!uri.startsWith(prefix)) return ''
  let body = uri.slice(prefix.length).toUpperCase()
  if (body.startsWith('00')) body = body.slice(2)
  if (body.length < 5) return ''
  const encoded = body.slice(0, -4)
  if (!/^[0-9A-Z]+$/.test(encoded)) return ''
  const payload = base36Decode(encoded)
  if (!Number.isSafeInteger(payload)) return ''
  const code = payload & SETUP_CODE_MASK
  const digits = String(code).padStart(8, '0')
  if (!/^\d{8}$/.test(digits)) return ''
  return digits
}

// Split "84131633" -> ["8413", "1633"] for the stacked official layout.
// Returns ["", ""] when invalid.
export function homekitSetupCodeLines(code) {
  const digits = String(code ?? '').replace(/\D/g, '')
  if (!/^\d{8}$/.test(digits)) return ['', '']
  return [digits.slice(0, 4), digits.slice(4)]
}

// Single-line display "8413 1633" for manual entry captions.
export function formatHomekitSetupCode(code) {
  const [a, b] = homekitSetupCodeLines(code)
  if (!a) return ''
  return `${a} ${b}`
}

// Official setup-label digit art (filled outlines, glyph cell 34x48,
// row step 54 — cf. homekit-code MIT, after maximkulkin/esp-homekit
// gen_qrcode). Rendered as SVG so the label needs no font.
export const HOMEKIT_DIGIT_W = 34
export const HOMEKIT_DIGIT_H = 48
export const HOMEKIT_DIGIT_STEP = 54
export const HOMEKIT_DIGIT_GLYPHS = {
  0: 'M17 48c11 0 17-9 17-24S28 0 17 0 0 9 0 24s6 24 17 24ZM7 24C7 12 10 6 17 6c4 0 7 3 9 8L7 28v-4Zm10 18c-4 0-7-2-9-8l19-14v4c0 12-3 18-10 18Z',
  1: 'M34 48v-6H21V0h-7L0 9v7l13-9h1v35H0v6h34Z',
  2: 'M0 14.4684V14.664H7.02096V14.4684C7.02096 9.222 10.7987 5.73523 16.501 5.73523C22.1677 5.73523 25.8742 8.86354 25.8742 13.6864C25.8742 17.2383 24.2348 19.7149 17.5702 26.3299L0.356394 43.3401V48H34V42.0692H10.7631V41.5153L22.239 30.3381C30.6143 22.3218 33.1803 18.2811 33.1803 13.3279C33.1803 5.40937 26.5157 0 16.7149 0C6.91405 0 0 5.99593 0 14.4684Z',
  3: 'M11 26h6c6 0 10 3 10 8s-4 8-10 8-10-3-10-7H0c0 8 7 13 17 13s17-6 17-14c0-6-4-10-10-11 5-1 8-5 8-11 0-7-6-12-15-12C7 0 1 5 1 13h6c1-5 4-7 10-7 5 0 8 2 8 7s-3 8-9 8h-5v5Z',
  4: 'M21 48h6V38h7v-6h-7V19h-6v13H7L22 0h-7L0 33v5h21v10Z',
  5: 'M17 48c10 0 17-7 17-16s-6-16-16-16c-4 0-8 2-10 4L9 6h22V0H3L1 27h7c1-3 5-5 9-5 6 0 10 4 10 10s-4 10-10 10c-5 0-10-3-10-8H0c0 8 7 14 17 14Z',
  6: 'M34 32c0-9-6-15-15-15-3 0-6 1-8 3l1-3L25 0h-8L7 14c-5 7-7 12-7 18 0 9 7 16 17 16s17-7 17-16ZM17 42c-6 0-10-4-10-10s4-10 10-10 10 4 10 10-4 10-10 10Z',
  7: 'M4 48h8L34 6V0H0v6h27L4 48Z',
  8: 'M17 48c10 0 17-5 17-13 0-6-4-11-10-12v-1c5-1 8-5 8-10 0-7-6-12-15-12S2 5 2 12c0 5 3 9 8 10v1C4 24 0 29 0 35c0 8 7 13 17 13Zm0-28c-5 0-9-3-9-7 0-5 4-8 9-8s9 3 9 8c0 4-4 7-9 7Zm0 23c-6 0-10-4-10-9s4-8 10-8 10 3 10 8-4 9-10 9Z',
  9: 'M0 16c0 9 6 15 15 15 3 0 6-1 8-3l-1 3L9 48h8l10-14c5-7 7-12 7-18 0-9-7-16-17-16S0 7 0 16ZM17 6c6 0 10 4 10 10s-4 10-10 10S7 22 7 16 11 6 17 6Z',
}
