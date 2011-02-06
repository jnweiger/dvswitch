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

// Min and max supported sample frequencies
#define PCM_FREQ_MIN	32000
#define PCM_FREQ_MAX	48000

// Min and max packet sizes (in frames).  Maximum is set somewhat
// greater than the max number allowed in a single DV frame.  We need
// some minimum in order to size queues, so we somewhat arbitrarily
// set a minimum of 1/50 second at minimum sample rate.
#define PCM_PACKET_SIZE_MIN	640
#define PCM_PACKET_SIZE_MAX	2000

// A packet of PCM frames received together (via ALSA, RTP, whatever)
struct pcm_packet
{
    uint64_t timestamp;	          // set by mixer
    bool do_record;               // set by mixer
    bool cut_before;              // set by mixer
    bool format_error;            // set by mixer
    unsigned sample_rate;         // actual rate in Hz
    unsigned frame_count;         // number of frames in this packet
    pcm_sample samples[PCM_CHANNELS * PCM_PACKET_SIZE_MAX];
                                  // PCM samples, interleaved
};

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_PCM_H)
