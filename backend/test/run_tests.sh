#!/bin/sh
# Host-side unit tests for pure backend logic (no ESP-IDF required).
set -e
cd "$(dirname "$0")"
cc -std=c11 -Wall -Wextra -o /tmp/test_secplus1 \
    test_secplus1.c ../src/secplus1.c -lm
/tmp/test_secplus1
cc -std=c11 -Wall -Wextra -o /tmp/test_raw_json \
    test_raw_json.c ../src/raw_json.c
/tmp/test_raw_json
