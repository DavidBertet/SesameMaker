// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
// Host-side unit tests for the id-keyed event dispatcher.

#include "../src/core/event_routes.h"

#include <assert.h>
#include <stdio.h>

static int tests_run = 0;
static int s_calls = 0;
static void *s_last_data = NULL;

static void record_call(void *event_data)
{
    s_calls++;
    s_last_data = event_data;
}

static void test_match_dispatches(void)
{
    tests_run++;
    static const event_route_t routes[] = {
        {10, record_call},
        {20, record_call},
    };
    s_calls = 0;
    s_last_data = NULL;
    int marker = 42;
    assert(event_routes_dispatch(routes, 2, 20, &marker));
    assert(s_calls == 1 && s_last_data == &marker);
}

static void test_miss_calls_nothing(void)
{
    tests_run++;
    static const event_route_t routes[] = {
        {10, record_call},
    };
    s_calls = 0;
    assert(!event_routes_dispatch(routes, 1, 99, NULL));
    assert(s_calls == 0);
    assert(!event_routes_dispatch(NULL, 0, 10, NULL));
}

static void test_first_match_wins(void)
{
    tests_run++;
    s_calls = 0;
    // Two entries, same id: only the first may run.
    static const event_route_t routes[] = {
        {7, record_call},
        {7, record_call},
    };
    assert(event_routes_dispatch(routes, 2, 7, NULL));
    assert(s_calls == 1);
}

int main(void)
{
    test_match_dispatches();
    test_miss_calls_nothing();
    test_first_match_wins();
    printf("test_event_routes: %d tests passed\n", tests_run);
    return 0;
}
