// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the protocol abstraction (ids, strings, caps).

#include <assert.h>
#include <stdio.h>

#include "../src/protocol.h"

static int tests_run = 0;

static void test_id_strings(void)
{
    tests_run++;
    assert(protocol_id_str(PROTOCOL_SECPLUS1) != NULL);
    assert(protocol_id_str(PROTOCOL_DRYCONTACT) != NULL);
    assert(protocol_id_str(PROTOCOL_SECPLUS2) != NULL);

    protocol_id_t id;
    assert(protocol_id_from_str("secplus1", &id) && id == PROTOCOL_SECPLUS1);
    assert(protocol_id_from_str("drycontact", &id) && id == PROTOCOL_DRYCONTACT);
    assert(protocol_id_from_str("secplus2", &id) && id == PROTOCOL_SECPLUS2);
    assert(!protocol_id_from_str("nope", &id));
    assert(!protocol_id_from_str(NULL, &id));
    assert(!protocol_id_from_str("secplus1", NULL));
}

static void test_supported(void)
{
    tests_run++;
    // secplus2 is a stub: known id, no driver yet.
    assert(protocol_id_supported(PROTOCOL_SECPLUS1));
    assert(protocol_id_supported(PROTOCOL_DRYCONTACT));
    assert(!protocol_id_supported(PROTOCOL_SECPLUS2));
}

static void test_caps_secplus1_full(void)
{
    tests_run++;
    protocol_caps_t c = protocol_caps_for(PROTOCOL_SECPLUS1);
    assert(c.light && c.lock && c.obstruction && c.motion && c.panel);
    assert(!c.sensors); // bus feedback, not reeds
}

static void test_caps_drycontact_minimal(void)
{
    tests_run++;
    // The UI contract: dry-contact defines no light/lock/obstruction/motion/
    // panel; door feedback arrives through reed sensors only.
    protocol_caps_t c = protocol_caps_for(PROTOCOL_DRYCONTACT);
    assert(!c.light && !c.lock && !c.obstruction && !c.motion && !c.panel);
    assert(c.sensors);
}

static void test_caps_json(void)
{
    tests_run++;
    protocol_caps_t c = protocol_caps_for(PROTOCOL_DRYCONTACT);
    char buf[160];
    size_t n = protocol_caps_json(&c, buf, sizeof(buf));
    assert(n > 0 && n < sizeof(buf));
    assert(protocol_caps_json(NULL, buf, sizeof(buf)) == 0);
}

int main(void)
{
    test_id_strings();
    test_supported();
    test_caps_secplus1_full();
    test_caps_drycontact_minimal();
    test_caps_json();
    printf("test_protocol: %d passed\n", tests_run);
    return 0;
}
