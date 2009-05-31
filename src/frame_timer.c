// Copyright 2007 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// See the file "COPYING" for licence details.

// Simple interval timer functions

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include <sys/time.h>
#include <sys/types.h>

#include "frame_timer.h"

static timer_t frame_timer_id;
struct timespec frame_timer_res;

void frame_timer_init(void)
{
    sigset_t sigset_alarm;
    sigemptyset(&sigset_alarm);
    sigaddset(&sigset_alarm, SIGALRM);
    if (pthread_sigmask(SIG_BLOCK, &sigset_alarm, NULL) != 0)
    {
	perror("FATAL: pthread_sigmask");
	exit(1);
    }

    // On Linux, CLOCK_MONOTONIC matches the kernel interval timer
    // (resolution is controlled by HZ) and there is no
    // CLOCK_MONOTONIC_HR.
    if (clock_getres(CLOCK_MONOTONIC, &frame_timer_res) == -1)
    {
	perror("FATAL: clock_get_res");
	exit(1);
    }
    // Require a 250 Hz or faster clock.  The maximum clock period is
    // set 1% longer than this because Linux rounds to the nearest
    // number of whole hardware timer periods and reports that.
    if (frame_timer_res.tv_sec != 0
	|| (unsigned)frame_timer_res.tv_nsec > 1010000000 / 250)
    {
	fputs("FATAL: CLOCK_MONOTONIC resolution is too low; it must be"
	      " at least 250 Hz\n"
	      "       (Linux: CONFIG_HZ=250)\n",
	      stderr);
	exit(1);
    }

    struct sigevent event = {
	.sigev_notify = SIGEV_SIGNAL,
	.sigev_signo =  SIGALRM
    };
    if (timer_create(CLOCK_MONOTONIC, &event, &frame_timer_id) != 0)
    {
	perror("FATAL: timer_create");
	exit(1);
    }
}

uint64_t frame_timer_get(void)
{
    struct timespec result;
    if (clock_gettime(CLOCK_MONOTONIC, &result) != 0)
    {
	perror("FATAL: clock_gettime");
	exit(1);
    }
    return (uint64_t)result.tv_sec * 1000000000 + result.tv_nsec;
}

void frame_timer_wait(uint64_t point)
{
    struct itimerspec interval = {
	.it_value = { .tv_sec = point / 1000000000,
		      .tv_nsec = point % 1000000000 },
	.it_interval = { .tv_sec = 0,
			 .tv_nsec = 0 }
    };
    if (timer_settime(frame_timer_id, TIMER_ABSTIME, &interval, 0) != 0)
    {
	perror("FATAL: timer_settime");
	exit(1);
    }  
    sigset_t sigset_alarm;
    sigemptyset(&sigset_alarm);
    sigaddset(&sigset_alarm, SIGALRM);
    int dummy;
    sigwait(&sigset_alarm, &dummy);
}
