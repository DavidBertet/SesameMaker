// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Improv Wi-Fi provisioning service over the console serial line (ESP Web
// Tools and the CLI speak it; see https://www.improv-wifi.com/serial/).
// Transport-agnostic stdio: USB-Serial-JTAG where the silicon has it,
// plain UART on classic targets (IDF routes stdio per sdkconfig console).
// Shares the console line, so it only reacts to IMPROV-framed packets and
// never logs secrets.
//
// Security model (same as ESPHome): physical USB presence IS the
// authorization — no button dance, which is exactly what lets stock ESP Web
// Tools work out of the box. Beyond that: passwords are never logged, SSID
// at most, and wrong credentials just fail the attempt.

#pragma once

// Start the provisioning task (idempotent). Safe to call before Wi-Fi is
// up; commands that need the radio simply fail gracefully until then.
void improv_serial_start(void);
