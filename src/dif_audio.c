// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF audio (de)multiplexing and decoding/encoding of 12-bit audio

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <string.h>

#include "dif.h"

// Samples may be encoded as either 16-bit LPCM or 12-bit companded PCM.
// The companding mapping is:
//
// 16-bit sample    12-bit code   Scale
// ------------------------------------
// 0x4000..0x7fff   0x700..0x7ff  6
// 0x2000..0x4000   0x600..0x700  5
// 0x1000..0x2000   0x500..0x600  4
// 0x0800..0x1000   0x400..0x500  3
// 0x0400..0x0800   0x300..0x400  2
// 0x0200..0x0400   0x200..0x300  1
// 0x0000..0x0200   0x000..0x200  0
// 0xfe00..0xffff   0xe00..0xfff  0
// 0xfc00..0xfe00   0xd00..0xe00  1
// 0xf800..0xfc00   0xc00..0xd00  2
// 0xf000..0xf800   0xb00..0xc00  3
// 0xe000..0xf000   0xa00..0xb00  4
// 0xc000..0xe000   0x900..0xa00  5
// 0x8001..0xc000   0x801..0x900  6
//
// (The ranges overlap because both scale values work at the boundaries.)

static unsigned get_12bit_scale(uint16_t sample)
{
    unsigned result = 0;

    if (sample & 0x7000)
    {
	sample >>= 4;
	result += 4;
    }
    if (sample & 0x0c00)
    {
	sample >>= 2;
	result += 2;
    }
    if (sample & 0x0200)
	result += 1;
    return result;
}

static int16_t decode_12bit(unsigned code)
{
    if (code < 0x200)
    {
	return code;
    }
    else if (code < 0x800)
    {
        unsigned scale = (code >> 8) - 1;
        return ((code & 0xff) + 0x100) << scale;
    }
    else if (code == 0x800)
    {
	return 0;
    }
    else if (code < 0xe00)
    {
	unsigned scale = 14 - (code >> 8);
	return ((int)(code & 0xff) - 0x200) << scale;
    }
    else
    {
	return (int)code - 0x1000;
    }
}

unsigned dv_buffer_get_audio(const uint8_t * buffer, int16_t * samples)
{
    const struct dv_system * system = dv_buffer_system(buffer);
    const uint8_t * as_pack = buffer + (6 + 3 * 16) * DIF_BLOCK_SIZE + 3;

    if (as_pack[0] != 0x50)
	return 0;

    enum dv_sample_rate sample_rate_code = (as_pack[4] >> 3) & 7;
    if (sample_rate_code >= dv_sample_rate_count)
	return 0;

    unsigned quant = as_pack[4] & 7;
    if (quant > 1)
	return 0;

    unsigned sample_count =
	2 * (system->audio_frame_counts[sample_rate_code].min +
	     (as_pack[1] & 0x3f));

    for (unsigned seq = 0;
	 seq != (quant ? system->seq_count / 2 : system->seq_count);
	 ++seq)
    {
	for (unsigned block_n = 0; block_n != 9; ++block_n)
	{
	    const uint8_t * block =
		&buffer[seq * DIF_SEQUENCE_SIZE +
			(6 + 16 * block_n) * DIF_BLOCK_SIZE];

	    if (quant) // 12-bit
	    {
		for (unsigned i = 0; i != 24; ++i)
		{
		    unsigned pos = (system->audio_shuffle[seq][block_n] +
				    i * system->seq_count * 9);
		    if (pos < sample_count)
		    {
			unsigned code = ((block[8 + 3 * i] << 4) +
					 (block[8 + 3 * i + 2] >> 4));
			samples[pos] = decode_12bit(code);
		    }

		    pos = (system->audio_shuffle[
			       seq + system->seq_count / 2][block_n] +
			   i * system->seq_count * 9);
		    if (pos < sample_count)
		    {
			unsigned code = ((block[8 + 3 * i + 1] << 4) +
					  (block[8 + 3 * i + 2] & 0xf));
			samples[pos] = decode_12bit(code);
		    }
		}
	    }
	    else // 16-bit
	    {
		for (unsigned i = 0; i != 36; ++i)
		{
		    unsigned pos = (system->audio_shuffle[seq][block_n] +
				    i * system->seq_count * 9);
		    if (pos < sample_count)
		    {
			int16_t sample = (block[8 + 2 * i + 1] +
					  (block[8 + 2 * i] << 8));
			if (sample == -0x8000)
			    sample = 0;
			samples[pos] = sample;
		    }
		}
	    }
	}
    }

    return sample_count / 2;
}

void dv_buffer_get_audio_levels(const uint8_t * buffer, int * levels)
{
    int16_t samples[2 * 2000];
    unsigned frame_count = dv_buffer_get_audio(buffer, samples);

    assert(2 * frame_count * sizeof(int16_t) <= sizeof(samples));

    // Total of squares of samples, so we can calculate average power.  We shift
    // right to avoid overflow.
    static const unsigned total_shift = 9;
    unsigned total_l = 0, total_r = 0;

    for (unsigned i = 0; i != frame_count; ++i)
    {
	int16_t sample = samples[2 * i];
	total_l += ((unsigned)(sample * sample)) >> total_shift;
	sample = samples[2 * i + 1];
	total_r += ((unsigned)(sample * sample)) >> total_shift;
    }

    // Calculate average power and convert to dB
    levels[0] = (total_l == 0 ? INT_MIN
		 : (int)(log10((double)total_l * (1 << total_shift) /
			       ((double)frame_count * (0x7fff * 0x7fff)))
			 * 10.0));
    levels[1] = (total_r == 0 ? INT_MIN
		 : (int)(log10((double)total_r * (1 << total_shift) /
			       ((double)frame_count * (0x7fff * 0x7fff)))
			 * 10.0));
}

static unsigned encode_12bit(int16_t sample)
{
    if (sample >= -0x200 && sample <= 0x200)
    {
	return (unsigned)sample & 0xfff;
    }
    else if (sample > 0)
    {
	unsigned scale = get_12bit_scale(sample);
	return ((scale + 1) << 8) | ((sample >> scale) & 0xff);
    }
    else
    {
	unsigned scale = get_12bit_scale(~sample);
	return ((14 - scale) << 8) | ((((sample - 1) >> scale) + 1) & 0xff);
    }
}

void dv_buffer_dub_audio(uint8_t * dest, const uint8_t * source)
{
    const struct dv_system * system = dv_buffer_system(dest);
    assert(dv_buffer_system(source) == system);

    for (unsigned seq_num = 0; seq_num != system->seq_count; ++seq_num)
    {
	for (unsigned block_num = 0; block_num != 9; ++block_num)
	{
	    ptrdiff_t block_pos = (DIF_SEQUENCE_SIZE * seq_num
				   + DIF_BLOCK_SIZE * (6 + block_num * 16));
	    memcpy(dest + block_pos, source + block_pos, DIF_BLOCK_SIZE);
	}
    }
}

void dv_buffer_set_audio(uint8_t * buffer,
			 enum dv_sample_rate sample_rate_code,
			 unsigned frame_count, const int16_t * samples)
{
    const struct dv_system * system = dv_buffer_system(buffer);

    assert(sample_rate_code >= 0 && sample_rate_code < dv_sample_rate_count);
    assert(frame_count >= system->audio_frame_counts[sample_rate_code].min &&
	   frame_count <= system->audio_frame_counts[sample_rate_code].max);

    bool use_12bit = sample_rate_code == dv_sample_rate_32k;
    unsigned sample_count = frame_count * 2; // stereo

    // Each audio block has a 3-byte block id, a 5-byte AAUX
    // pack, and 72 bytes of samples.  Audio block 3 in each
    // sequence has an AS (audio source) pack, audio block 4
    // has an ASC (audio source control) pack, and the other
    // packs seem to be optional.
    static const uint8_t aaux_blank_pack[DIF_PACK_SIZE] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF
    };
    uint8_t aaux_as_pack[DIF_PACK_SIZE] = {
	// pack id; 0x50 for AAUX source
	0x50,
	// bits 0-5: number of audio frames in video frame minus minimum value
	// bit 6: flag "should be 1"
	// bit 7: flag for unlocked audio sampling
	(frame_count - system->audio_frame_counts[sample_rate_code].min)
	| (1 << 6) | (1 << 7),
	// bits 0-3: audio mode
	// bit 4: flag for independent channels
	// bit 5: flag for "lumped" stereo (?)
	// bits 6-7: number of audio channels per block minus 1
	use_12bit << 6,
	// bits 0-4: system type; 0x0 for DV
	// bit 5: frame rate; 0 for 29.97 fps, 1 for 25 fps
	// bit 6: flag for multi-language audio
	// bit 7: ?
	dv_buffer_system_code(buffer) << 5,
	// bits 0-2: quantisation; 0 for 16-bit LPCM, 1 for 12-bit
	// bits 3-5: sample rate code
	// bit 6: time constant of emphasis; must be 1
	// bit 7: flag for no emphasis
	use_12bit | (sample_rate_code << 3) | (1 << 6) | (1 << 7)
    };
    static const uint8_t aaux_asc_pack[DIF_PACK_SIZE] = {
	// pack id; 0x51 for AAUX source control
	0x51,
	// bits 0-1: emphasis flag and ?
	// bits 2-3: compression level; 0 for once
	// bits 4-5: input type; 1 for digital
	// bits 6-7: copy generation management system; 0 for unrestricted
	(1 << 4),
	// bits 0-2: ?
	// bits 3-5: recording mode; 1 for original (XXX should indicate dub)
	// bit 6: recording end flag, inverted
	// bit 7: recording start flag, inverted
	(1 << 3) | (1 << 6) | (1 << 7),
	// bits 0-6: speed; 0x20 seems to be normal
	// bit 7: direction: 1 for forward
	0x20 | (1 << 7),
	// bits 0-6: genre; 0x7F seems to be unknown
	// bit 7: reserved
	0x7F
    };

    for (unsigned seq = 0; seq != system->seq_count; ++seq)
    {
	if (use_12bit && seq == system->seq_count / 2)
	    samples = NULL; // silence extra 2 channels

	for (unsigned block_n = 0; block_n != 9; ++block_n)
	{
	    uint8_t * out = (buffer + seq * DIF_SEQUENCE_SIZE +
			     (6 + 16 * block_n) * DIF_BLOCK_SIZE +
			     DIF_BLOCK_ID_SIZE);
	    const uint8_t * pack;

	    if (block_n == ((seq & 1) ? 0 : 3))
		pack = aaux_as_pack;
	    else if (block_n == ((seq & 1) ? 1 : 4))
		pack = aaux_asc_pack;
	    else
		pack = aaux_blank_pack;
	    memcpy(out, pack, DIF_PACK_SIZE);
	    out += DIF_PACK_SIZE;

	    if (samples == NULL)
	    {
		memset(out, 0, DIF_BLOCK_SIZE - DIF_BLOCK_ID_SIZE - DIF_PACK_SIZE);
	    }
	    else if (use_12bit)
	    {
		for (unsigned i = 0; i != 24; ++i)
		{
		    unsigned pos = (system->audio_shuffle[seq][block_n] +
				    i * system->seq_count * 9);
		    unsigned code1 =
			(pos < sample_count) ? encode_12bit(samples[pos]) : 0;
		    pos = (system->audio_shuffle[
			       seq + system->seq_count / 2][block_n] +
			   i * system->seq_count * 9);
		    unsigned code2 =
			(pos < sample_count) ? encode_12bit(samples[pos]) : 0;

		    *out++ = code1 >> 4;
		    *out++ = code2 >> 4;
		    *out++ = (code1 << 4) | (code2 & 0xf);
		}
	    }
	    else // 16-bit
	    {
		for (unsigned i = 0; i != 36; ++i)
		{
		    unsigned pos = (system->audio_shuffle[seq][block_n] +
				    i * system->seq_count * 9);
		    int16_t sample = (pos < sample_count) ? samples[pos] : 0;
		    
		    *out++ = sample >> 8;
		    *out++ = sample & 0xff;
		}
	    }
	}
    }
}

void dv_buffer_silence_audio(uint8_t * buffer,
			     enum dv_sample_rate sample_rate_code,
			     unsigned serial_num)
{
    const struct dv_system * system = dv_buffer_system(buffer);
    unsigned frame_count =
	system->audio_frame_counts[sample_rate_code].std_cycle[
	    serial_num %
	    system->audio_frame_counts[sample_rate_code].std_cycle_len];

    dv_buffer_set_audio(buffer, sample_rate_code, frame_count, NULL);
}
