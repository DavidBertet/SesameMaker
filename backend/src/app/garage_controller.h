// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "protocol.h"
#include "secplus1.h"
#include "secplus2.h"

typedef enum
{
    GARAGE_LIGHT_UNKNOWN = -1,
    GARAGE_LIGHT_OFF = 0,
    GARAGE_LIGHT_ON = 1,
} garage_light_state_t;

typedef enum
{
    GARAGE_LOCK_UNKNOWN = -1,
    GARAGE_LOCK_UNLOCKED = 0,
    GARAGE_LOCK_LOCKED = 1,
} garage_lock_state_t;

typedef enum
{
    GARAGE_PANEL_WAITING = 0, // listening for an existing wall panel
    GARAGE_PANEL_DETECTED,    // real panel on the bus, we just snoop
    GARAGE_PANEL_EMULATING,   // no panel found, we emulate one
} garage_panel_mode_t;

typedef struct
{
    secplus1_door_state_t door_state;
    secplus1_door_state_t maybe_door_state; // pending confirmation
    garage_light_state_t light_state;
    garage_light_state_t maybe_light_state;
    garage_lock_state_t lock_state;
    garage_lock_state_t maybe_lock_state;
    bool obstruction;
    bool motion;
    bool is_0x37_panel;
    garage_panel_mode_t panel_mode;
    uint32_t last_status_ms;
    bool door_moving; // we triggered a toggle and expect motion
    // Active protocol + caps (refreshed from the registry). Reed sensor
    // inputs are only meaningful when caps.sensors is true (dry-contact).
    protocol_id_t protocol;
    protocol_caps_t caps;
    bool open_limit;
    bool close_limit;
    bool sensors_valid;
} garage_state_t;

esp_err_t garage_controller_init(void);
esp_err_t garage_controller_start(void);
// Re-read the active protocol/caps from the registry (after set_protocol).
void garage_controller_refresh_protocol(void);
// Shared garage_status JSON builder (single user + broadcast + tests).
// Includes protocol, caps and (when caps.sensors) reed sensor state.
size_t garage_controller_get_status_json(char *buf, size_t len);

// High level actions. `action` is one of "toggle","open","close","stop".
esp_err_t garage_controller_door_action(const char *action);
esp_err_t garage_controller_light_action(const char *action); // "toggle","on","off"
esp_err_t garage_controller_lock_action(const char *action);  // "toggle","lock","unlock"

esp_err_t garage_controller_get_state(garage_state_t *out);
// Protocol-driver upcalls: dry-contact reports reed/door snapshots here,
// secplus2 reports bus status/motion here. The controller owns broadcast +
// MQTT; drivers never send WS frames.
void garage_controller_report_door(secplus1_door_state_t door, bool moving);
void garage_controller_report_sensors(bool open_hit, bool close_hit, bool valid);
void garage_controller_report_secplus2_status(const secplus2_status_t *st);
void garage_controller_report_motion(void);
esp_err_t garage_controller_sync(void); // force a status query round
bool garage_controller_light_on(void);
bool garage_controller_locked(void);
