// Copyright 2011 Ben Hutchings.
// See the file "COPYING" for licence details.

// PCM audio definitions

#ifndef DVSWITCH_PCM_H
#define DVSWITCH_PCM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// DV audio has at most 16 bits per sample
typedef int16_t pcm_sample;

// DV supports 4-channel audio, but at the cost of resolution
// (12-bit samples).  We don't bother to support that.
#define PCM_CHANNELS	2

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_PCM_H)
