// Copyright 2007 Ben Hutchings.
// See the file "COPYING" for licence details.

// Simple interval timer functions

#ifndef DVSWITCH_FRAME_TIMER_H
#define DVSWITCH_FRAME_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialise timer in a disarmed state.  Must be called in the first
// thread before any more threads are created and before any of the
// following functions are used.
void frame_timer_init(void);

// Get a timestamp.  This is the time since an unspecified point in
// the past, in ns.
uint64_t frame_timer_get(void);

// Wait until frame_timer_get() would return at least the given
// timestamp.
void frame_timer_wait(uint64_t timestamp);

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_FRAME_TIMER_H)
