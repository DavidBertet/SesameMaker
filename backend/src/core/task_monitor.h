// Copyright (c) 2026 David Bertet. Licensed under the MIT License.
//
// Task stack monitor: answers "does this task need more stack, or is it
// slowly filling up?" by sampling the kernel stack high-water mark.
//
// HWM is a historical minimum (free words, never recovers), which makes it
// a poor live-health badge but a perfect fill detector: sample it
// periodically and any drop is stack consumed since monitoring began.
// Per task we keep first-seen and latest HWM plus timestamps:
//   min_free_words  = latest HWM (absolute floor, what an overflow check needs)
//   drift_words     = first - latest (consumed since boot monitoring began)
//   rate_wph        = drift per hour (fill rate; 0 = flat since boot)
// No stack totals are reported: they are not queryable from TaskStatus_t,
// and percentages without totals were the old BS. Thresholds below are in
// absolute free words.

#pragma once

#include <stddef.h>

// Sample period and table sizing.
#define TASKMON_PERIOD_S 30
#define TASKMON_MAX_TASKS 32
#define TASKMON_MISS_DROP 5

// Absolute free-word thresholds (ESP32 words are 4 bytes; ISR nesting can
// need a few hundred bytes worst case, hence the conservative floor).
#define TASKMON_CRITICAL_WORDS 128
#define TASKMON_WATCH_WORDS 512

// Start the sampler task (idempotent). Safe before the scheduler serves
// snapshots; first sample baselines every task it sees.
void task_monitor_start(void);

// Snapshot the table as JSON into buf (NUL-terminated, truncated safely).
// Returns bytes written excluding NUL (json_writer sizing semantics).
size_t task_monitor_json(char *buf, size_t len);
