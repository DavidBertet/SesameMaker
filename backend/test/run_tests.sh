#!/bin/sh
# Host-side unit tests for pure backend logic (no ESP-IDF required).
set -e
cd "$(dirname "$0")"
cc -std=c11 -Wall -Wextra -o /tmp/test_secplus1 \
    test_secplus1.c ../src/app/secplus1.c -lm
/tmp/test_secplus1
# Native secplus2 wireline codec (MIT, src/app/secplus2.c).
cc -std=c11 -Wall -Wextra -o /tmp/test_secplus2 \
    test_secplus2.c ../src/app/secplus2.c -lm
/tmp/test_secplus2
cc -std=c11 -Wall -Wextra -I../src/core -o /tmp/test_raw_json \
    test_raw_json.c ../src/app/raw_json.c
/tmp/test_raw_json
cc -std=c11 -Wall -Wextra -o /tmp/test_protocol \
    test_protocol.c
/tmp/test_protocol
cc -std=c11 -Wall -Wextra -o /tmp/test_drycontact \
    test_drycontact.c ../src/app/drycontact.c
/tmp/test_drycontact
cc -std=c11 -Wall -Wextra -o /tmp/test_zigbee \
    test_zigbee.c
/tmp/test_zigbee
cc -std=c11 -Wall -Wextra -o /tmp/test_zigbee_rejoin \
    test_zigbee_rejoin.c
/tmp/test_zigbee_rejoin
cc -std=c11 -Wall -Wextra -o /tmp/test_zigbee_lqi \
    test_zigbee_lqi.c
/tmp/test_zigbee_lqi
cc -std=c11 -Wall -Wextra -o /tmp/test_zigbee_bdb \
    test_zigbee_bdb.c
/tmp/test_zigbee_bdb
cc -std=c11 -Wall -Wextra -o /tmp/test_channel_config \
    test_channel_config.c
/tmp/test_channel_config
cc -std=c11 -Wall -Wextra -o /tmp/test_zb_attr \
    test_zb_attr.c
/tmp/test_zb_attr
cc -std=c11 -Wall -Wextra -o /tmp/test_zigbee_ieee \
    test_zigbee_ieee.c
/tmp/test_zigbee_ieee
cc -std=c11 -Wall -Wextra -I../src/core -o /tmp/test_pin_inspector \
    test_pin_inspector.c ../src/core/inspector/pin_inspector.c
/tmp/test_pin_inspector
cc -std=c11 -Wall -Wextra -o /tmp/test_homekit_services \
    test_homekit_services.c
/tmp/test_homekit_services
cc -std=c11 -Wall -Wextra -o /tmp/test_ap_window \
    test_ap_window.c
/tmp/test_ap_window
