// Copyright 2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF and raw video frame structures and utilities

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "avcodec_wrap.h"

#include "frame.h"

int raw_frame_get_buffer(AVCodecContext * context, AVFrame * header)
{
    struct raw_frame * frame = context->opaque;

    if (context->pix_fmt == PIX_FMT_YUV420P)
    {
	header->data[0] = frame->buffer._420.y;
	header->linesize[0] = FRAME_LINESIZE_4;
	header->data[1] = frame->buffer._420.cb;
	header->linesize[1] = FRAME_LINESIZE_2;
	header->data[2] = frame->buffer._420.cr;
	header->linesize[2] = FRAME_LINESIZE_2;
    }
    else if (context->pix_fmt == PIX_FMT_YUV411P)
    {
	header->data[0] = frame->buffer._411.y;
	header->linesize[0] = FRAME_LINESIZE_4;
	header->data[1] = frame->buffer._411.cb;
	header->linesize[1] = FRAME_LINESIZE_1;
	header->data[2] = frame->buffer._411.cr;
	header->linesize[2] = FRAME_LINESIZE_1;
    }
    else
    {
	assert(!"unexpected pixel format");
    }

    frame->pix_fmt = context->pix_fmt;
    header->type = FF_BUFFER_TYPE_USER;

    return 0;
}

void raw_frame_release_buffer(AVCodecContext * context __attribute__((unused)),
			      AVFrame * header)
{
    for (int i = 0; i != 4; ++i)
	header->data[i] = 0;
}

int raw_frame_reget_buffer(AVCodecContext * context __attribute__((unused)),
			   AVFrame * header __attribute__((unused)))
{
    return 0;
}

void copy_raw_frame(struct raw_frame_ref dest,
		    struct raw_frame_ref source)
{
    assert(dest.height == source.height);
    assert(dest.pix_fmt == source.pix_fmt);

    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(dest.pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    unsigned width = FRAME_WIDTH;
    unsigned height = source.height;

    for (int plane = 0; plane != 4; ++plane)
    {
	const uint8_t * source_p = source.planes.data[plane];
	if (!source_p)
	    continue;
	const unsigned source_size = source.planes.linesize[plane];
	uint8_t * dest_p = dest.planes.data[plane];
	const unsigned dest_size = dest.planes.linesize[plane];

	if (plane == 1)
	{
	    width >>= chroma_shift_horiz;
	    height >>= chroma_shift_vert;
	}

	for (unsigned y = 0; y != height; ++y)
	{
	    memcpy(dest_p, source_p, width);
	    source_p += source_size;
	    dest_p += dest_size;
	}
    }
}

