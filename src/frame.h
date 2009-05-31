// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF and raw video frame structures and utilities

#ifndef DVSWITCH_FRAME_H
#define DVSWITCH_FRAME_H

#ifndef __cplusplus
#include <stdbool.h>
#endif
#include <stddef.h>

#include <sys/types.h>

#include "avcodec_wrap.h"

#include "dif.h"

#ifdef __cplusplus
extern "C" {
#endif

struct dv_frame
{
    uint64_t timestamp;           // set by mixer
    unsigned serial_num;          // set by mixer
    bool do_record;               // set by mixer
    bool cut_before;              // set by mixer
    bool format_error;            // set by mixer
    uint8_t buffer[DIF_MAX_FRAME_SIZE];
};

static inline
const struct dv_system * dv_frame_system(const struct dv_frame * frame)
{
    return dv_buffer_system(frame->buffer);
}

static inline enum dv_frame_aspect
dv_frame_get_aspect(const struct dv_frame * frame)
{
    return dv_buffer_get_aspect(frame->buffer);
}

static inline enum dv_sample_rate
dv_frame_get_sample_rate(const struct dv_frame * frame)
{
    return dv_buffer_get_sample_rate(frame->buffer);
}

#define FRAME_WIDTH           720
#define FRAME_HEIGHT_MAX      576

#define FRAME_LINESIZE_4	((FRAME_WIDTH + 15) & ~15)
#define FRAME_LINESIZE_2	((FRAME_WIDTH / 2 + 15) & ~15)
#define FRAME_LINESIZE_1	((FRAME_WIDTH / 4 + 15) & ~15)

struct raw_frame
{
    AVFrame header;
    enum PixelFormat pix_fmt;
    enum dv_frame_aspect aspect;
    union
    {
	struct
	{
	    uint8_t y[FRAME_LINESIZE_4 * FRAME_HEIGHT_MAX];
	    uint8_t cb[FRAME_LINESIZE_2 * FRAME_HEIGHT_MAX / 2];
	    uint8_t cr[FRAME_LINESIZE_2 * FRAME_HEIGHT_MAX / 2];
	} _420;
	struct
	{
	    uint8_t y[FRAME_LINESIZE_4 * FRAME_HEIGHT_MAX];
	    uint8_t cb[FRAME_LINESIZE_1 * FRAME_HEIGHT_MAX];
	    uint8_t cr[FRAME_LINESIZE_1 * FRAME_HEIGHT_MAX];
	} _411;
    } buffer __attribute__((aligned(16)));
};

// Buffer management functions for use with raw_frame.
// These require that context->opaque is a pointer to the
// struct raw_frame to be used.
extern int raw_frame_get_buffer(AVCodecContext * context, AVFrame * av_frame);
extern void raw_frame_release_buffer(AVCodecContext * context, AVFrame * frame);
extern int raw_frame_reget_buffer(AVCodecContext * context, AVFrame * av_frame);

static inline
const struct dv_system * raw_frame_system(const struct raw_frame * frame)
{
    return (const struct dv_system *)frame->header.opaque;
}

struct raw_frame_ref
{
    AVPicture planes;
    enum PixelFormat pix_fmt;
    unsigned height;
};

void copy_raw_frame(struct raw_frame_ref dest, struct raw_frame_ref source);

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_FRAME_H)
