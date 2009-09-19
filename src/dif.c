// Copyright 2008-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF definitions and metadata access

#include "dif.h"

static const uint8_t dv_audio_shuffle_625_50[12][9] = {
    {   0,  36,  72,  26,  62,  98,  16,  52,  88}, /* 1st channel */
    {   6,  42,  78,  32,  68, 104,  22,  58,  94},
    {  12,  48,  84,   2,  38,  74,  28,  64, 100},
    {  18,  54,  90,   8,  44,  80,  34,  70, 106},
    {  24,  60,  96,  14,  50,  86,   4,  40,  76},
    {  30,  66, 102,  20,  56,  92,  10,  46,  82},

    {   1,  37,  73,  27,  63,  99,  17,  53,  89}, /* 2nd channel */
    {   7,  43,  79,  33,  69, 105,  23,  59,  95},
    {  13,  49,  85,   3,  39,  75,  29,  65, 101},
    {  19,  55,  91,   9,  45,  81,  35,  71, 107},
    {  25,  61,  97,  15,  51,  87,   5,  41,  77},
    {  31,  67, 103,  21,  57,  93,  11,  47,  83},
};

const struct dv_system dv_system_625_50 =
{
    .common_name = "pal",
    .frame_width = 720,
    .frame_height = 576,
    .active_region = { .left = 9, .top = 0, .right = 711, .bottom = 576 },
    .frame_rate_numer = 25,
    .frame_rate_denom = 1,
    .pixel_aspect = {
	[dv_frame_aspect_normal] = { .width =  59, .height = 54 },
	[dv_frame_aspect_wide] =   { .width = 118, .height = 81 }
    },
    .seq_count = 12,
    .size = 12 * DIF_SEQUENCE_SIZE,
    .audio_frame_counts = {
	[dv_sample_rate_48k] =  { .min = 1896, .max = 1944, .std_cycle_len = 1, .std_cycle = { 1920 } },
	[dv_sample_rate_44k1] = { .min = 1742, .max = 1786, .std_cycle_len = 1, .std_cycle = { 1764 } },
	[dv_sample_rate_32k] =  { .min = 1264, .max = 1296, .std_cycle_len = 1, .std_cycle = { 1280 } }
    },
    .audio_shuffle = dv_audio_shuffle_625_50,
};

static const uint8_t dv_audio_shuffle_525_60[10][9] = {
    {  0, 30, 60, 20, 50, 80, 10, 40, 70 }, /* 1st channel */
    {  6, 36, 66, 26, 56, 86, 16, 46, 76 },
    { 12, 42, 72,  2, 32, 62, 22, 52, 82 },
    { 18, 48, 78,  8, 38, 68, 28, 58, 88 },
    { 24, 54, 84, 14, 44, 74,  4, 34, 64 },

    {  1, 31, 61, 21, 51, 81, 11, 41, 71 }, /* 2nd channel */
    {  7, 37, 67, 27, 57, 87, 17, 47, 77 },
    { 13, 43, 73,  3, 33, 63, 23, 53, 83 },
    { 19, 49, 79,  9, 39, 69, 29, 59, 89 },
    { 25, 55, 85, 15, 45, 75,  5, 35, 65 },
};

const struct dv_system dv_system_525_60 =
{
    .common_name = "ntsc",
    .frame_width = 720,
    .frame_height = 480,
    .active_region = { .left = 4, .top = 0, .right = 716, .bottom = 480 },
    .frame_rate_numer = 30000,
    .frame_rate_denom = 1001,
    .pixel_aspect = {
	[dv_frame_aspect_normal] = { .width = 10, .height = 11 },
	[dv_frame_aspect_wide] =   { .width = 40, .height = 33 }
    },
    .seq_count = 10,
    .size = 10 * DIF_SEQUENCE_SIZE,
    .audio_frame_counts = {
	[dv_sample_rate_48k] = {
	    .min = 1580, .max = 1620,
	    .std_cycle_len = 5, .std_cycle = { 1600, 1602, 1602, 1602, 1602 }
	},
	[dv_sample_rate_44k1] = {
	    .min = 1452, .max = 1489,
	    .std_cycle_len = 100,
	    .std_cycle = {
		1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472,
		1471, 1472, 1471, 1472, 1471, 1472, 1471, 1471, 1472, 1471,
		1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471,
		1472, 1471, 1472, 1471, 1471, 1472, 1471, 1472, 1471, 1472,
		1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472,
		1471, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471,
		1472, 1471, 1472, 1471, 1472, 1471, 1471, 1472, 1471, 1472,
		1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472,
		1471, 1472, 1471, 1471, 1472, 1471, 1472, 1471, 1472, 1471,
		1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471, 1472, 1471
	    }
	},
	[dv_sample_rate_32k] = {
	    .min = 1053, .max = 1080,
	    .std_cycle_len = 15,
	    .std_cycle = {
		1068, 1067, 1068, 1068, 1068, 1067, 1068, 1068, 1068, 1067, 1068, 1068, 1068, 1067, 1068
	    }
	},
    },
    .audio_shuffle = dv_audio_shuffle_525_60,
};

enum dv_frame_aspect dv_buffer_get_aspect(const uint8_t * buffer)
{
    const uint8_t * vsc_pack = buffer + 5 * DIF_BLOCK_SIZE + 53;

    // If no VSC pack present, assume normal (4:3) aspect
    if (vsc_pack[0] != 0x61)
	return dv_frame_aspect_normal;

    // Check the aspect code (depends partly on the DV variant)
    int aspect = vsc_pack[2] & 7;
    int apt = buffer[4] & 7;
    if (aspect == 2 || (apt == 0 && aspect == 7))
	return dv_frame_aspect_wide;
    else
	return dv_frame_aspect_normal;
}

void dv_buffer_set_aspect(uint8_t * buffer, enum dv_frame_aspect aspect)
{
    const struct dv_system * system = dv_buffer_system(buffer);
    unsigned seq;

    for (seq = 0; seq != system->seq_count; ++seq)
    {
	uint8_t * vsc_pack =
	    buffer + seq * DIF_SEQUENCE_SIZE +
	    ((seq & 1) ? 3 * DIF_BLOCK_SIZE + 8 : 5 * DIF_BLOCK_SIZE + 53);

	vsc_pack[2] = ((vsc_pack[2] & 0xf8) |
		       ((aspect == dv_frame_aspect_wide) ? 2 : 0));
    }
}

enum dv_sample_rate dv_buffer_get_sample_rate(const uint8_t * buffer)
{
    const uint8_t * as_pack = buffer + (6 + 3 * 16) * DIF_BLOCK_SIZE + 3;

    if (as_pack[0] == 0x50)
    {
	unsigned sample_rate = (as_pack[4] >> 3) & 7;
	if (sample_rate < dv_sample_rate_count)
	    return sample_rate;
    }

    // If no AS pack present or sample rate is unrecognised, assume 48 kHz.
    // XXX Does this make any sense?
    return dv_sample_rate_48k;
}   
