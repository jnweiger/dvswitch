// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF definitions, metadata access and audio functions

#ifndef DVSWITCH_DIF_H
#define DVSWITCH_DIF_H

#include <stddef.h>
#include <stdint.h>

#include "geometry.h"
#include "pcm.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DIF_BLOCK_SIZE 80
#define DIF_BLOCKS_PER_SEQUENCE 150
#define DIF_SEQUENCE_SIZE (DIF_BLOCK_SIZE * DIF_BLOCKS_PER_SEQUENCE)
#define DIF_MAX_FRAME_SIZE (DIF_SEQUENCE_SIZE * 12)

#define DIF_BLOCK_ID_SIZE 3
#define DIF_PACK_SIZE 5

// Block id for first block of a sequence
#define DIF_SIGNATURE_SIZE DIF_BLOCK_ID_SIZE
#define DIF_SIGNATURE "\x1f\x07\x00"

enum dv_sample_rate
{
    dv_sample_rate_invalid = -2,
    dv_sample_rate_auto = -1,
    dv_sample_rate_48k,
    dv_sample_rate_32k,
    dv_sample_rate_count
};

extern enum dv_sample_rate dv_buffer_get_sample_rate(const uint8_t * frame);

enum dv_frame_aspect
{
    dv_frame_aspect_auto = -1,
    dv_frame_aspect_normal,	// 4:3
    dv_frame_aspect_wide,	// 16:9
    dv_frame_aspect_count
};

extern enum dv_frame_aspect dv_buffer_get_aspect(const uint8_t * frame);
extern void dv_buffer_set_aspect(uint8_t * buffer, enum dv_frame_aspect aspect);

struct dv_system
{
    const char * common_name;
    unsigned frame_width, frame_height;
    struct rectangle active_region;
    unsigned frame_rate_numer, frame_rate_denom;
    struct {
	unsigned width, height;
    } pixel_aspect[dv_frame_aspect_count];
    unsigned seq_count;
    size_t size;
    // The number of audio frames per video frame may vary, for two
    // reasons.  Firstly, consumer gear is not required to have
    // synchronised audio and video clocks.  Secondly, the frame rate
    // 30000/1001 does not divide evenly into any of the supported
    // audio sample rates.
    struct {
	// Minimum and maximum sample counts allowed.  The actual sample
	// count is encoded in the AS pack relative to the minimum.
	unsigned min, max;
	// The cycle of sample counts required for "locked audio", per
	// IEC 61834-2:1998 table 17.
	unsigned std_cycle_len, std_cycle[15];
    } audio_frame_counts[dv_sample_rate_count];
    const uint8_t (*audio_shuffle)[9];
};

extern const struct dv_system dv_system_625_50, dv_system_525_60;

static inline
unsigned dv_buffer_system_code(const uint8_t * buffer)
{
    return buffer[3] >> 7;
}

static inline
const struct dv_system * dv_buffer_system(const uint8_t * buffer)
{
    return dv_buffer_system_code(buffer) ? &dv_system_625_50 : &dv_system_525_60;
}

// Get audio data from buffer.  Copy the first 2 channels to the buffer
// as interleaved signed 16-bit PCM samples.  Return the number of
// samples from each channel.  Caller must ensure the buffer is large
// enough!
unsigned dv_buffer_get_audio(const uint8_t * buffer, pcm_sample * samples);

#ifdef USE_JACK
/* used w/ -DUSE_JACK by dvsource-jack.c */
#include <jack/jack.h>
#define dvs_pcm_sample_t jack_default_audio_sample_t
#else
#define dvs_pcm_sample_t pcm_sample
#endif

// Set sample rate and audio data in buffer.  Copy interleaved signed
// 16-bit PCM samples to the first 2 channels to the buffer
// (companding to 12-bit if necessary).  Caller must ensure the number
// of samples is valid for the selected video system and sample rate.
void dv_buffer_set_audio(uint8_t * buffer,
			 enum dv_sample_rate sample_rate_code,
			 unsigned frame_count, const dvs_pcm_sample_t * samples);

void dv_buffer_get_audio_levels(const uint8_t * frame, int * levels);
void dv_buffer_dub_audio(uint8_t * dest, const uint8_t * source);
void dv_buffer_silence_audio(uint8_t * buffer,
			     enum dv_sample_rate sample_rate_code,
			     unsigned serial_num);
void dv_buffer_fill_dummy(uint8_t * buf, const struct dv_system * system);

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_DIF_H)
