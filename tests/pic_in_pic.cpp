#include <cassert>
#include <cstring>
#include <iostream>

#include "avcodec_wrap.h"
#include "video_effect.h"

const uint32_t source_colour = 0xfefefe, dest_colour = 0x000000, pad_colour = 0xbadbad;

#ifdef TEST_SPEED
const int pad = 0;
const int dims[] = {
    480, 576, 702, 712, 720
};
#else
const int pad = 100;
const int dims[] = {
    1, 2, 3, 4, 15, 16, 17, 31, 32, 33, 64,
    128, 256, 480, 576, 702, 712, 719, 720
};
#endif
const int n_dims = sizeof(dims) / sizeof(dims[0]);

void alloc_plane(raw_frame_ref & frame, int i, int width, int height)
{
    size_t size = (width + 2 * pad) * (height + 2 * pad);
    uint8_t * buf = new uint8_t[size];
    frame.planes.linesize[i] = width + 2 * pad;
    frame.planes.data[i] = buf + frame.planes.linesize[i] * pad + pad;
}

raw_frame_ref alloc_frame(PixelFormat pix_fmt, int width, int height)
{
    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    raw_frame_ref frame;
    alloc_plane(frame, 0, width, height);
    alloc_plane(frame, 1,
		width >> chroma_shift_horiz, height >> chroma_shift_vert);
    alloc_plane(frame, 2,
		width >> chroma_shift_horiz, height >> chroma_shift_vert);
    frame.planes.data[3] = 0;
    frame.planes.linesize[3] = 0;
    frame.pix_fmt = pix_fmt;
    frame.height = height;
    return frame;
}

void free_frame(raw_frame_ref frame)
{
    for (int i = 0; i != 3; ++i)
	delete[] (frame.planes.data[i] - frame.planes.linesize[i] * pad - pad);
}

void fill_rect_colour(raw_frame_ref frame, rectangle rect, uint32_t colour)
{
    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(frame.pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    for (int i = 0; i != 3; ++i)
    {
	if (i == 1)
	{
	    rect.left >>= chroma_shift_horiz;
	    rect.right >>= chroma_shift_horiz;
	    rect.top >>= chroma_shift_vert;
	    rect.bottom >>= chroma_shift_vert;
	}

	const uint8_t value = colour >> (16 - 8 * i);
	for (int y = rect.top; y != rect.bottom; ++y)
	    std::memset(frame.planes.data[i] + frame.planes.linesize[i] * y + rect.left,
			value, rect.right - rect.left);
    }
}

void assert_rect_colour(raw_frame_ref frame, rectangle rect, uint32_t colour)
{
    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(frame.pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    bool matched = true;

    for (int i = 0; i != 3; ++i)
    {
	if (i == 1)
	{
	    rect.left >>= chroma_shift_horiz;
	    rect.right >>= chroma_shift_horiz;
	    rect.top >>= chroma_shift_vert;
	    rect.bottom >>= chroma_shift_vert;
	}

	const uint8_t value = colour >> (16 - 8 * i);
	for (int y = rect.top; y != rect.bottom; ++y)
	    for (int x = rect.left; x != rect.right; ++x)
	    {
		uint8_t found_value =
		    *(frame.planes.data[i] + y * frame.planes.linesize[i] + x);
		if (found_value != value)
		{
		    std::cerr << "mismatch at (" << i << "," << x << "," << y
			      << "): expected " << unsigned(value)
			      << " found " << unsigned(found_value) << "\n";
		    matched = false;
		}
	    }
    }

    assert(matched);
}

void assert_padding_unchanged(raw_frame_ref frame, int width, int height)
{
    rectangle top_border = { -pad, -pad, width + pad, 0 };
    rectangle left_border = { -pad, 0, 0, height };
    rectangle right_border = { width, 0, width + pad, height };
    rectangle bottom_border = { -pad, height, width + pad, height + pad };
    assert_rect_colour(frame, top_border, pad_colour);
    assert_rect_colour(frame, left_border, pad_colour);
    assert_rect_colour(frame, right_border, pad_colour);
    assert_rect_colour(frame, bottom_border, pad_colour);
}

void test_pic_in_pic(raw_frame_ref dest, int d_width, int d_height,
		     int d_r_width, int d_r_height,
		     raw_frame_ref source, int s_width, int s_height)
{
    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(dest.pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    rectangle s_rect = { 0, 0, s_width, s_height };

    for (int place_horiz = 0; place_horiz != 3; ++place_horiz)
    {
	rectangle d_rect;
	d_rect.left = (((d_width - d_r_width) * place_horiz / 2)
		       & -(1U << chroma_shift_horiz));
	if (place_horiz != 0 && d_rect.left == 0)
	    continue;
	d_rect.right = d_rect.left + d_r_width;

	for (int place_vert = 0; place_vert != 3; ++place_vert)
	{
	    d_rect.top = (((d_height - d_r_height) * place_vert / 2)
			  & -(1U << chroma_shift_vert));
	    if (place_vert != 0 && d_rect.top == 0)
		continue;
	    d_rect.bottom = d_rect.top + d_r_height;

	    video_effect_pic_in_pic(dest, d_rect, source, s_rect);

#ifndef TEST_SPEED
	    // Check we overwrote the area we were supposed to
	    assert_rect_colour(dest, d_rect, source_colour);

	    // Check we missed the rest of the destination frame
	    rectangle top_border = { 0, 0, d_width, d_rect.top };
	    rectangle left_border = { 0, d_rect.top
				      , d_rect.left, d_rect.bottom };
	    rectangle right_border = { d_rect.right, d_rect.top,
				       d_width, d_rect.bottom };
	    rectangle bottom_border = { 0, d_rect.bottom, d_width, d_height };
	    assert_rect_colour(dest, top_border, dest_colour);
	    assert_rect_colour(dest, left_border, dest_colour);
	    assert_rect_colour(dest, right_border, dest_colour);
	    assert_rect_colour(dest, bottom_border, dest_colour);

	    // Check we missed the padding
	    assert_padding_unchanged(dest, d_width, d_height);

	    // Restore destination area
	    fill_rect_colour(dest, d_rect, dest_colour);
#endif
	}
    }
}

void test_dest(raw_frame_ref dest, int d_width, int d_height,
	       int s_width, int s_height)
{
    raw_frame_ref source = alloc_frame(dest.pix_fmt, s_width, s_height);
    rectangle s_buf_rect = { -pad, -pad, s_width + pad, s_height + pad };
    fill_rect_colour(source, s_buf_rect, pad_colour);
    rectangle s_frame_rect = { 0, 0, s_width, s_height };
    fill_rect_colour(source, s_frame_rect, source_colour);

    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(dest.pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    for (int i = 0; i != n_dims; ++i)
    {
	int d_r_width = dims[i];
	if (d_r_width > d_width || d_r_width > s_width)
	    break;
	if (d_r_width & ((1 << chroma_shift_horiz) - 1))
	    continue;
	for (int j = 0; j != n_dims; ++j)
	{
	    int d_r_height = dims[j];
	    if (d_r_height > d_height || d_r_height > s_height)
		break;
	    if (d_r_height & ((1 << chroma_shift_vert) - 1))
		continue;
	    test_pic_in_pic(dest, d_width, d_height,
			    d_r_width, d_r_height,
			    source, s_width, s_height);
	}
    }

    free_frame(source);
}

void test_format_size(PixelFormat pix_fmt, int d_width, int d_height)
{
    raw_frame_ref dest = alloc_frame(pix_fmt, d_width, d_height);
    rectangle d_buf_rect = { -pad, -pad, d_width + pad, d_height + pad };
    fill_rect_colour(dest, d_buf_rect, pad_colour);
    rectangle d_frame_rect = { 0, 0, d_width, d_height };
    fill_rect_colour(dest, d_frame_rect, dest_colour);

    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    for (int i = 0; i != n_dims; ++i)
    {
	int s_width = dims[i];
	if ((s_width & ((1 << chroma_shift_horiz) - 1)) == 0)
	    for (int j = 0; j != n_dims; ++j)
	    {
		int s_height = dims[j];
		if (s_height > FRAME_HEIGHT_MAX)
		    break;
		if (s_height & ((1 << chroma_shift_vert) - 1))
		    continue;
		test_dest(dest, d_width, d_height, s_width, s_height);
	    }
    }

    free_frame(dest);
}

void test_format(PixelFormat pix_fmt)
{
    int chroma_shift_horiz, chroma_shift_vert;
    avcodec_get_chroma_sub_sample(pix_fmt,
				  &chroma_shift_horiz, &chroma_shift_vert);

    for (int i = 0; i != n_dims; ++i)
    {
	int width = dims[i];
	if ((width & ((1 << chroma_shift_horiz) - 1)) == 0)
	    for (int j = 0; j != n_dims; ++j)
	    {
		int height = dims[j];
		if ((height & ((1 << chroma_shift_vert) - 1)) == 0)
		    test_format_size(pix_fmt, width, height);
	    }
    }
}

int main()
{
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 5, 0)
    avcodec_init();
#endif
    avcodec_register_all();
    test_format(PIX_FMT_YUV420P);
    test_format(PIX_FMT_YUV411P);
}
