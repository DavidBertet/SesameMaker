// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <esp_event.h>
#include "esp_netif.h"
#include "esp_pm.h"

#include "constants.h"
#include "storage.h"
#include "captdns.h"
#include "wifi.h"
#include "webserver.h"
#include "spiffs.h"
#include "ntp_sync.h"

#include "websocket.h"
#include "ws_homekit.h"
#include "ws_mqtt.h"
#include "ws_wifi.h"
#include "ws_settings.h"
#include "ws_garage.h"
#include "ws_pins.h"
#include "ws_protocol.h"
#include "ws_log.h"

#include "garage_controller.h"
#include "app_pins.h"
#include "improv_serial.h"
#include "homekit.h"
#include "homekit_garage.h"
#include "protocol_registry.h"
#include "mqtt.h"
#include "mqtt_garage.h"
#include "settings_garage.h"
#include "zigbee.h"
#include "zigbee_garage.h"
#include "ws_zigbee.h"

static const char *TAG = "main";

// Latch the TX pad low across software resets (OTA, esp_restart). The
// IO-MUX hold survives the reset, so GPIO4 never floats through the
// bootloader window where a floating base could key Q1 and the opener
// would read a door-button press. Power cycles still need the base
// pulldown resistor on the board - hold state is lost when power drops.
// The dry-contact relay GPIO is held low for the same reason: a floating
// relay driver at boot reads as a button press to a dumb opener.
static void hold_garage_tx_low(void)
{
  gpio_set_direction(GARAGE_TX_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(GARAGE_TX_GPIO, 0);
  gpio_hold_en(GARAGE_TX_GPIO);
  gpio_set_direction(DRY_RELAY_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(DRY_RELAY_GPIO, 0);
  gpio_hold_en(DRY_RELAY_GPIO);
}

void app_main()
{
  ESP_LOGI(TAG, "IDF version: %s", esp_get_idf_version());

  // Release any hold left by the previous shutdown, then hold the secplus1
  // TX line idle from the very first instruction. Until garage_uart_init()
  // routes the pin, GPIO4 floats during boot/wifi init and can pull the
  // wall line low -> the opener reads a door-button press and toggles the
  // door. Drive the NPN base low so the bus stays high. Same for the
  // dry-contact relay driver so boot never reads as a press.
  gpio_hold_dis(GARAGE_TX_GPIO);
  gpio_set_direction(GARAGE_TX_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(GARAGE_TX_GPIO, 0);
  gpio_hold_dis(DRY_RELAY_GPIO);
  gpio_set_direction(DRY_RELAY_GPIO, GPIO_MODE_OUTPUT);
  gpio_set_level(DRY_RELAY_GPIO, 0);

  // Init NVS storage (never aborts: on persistent flash failure boot
  // continues degraded with defaults instead of panic-looping).
  esp_err_t storage_ret = setup_storage();
  if (storage_ret != ESP_OK)
  {
    ESP_LOGW(TAG, "storage init failed: %s", esp_err_to_name(storage_ret));
  }

  // Init TCP/IP stack
  ESP_ERROR_CHECK(esp_netif_init());
  // Init event mechanism
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  // Power Management framework must be up BEFORE WiFi init: the Wi-Fi/802.15.4
  // coex arbiter needs it to time-slice the single 2.4 GHz radio. 15.4 targets
  // only, so classic chips keep default clocks. light_sleep stays off
  // (Zigbee RX must stay on); WIFI_PS_MIN_MODEM gives the slice windows.
#ifdef CONFIG_SOC_IEEE802154_SUPPORTED
  esp_pm_config_t pm_cfg = {
      .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
      .min_freq_mhz = 80,
      .light_sleep_enable = false,
  };
  ESP_ERROR_CHECK(esp_pm_configure(&pm_cfg));
#endif

  // Init file storage
  ESP_ERROR_CHECK(setup_spiffs());

  // Setup captive portal - automatically opens the page when we connect to the wifi
  // setup_captive_dns();

  // Setup wifi access point
  setup_wifi();

  // Setup HTTP server
  setup_server();

  // Init hardware
  ESP_ERROR_CHECK(protocol_registry_init());
  ESP_ERROR_CHECK(garage_controller_init());

  // Product content registers before any transport serves it.
  settings_garage_register();

  // Register websocket callbacks
  register_callback("wifi_status", ws_handle_wifi_status);
  register_callback("wifi_scan", ws_handle_wifi_scan);
  register_callback("wifi_connect", ws_handle_wifi_connect);
  register_callback("wifi_disconnect", ws_handle_wifi_disconnect);

  register_callback("get_settings", ws_handle_get_settings);
  register_callback("time_update", ws_handle_time_update);
  register_callback("get_system_info", ws_handle_system_info);

  register_callback("get_garage_status", ws_handle_get_garage_status);
  register_callback("door_command", ws_handle_garage_door_command);
  register_callback("light_command", ws_handle_garage_light_command);
  register_callback("lock_command", ws_handle_garage_lock_command);
  register_callback("get_garage_raw", ws_handle_get_garage_raw);
  register_callback("garage_sync", ws_handle_garage_sync);

  register_callback("get_protocol", ws_handle_get_protocol);
  register_callback("set_protocol", ws_handle_set_protocol);

  // Live pin inspector: app declares the table, core samples + formats.
  app_pins_register();
  register_callback("get_gpio_state", ws_handle_get_gpio_state);
  register_callback("bus_capture_start", ws_handle_bus_capture_start);
  register_callback("bus_capture_stop", ws_handle_bus_capture_stop);

  register_callback("get_mqtt_config", ws_handle_get_mqtt_config);
  register_callback("set_mqtt_config", ws_handle_set_mqtt_config);

  register_callback("get_zigbee_config", ws_handle_get_zigbee_config);
  register_callback("set_zigbee_config", ws_handle_set_zigbee_config);
  register_callback("zigbee_pair", ws_handle_zigbee_pair);
  register_callback("zigbee_leave", ws_handle_zigbee_leave);
  register_callback("zigbee_reset", ws_handle_zigbee_reset);

  register_callback("get_homekit", ws_handle_get_homekit);
  register_callback("set_homekit", ws_handle_set_homekit);

  register_callback("log_start", ws_handle_log_start);
  register_callback("log_stop", ws_handle_log_stop);
  ws_log_init();

  // Hold TX low across any software reset (see hold_garage_tx_low).
  ESP_ERROR_CHECK(esp_register_shutdown_handler(hold_garage_tx_low));

  // Start the garage door controller
  ESP_ERROR_CHECK(garage_controller_start());

  // MQTT bridge (subscribes to {prefix}/set, publishes {prefix}/state).
  // Garage content registers first so (re)connects pick it up.
  mqtt_garage_register();
  ESP_ERROR_CHECK(mqtt_init());

  // Zigbee bridge (C6 only; no-op stub on classic builds).
  // Garage content registers first so stack startup picks it up.
  zigbee_garage_register();
  ESP_ERROR_CHECK(zigbee_init());
  zigbee_button_init();

  // USB-serial Improv Wi-Fi provisioning (ESP Web Tools + CLI). Runs its
  // own task; needs nothing but the scheduler.
  improv_serial_start();

  // HomeKit accessory. Content registers first so HAP startup picks it up;
  // the task waits for Wi-Fi itself.
  homekit_garage_register();
  homekit_start();

  // Retrieve time from network
  start_ntp_sync();
}
