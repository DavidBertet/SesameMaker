// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Improv Wi-Fi serial protocol codec (https://www.improv-wifi.com/serial/).
// Pure byte framing + RPC payload builders/parsers, no ESP-IDF includes so
// it stays host-testable; the USB-serial task using it lives in
// improv_serial.c.
//
// Wire format (all multi-byte values single bytes, strings raw UTF-8):
//   'I','M','P','R','O','V', 0x01, type, len, data..., checksum, '\n'
// where checksum is the sum of every preceding byte mod 256. The trailing
// newline keeps console logs on the shared USB line readable, and neither
// log output nor commands ever start with the magic, so both sides resync
// through noise.
//
// SesameMaker extension: RPC 0x10 sets the OTA upload password
// ([pass_len][pass], 0 clears it). RPC 0x11 reads it back (USB-physical
// only; anyone holding the cable could overwrite it or flash anything
// anyway, so reading adds no capability — it lets install.sh print the
// effective password). RPC 0x12 resets network identity (forgets
// WiFi, deletes the OTA password key so a fresh one generates at next
// boot, then reboots). RPC 0x13 is the full factory reset: erases default
// NVS (WiFi + all app settings) and the Zigbee/HomeKit partitions, then
// reboots. Stock clients never send them and answer
// "unknown RPC" if asked; our CLI speaks them.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Packet types (byte 8).
#define IMPROV_TYPE_STATE 0x01  // device -> client, 1 byte payload
#define IMPROV_TYPE_ERROR 0x02  // device -> client, 1 byte payload
#define IMPROV_TYPE_RPC 0x03    // client -> device
#define IMPROV_TYPE_RESULT 0x04 // device -> client

// Current-state values (0x01 payload).
#define IMPROV_STATE_STOPPED 0x00     // provisioning unavailable
#define IMPROV_STATE_AUTHORIZED 0x02  // ready for credentials
#define IMPROV_STATE_PROVISIONING 0x03
#define IMPROV_STATE_PROVISIONED 0x04

// Error-state values (0x02 payload).
#define IMPROV_ERROR_NONE 0x00
#define IMPROV_ERROR_INVALID_RPC 0x01 // malformed packet
#define IMPROV_ERROR_UNKNOWN_RPC 0x02 // unknown command
#define IMPROV_ERROR_UNABLE_TO_CONNECT 0x03
#define IMPROV_ERROR_BAD_HOSTNAME 0x05
#define IMPROV_ERROR_UNKNOWN 0xFF

// RPC commands (0x03 payload byte 1).
#define IMPROV_RPC_WIFI_SETTINGS 0x01
#define IMPROV_RPC_GET_STATE 0x02
#define IMPROV_RPC_GET_INFO 0x03
#define IMPROV_RPC_GET_NETWORKS 0x04
#define IMPROV_RPC_GET_HOSTNAME 0x05
#define IMPROV_RPC_GET_DEVNAME 0x06
#define IMPROV_RPC_GET_NETSTATE 0x07
// SesameMaker extension: set the OTA upload password.
#define IMPROV_RPC_SET_OTA_PASSWORD 0x10
// SesameMaker extension: read it back (USB-physical only; anyone holding
// the cable could overwrite it or flash anything anyway, so reading adds
// no capability — it lets install.sh print the effective password).
#define IMPROV_RPC_GET_OTA_PASSWORD 0x11
// SesameMaker extension: network reset (empty payload, empty result).
// Forgets WiFi, deletes the OTA password key, then reboots.
#define IMPROV_RPC_NETWORK_RESET 0x12
// SesameMaker extension: full factory reset (empty payload, empty result).
// Erases default NVS (WiFi + all app settings) and the Zigbee/HomeKit
// partitions, then reboots.
#define IMPROV_RPC_FACTORY_RESET 0x13

#define IMPROV_SSID_MAX 32
#define IMPROV_PASS_MAX 64
#define IMPROV_OTA_PASS_MAX 64

// Largest receivable frame: 6 magic + ver + type + len + 255 data + cksum.
#define IMPROV_FRAME_MAX 272

// Build one frame (header + data + checksum + '\n') into out.
// Returns total bytes, 0 when len > 255 or max too small.
size_t improv_frame(uint8_t type, const uint8_t *data, size_t len,
                    uint8_t *out, size_t max);

// Byte-stream parser. Feed incoming bytes; returns true once a complete,
// checksum-valid frame is ready (accessors below). A completed result stays
// latched until the next completed frame replaces it, so callers feeding
// whole chunks (framing bytes, trailing newlines, log noise included) can
// dispatch immediately on true. Version mismatch, checksum failure and
// overruns resync silently (return false); callers should additionally
// reset on idle gaps (no clock in here).
typedef struct
{
    uint8_t buf[IMPROV_FRAME_MAX];
    size_t pos;
    uint8_t type;
    uint8_t len;
    bool ready;
} improv_parser_t;

void improv_parser_init(improv_parser_t *p);
bool improv_parser_feed(improv_parser_t *p, uint8_t byte);
uint8_t improv_frame_type(const improv_parser_t *p);
const uint8_t *improv_frame_data(const improv_parser_t *p);
size_t improv_frame_len(const improv_parser_t *p);

// RPC command envelope [cmd][len][payload...]: split data into command id
// and payload. False when data is shorter than 2 bytes or the declared
// payload length overruns.
bool improv_rpc_parse(const uint8_t *data, size_t len, uint8_t *cmd_out,
                      const uint8_t **payload, size_t *payload_len);

// WIFI_SETTINGS payload [ssid_len][ssid][pass_len][pass]: copy out
// NUL-terminated strings. False on truncation, overrun, empty ssid, or
// lengths beyond ssid_max/pass_max (buffers must hold max+1).
bool improv_wifi_settings_parse(const uint8_t *data, size_t len, char *ssid,
                                size_t ssid_max, char *pass, size_t pass_max);

// SET_OTA_PASSWORD payload [pass_len][pass]: copy out NUL-terminated
// (length 0 clears the password). False on truncation, overrun, or lengths
// beyond pass_max (buffer must hold max+1).
bool improv_ota_password_parse(const uint8_t *data, size_t len, char *pass,
                               size_t pass_max);

// RPC result payload [cmd][len][slen str]... for n strings.
// Returns payload bytes, 0 on overflow (out too small).
size_t improv_result_payload(uint8_t cmd, const char *const *strs, size_t n,
                             uint8_t *out, size_t max);
