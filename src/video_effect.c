// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Video effects for raw video frames

#include <assert.h>
#include <string.h>

#include <libavutil/pixdesc.h>

#include "video_effect.h"

enum {
    luma_bias = 16,   // black level (lower values are reserved for sync)
    luma_max = 235,
    chroma_bias = 128 // neutral level (chroma components are signed)
};

void video_effect_show_title_safe(struct raw_frame_ref dest)
{
    int chroma_shift_horiz, chroma_shift_vert;
    av_pix_fmt_get_chroma_sub_sample(dest.pix_fmt,
                                     &chroma_shift_horiz, &chroma_shift_vert);

    unsigned width = FRAME_WIDTH;
    unsigned height = dest.height;
    unsigned border_horiz = (FRAME_WIDTH + 5) / 10;
    unsigned border_vert = (dest.height + 5) / 10;
    unsigned bias = luma_bias;

    // Darken the non-title-safe area
    for (int plane = 0; plane != 3; ++plane)
    {
	if (plane == 1)
	{
	    width >>= chroma_shift_horiz;
	    border_horiz >>= chroma_shift_horiz;
	    height >>= chroma_shift_vert;
	    border_vert >>= chroma_shift_vert;
	    bias = chroma_bias;
	}

	for (unsigned y = 0; y != height; ++y)
	{
	    uint8_t * p, * end;

	    // Do left border
	    p = dest.planes.data[plane] + dest.planes.linesize[plane] * y;
	    end = p + border_horiz;
	    while (p != end)
		*p = (*p + bias) / 2, ++p;

	    end = p + width - border_horiz;
	    if (y >= border_vert && y < height - border_vert)
		// Skip to right border
		p += width - 2 * border_horiz;
	    // else continue across top border or bottom border
	    while (p != end)
		*p = (*p + bias) / 2, ++p;
	}
    }
}

void video_effect_brighten(struct raw_frame_ref dest,
			   struct rectangle d_rect)
{
    int chroma_shift_horiz, chroma_shift_vert;
    av_pix_fmt_get_chroma_sub_sample(dest.pix_fmt,
                                     &chroma_shift_horiz, &chroma_shift_vert);

    unsigned bias = luma_max;

    for (int plane = 0; plane != 3; ++plane)
    {
	if (plane == 1)
	{
	    d_rect.left >>= chroma_shift_horiz;
	    d_rect.right >>= chroma_shift_horiz;
	    d_rect.top >>= chroma_shift_vert;
	    d_rect.bottom >>= chroma_shift_vert;
	    bias = chroma_bias;
	}

	for (unsigned y = d_rect.top; y != (unsigned)d_rect.bottom; ++y)
	{
	    uint8_t * p = (dest.planes.data[plane]
			   + dest.planes.linesize[plane] * y + d_rect.left);
	    uint8_t * end = p + (d_rect.right - d_rect.left);
	    while (p != end)
		*p = (*p + bias) / 2, ++p;
	}
    }
}

void video_effect_pic_in_pic(struct raw_frame_ref dest,
			     struct rectangle d_rect,
			     struct raw_frame_ref source,
			     struct rectangle s_rect)
{
    int chroma_shift_horiz, chroma_shift_vert;
    av_pix_fmt_get_chroma_sub_sample(dest.pix_fmt,
                                     &chroma_shift_horiz, &chroma_shift_vert);

    // Round coordinates so they include whole numbers of chroma pixels
    s_rect.left &= -(1U << chroma_shift_horiz);
    s_rect.right &= -(1U << chroma_shift_horiz);
    s_rect.top &= -(1U << chroma_shift_vert);
    s_rect.bottom &= -(1U << chroma_shift_vert);
    d_rect.left &= -(1U << chroma_shift_horiz);
    d_rect.right &= -(1U << chroma_shift_horiz);
    d_rect.top &= -(1U << chroma_shift_vert);
    d_rect.bottom &= -(1U << chroma_shift_vert);

    assert(s_rect.left >= 0 && s_rect.left < s_rect.right
	   && s_rect.right <= FRAME_WIDTH);
    assert(s_rect.top >= 0 && s_rect.top < s_rect.bottom
	   && (unsigned)s_rect.bottom <= source.height);
    assert(d_rect.left >= 0 && d_rect.left <= d_rect.right
	   && d_rect.right <= FRAME_WIDTH);
    assert(d_rect.top >= 0 && d_rect.top <= d_rect.bottom
	   && (unsigned)d_rect.bottom <= dest.height);

    if (d_rect.left == d_rect.right || d_rect.top == d_rect.bottom)
	return;

    unsigned s_left = s_rect.left;
    unsigned s_width = s_rect.right - s_rect.left;
    unsigned s_top = s_rect.top;
    unsigned s_height = s_rect.bottom - s_rect.top;
    unsigned d_left = d_rect.left;
    unsigned d_width = d_rect.right - d_rect.left;
    unsigned d_top = d_rect.top;
    unsigned d_height = d_rect.bottom - d_rect.top;
    assert(d_width <= s_width && d_height <= s_height);

    // Scaling tables

    struct weights {
	// Weight of source column/row on current dest column/row
	uint16_t cur;
	// Weight of source column/row on next dest column/row, plus 1
	// if this the last source column/row for this dest column/row.
	uint16_t spill;
    };
    struct weights col_weights[FRAME_WIDTH];
    struct weights row_weights[FRAME_HEIGHT_MAX];
    unsigned e, x, y;
    uint32_t weight_scale = (((1ULL << 32) + s_width * s_height / 2)
			     / (s_width * s_height));

    e = 0;
    for (x = 0; x != s_width; ++x)
    {
	e += d_width;
	if (e >= s_width)
	{
	    e -= s_width;
	    col_weights[x].cur = d_width - e;
	    col_weights[x].spill = 1 + e;
	}
	else
	{
	    col_weights[x].cur = d_width;
	    col_weights[x].spill = 0;
	}
    }

    e = 0;
    for (y = 0; y != s_height; ++y)
    {
	e += d_height;
	if (e >= s_height)
	{
	    e -= s_height;
	    row_weights[y].cur = d_height - e;
	    row_weights[y].spill = 1 + e;
	}
	else
	{
	    row_weights[y].cur = d_height;
	    row_weights[y].spill = 0;
	}
    }

    assert(col_weights[s_width - 1].spill == 1);
    assert(col_weights[(s_width >> chroma_shift_horiz) - 1].spill == 1);
    assert(row_weights[s_height - 1].spill == 1);
    assert(row_weights[(s_height >> chroma_shift_vert) - 1].spill == 1);

    for (unsigned plane = 0; plane != 3; ++plane)
    {
	if (plane == 1)
	{
	    d_left >>= chroma_shift_horiz;
	    d_width >>= chroma_shift_horiz;
	    s_left >>= chroma_shift_horiz;
	    s_width >>= chroma_shift_horiz;
	    d_top >>= chroma_shift_vert;
	    d_height >>= chroma_shift_vert;
	    s_top >>= chroma_shift_vert;
	    s_height >>= chroma_shift_vert;
	}
	uint8_t * dest_p = (dest.planes.data[plane]
			    + d_top * dest.planes.linesize[plane] + d_left);
	const unsigned dest_gap = dest.planes.linesize[plane] - d_width;
	uint32_t row_buffer[FRAME_WIDTH], * row_p;
	memset(row_buffer, 0, d_width * sizeof(uint32_t));

	// Loop over source rows
	for (y = 0; ; ++y)
	{
	    unsigned row_weight = row_weights[y].cur;
	    unsigned row_spill = row_weights[y].spill;

	    // Loop over source columns
	    const uint8_t * source_p =
		source.planes.data[plane]
		+ source.planes.linesize[plane] * (s_top + y) + s_left;
	    row_p = row_buffer;
	    uint32_t value_sum = *row_p;
	    for (x = 0; x != s_width; ++x)
	    {
		unsigned value_rw = *source_p++ * row_weight;
		value_sum += value_rw * col_weights[x].cur;
		if (col_weights[x].spill)
		{
		    *row_p++ = value_sum;
		    value_sum = *row_p + value_rw * (col_weights[x].spill - 1);
		}
	    }

	    if (!row_spill)
		continue;

	    // Spit out destination row
	    row_p = row_buffer;
	    for (x = 0; x != d_width; ++x)
		*dest_p++ = (*row_p++ * (uint64_t)weight_scale
			     + (1U << 31)) >> 32;

	    if (y == s_height - 1)
		break;

	    dest_p += dest_gap;

	    // Scale source row to next dest row if it overlaps
	    // otherwise just reinitialise row buffer
	    row_weight = row_spill - 1;
	    if (!row_weight)
	    {
		memset(row_buffer, 0, d_width * sizeof(uint32_t));
	    }
	    else
	    {
		source_p -= s_width;
		row_p = row_buffer;
		uint32_t value_sum = 0;
		for (x = 0; x != s_width; ++x)
		{
		    unsigned value_rw = *source_p++ * row_weight;
		    value_sum += value_rw * col_weights[x].cur;
		    if (col_weights[x].spill)
		    {
			*row_p++ = value_sum;
			value_sum = value_rw * (col_weights[x].spill - 1);
		    }
		}
	    }
	}
    }
}

void video_effect_fade(struct raw_frame_ref dest,
		       struct raw_frame_ref sec,
		       uint8_t scale)
{
    int x, y, plane;
    uint8_t *ptr_d, *ptr_s;
    uint16_t tmp;
    int width, height;

    int chroma_shift_horiz, chroma_shift_vert;
    av_pix_fmt_get_chroma_sub_sample(dest.pix_fmt,
                                     &chroma_shift_horiz, &chroma_shift_vert);

    width = FRAME_WIDTH;
    height = dest.height;
    for (plane = 0; plane < 3; plane++)
    {
	ptr_d = (dest.planes.data[plane]);
	ptr_s = (sec.planes.data[plane]);
	if (plane == 1)
	{
	    width >>= chroma_shift_horiz;
	    height >>= chroma_shift_vert;
	}
        for (y = 0; y < height; y++)
        {
	    for (x = 0; x < width; x++)
	    {
		tmp = scale * (*ptr_s - *ptr_d);
		*ptr_d += (uint8_t)(tmp >> 8);
		ptr_d++; ptr_s++;
	    }
	    ptr_d += dest.planes.linesize[plane] - width;
	    ptr_s += dest.planes.linesize[plane] - width;
        }
    }
}
