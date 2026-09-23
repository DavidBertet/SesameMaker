// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the HomeKit service selection.

#include <assert.h>
#include <stdio.h>

#include "../src/app/homekit_services.h"

static int tests_run = 0;

static void test_secplus_gets_all_services(void)
{
    tests_run++;
    homekit_services_t s = homekit_services_for_caps(protocol_caps_for(PROTOCOL_SECPLUS1));
    assert(s.light && s.lock && s.motion);
}

static void test_drycontact_gets_no_optional_services(void)
{
    tests_run++;
    homekit_services_t s = homekit_services_for_caps(protocol_caps_for(PROTOCOL_DRYCONTACT));
    assert(!s.light && !s.lock && !s.motion);
}

int main(void)
{
    test_secplus_gets_all_services();
    test_drycontact_gets_no_optional_services();
    printf("test_homekit_services: %d test groups passed\n", tests_run);
    return 0;
}
