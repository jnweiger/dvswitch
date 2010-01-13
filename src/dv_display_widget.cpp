// Copyright 2007-2009 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// See the file "COPYING" for licence details.

// Gtkmm widgets for displaying DV video

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <ostream>

#include <sys/ipc.h>
#include <sys/shm.h>

#include <gdkmm/cursor.h>

#include "dv_display_widget.hpp"
#include "frame.h"
#include "gui.hpp"
#include "video_effect.h"

// X headers come last due to egregious macro pollution.
#include <gdk/gdkx.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xvlib.h>

namespace
{
    template<typename T>
    T div_round_nearest(T numer, T denom)
    {
	return (numer + denom / 2) / denom;
    }

    const unsigned thumb_scale_denom = 4;

    const uint32_t invalid_xv_port = uint32_t(-1);

    Display * get_x_display(const Glib::RefPtr<Gdk::Drawable> & drawable)
    {
	return gdk_x11_drawable_get_xdisplay(drawable->gobj());
    }

    Display * get_x_display(Gtk::Widget & widget)
    {
	Glib::RefPtr<Gdk::Drawable> window(widget.get_window());
	assert(window);
	return get_x_display(window);
    }

    Window get_x_window(const Glib::RefPtr<Gdk::Drawable> & drawable)
    {
	return gdk_x11_drawable_get_xid(drawable->gobj());
    }

    Window get_x_window(Gtk::Widget & widget)
    {
	Glib::RefPtr<Gdk::Drawable> window(widget.get_window());
	assert(window);
	return get_x_window(window);
    }

    char * allocate_x_shm(Display * x_display, XShmSegmentInfo * info,
			  std::size_t size)
    {
	char * result = 0;
	if ((info->shmid = shmget(IPC_PRIVATE, size, IPC_CREAT | 0777)) != -1)
	{
	    info->shmaddr = static_cast<char *>(shmat(info->shmid, 0, 0));
	    if (info->shmaddr != reinterpret_cast<char *>(-1)
		&& XShmAttach(x_display, info))
		result = info->shmaddr;
	    shmctl(info->shmid, IPC_RMID, 0);
	}
	return result;
    }

    void free_x_shm(XShmSegmentInfo * info)
    {
	shmdt(info->shmaddr);
    }
}

// dv_display_widget

dv_display_widget::dv_display_widget(int lowres)
    : decoder_(avcodec_alloc_context()),
      decoded_serial_num_(-1),
      shm_busy_(false)
{
    AVCodecContext * decoder = decoder_.get();
    if (!decoder)
	throw std::bad_alloc();
    decoder->lowres = lowres;
    auto_codec_open_decoder(decoder_, CODEC_ID_DVVIDEO);
    decoder->opaque = this;
    decoder->get_buffer = get_buffer;
    decoder->release_buffer = release_buffer;
    decoder->reget_buffer = reget_buffer;

    set_app_paintable(true);
    set_double_buffered(false);
}

dv_display_widget::~dv_display_widget()
{
}

bool dv_display_widget::init_x_shm_events()
{
    Glib::RefPtr<Gdk::Display> display(get_window()->get_display());
    int major_opcode, first_error;

    if (!XQueryExtension(gdk_x11_display_get_xdisplay(display->gobj()),
			 "MIT-SHM",
			 &major_opcode, &x_shm_first_event_, &first_error))
	return false;

    gdk_x11_register_standard_event_type(display->gobj(),
					 x_shm_first_event_,
					 ShmNumberEvents);
    get_window()->add_filter(filter_x_shm_event, this);
    return true;
}

void dv_display_widget::fini_x_shm_events()
{
    get_window()->remove_filter(filter_x_shm_event, this);
}

GdkFilterReturn dv_display_widget::filter_x_shm_event(void * void_event,
						      GdkEvent *, void * data)
{
    dv_display_widget * widget = static_cast<dv_display_widget *>(data);
    XEvent * x_event = static_cast<XEvent *>(void_event);

    if (x_event->type == widget->x_shm_first_event_ + ShmCompletion)
    {
	widget->shm_busy_ = false;
	return GDK_FILTER_REMOVE;
    }

    return GDK_FILTER_CONTINUE;
}

dv_display_widget::display_region
dv_display_widget::get_display_region(const dv_system * system,
				      dv_frame_aspect frame_aspect)
{
    display_region result;
    static_cast<rectangle &>(result) = system->active_region;
    result.pixel_width = system->pixel_aspect[frame_aspect].width;
    result.pixel_height = system->pixel_aspect[frame_aspect].height;
    return result;
}

void dv_display_widget::put_frame(const dv_frame_ptr & dv_frame)
{
    if (!is_realized())
	return;

    if (dv_frame->serial_num != decoded_serial_num_ && !shm_busy_)
    {
	const struct dv_system * system = dv_frame_system(dv_frame.get());
	AVCodecContext * decoder = decoder_.get();

	AVFrame * header = get_frame_header();
	if (!header)
	    return;

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = dv_frame->buffer;
	packet.size = system->size;

	int got_frame;
	int used_size = avcodec_decode_video2(decoder,
					      header, &got_frame,
					      &packet);
	if (used_size <= 0)
	    return;
	assert(got_frame && size_t(used_size) == system->size);
	header->opaque = const_cast<void *>(static_cast<const void *>(system));
	decoded_serial_num_ = dv_frame->serial_num;

	put_frame_buffer(
	    get_display_region(system, dv_frame_get_aspect(dv_frame.get())));
	set_error(dv_frame->format_error);
	queue_draw();
    }
}

void dv_display_widget::put_frame(const raw_frame_ptr & raw_frame)
{
    if (!is_realized())
	return;

    if (raw_frame->header.pts != decoded_serial_num_ && !shm_busy_)
    {
	const struct dv_system * system = raw_frame_system(raw_frame.get());

	AVFrame * header = get_frame_buffer(get_frame_header(),
					    raw_frame->pix_fmt,
					    system->frame_height);
	if (!header)
	    return;

	raw_frame_ref dest, source;
	for (int plane = 0; plane != 4; ++plane)
	{
	    dest.planes.data[plane] = header->data[plane];
	    dest.planes.linesize[plane] = header->linesize[plane];
	    source.planes.data[plane] = raw_frame->header.data[plane];
	    source.planes.linesize[plane] = raw_frame->header.linesize[plane];
	}
	dest.pix_fmt = source.pix_fmt = raw_frame->pix_fmt;
	dest.height = source.height = system->frame_height;
	copy_raw_frame(dest, source);
	decoded_serial_num_ = raw_frame->header.pts;

	put_frame_buffer(get_display_region(system, raw_frame->aspect));
	queue_draw();
    }
}

void dv_display_widget::set_error(bool)
{
}

int dv_display_widget::get_buffer(AVCodecContext * context, AVFrame * header)
{
    dv_display_widget * widget =
	static_cast<dv_display_widget *>(context->opaque);

    return widget->get_frame_buffer(header, context->pix_fmt, context->height)
	? 0 : -1;
}

void dv_display_widget::release_buffer(AVCodecContext *, AVFrame * header)
{
    for (int i = 0; i != 4; ++i)
	header->data[i] = 0;
}

int dv_display_widget::reget_buffer(AVCodecContext *, AVFrame *)
{
    return 0;
}

// dv_full_display_widget

dv_full_display_widget::dv_full_display_widget()
    : pix_fmt_(PIX_FMT_NONE),
      height_(0),
      xv_port_(invalid_xv_port),
      xv_image_(0),
      xv_shm_info_(0),
      // We don't know what the frame format will be, but assume "PAL"
      // 4:3 frames and therefore an active image size of 702x576 and
      // pixel aspect ratio of 59:54.
      dest_width_(767), dest_height_(576),
      sel_enabled_(false),
      sel_in_progress_(false)
{
    std::memset(&source_region_, 0, sizeof(source_region_));
    std::memset(&selection_, 0, sizeof(selection_));

    set_size_request(dest_width_, dest_height_);

    add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK
	       | Gdk::BUTTON1_MOTION_MASK | Gdk::BUTTON2_MOTION_MASK);
}

void dv_full_display_widget::set_selection_enabled(bool flag)
{
    Glib::RefPtr<Gdk::Window> window(get_window());

    sel_enabled_ = flag;

    if (sel_enabled_)
    {
	window->set_cursor(Gdk::Cursor(Gdk::CROSSHAIR));
    }
    else
    {
	window->set_cursor(); // inherit from parent

	if (sel_in_progress_)
	{
	    sel_in_progress_ = false;
	    remove_modal_grab();
	}
    }

    queue_draw();
}

rectangle dv_full_display_widget::get_selection()
{
    return selection_;
}

bool dv_full_display_widget::try_init_xvideo(PixelFormat pix_fmt,
					     unsigned height) throw()
{
    if (pix_fmt == pix_fmt_ && height == height_)
	return xv_image_;

    fini_xvideo();

    if (!init_x_shm_events())
	return false;

    int xv_pix_fmt;
    switch (pix_fmt)
    {
    case PIX_FMT_YUV420P:
	// Use I420, which is an exact match and widely supported.
	xv_pix_fmt = 0x30323449;
	break;
    case PIX_FMT_YUV411P:
	// There is no common match for this, so use YUY2 and convert.
	xv_pix_fmt = 0x32595559;
	break;
    default:
	assert(!"Unexpected pixel format");
    }

    Display * x_display = get_x_display(*this);

    unsigned adaptor_count;
    XvAdaptorInfo * adaptor_info;

    if (XvQueryAdaptors(x_display, get_x_window(*this),
			&adaptor_count, &adaptor_info) != Success)
    {
	std::cerr << "ERROR: XvQueryAdaptors() failed\n";
	return false;
    }

    // Search for a suitable adaptor.
    unsigned i;
    for (i = 0; i != adaptor_count; ++i)
    {
	if (!(adaptor_info[i].type & XvImageMask))
	    continue;
	int format_count;
	XvImageFormatValues * format_info =
	    XvListImageFormats(x_display, adaptor_info[i].base_id,
			       &format_count);
	if (!format_info)
	    continue;
	for (int j = 0; j != format_count; ++j)
	    if (format_info[j].id == xv_pix_fmt)
		goto end_adaptor_loop;
    }
end_adaptor_loop:
    if (i == adaptor_count)
    {
	std::cerr << "ERROR: No Xv adaptor for this display supports "
		  << char(xv_pix_fmt & 0xFF)
		  << char((xv_pix_fmt >> 8) & 0xFF)
		  << char((xv_pix_fmt >> 16) & 0xFF)
		  << char(xv_pix_fmt >> 24)
		  << " format\n";
    }
    else
    {
	// Try to allocate a port.
	unsigned j;
	for (j = 0; j != adaptor_info[i].num_ports; ++j)
	{
	    XvPortID port = adaptor_info[i].base_id + j;
	    if (XvGrabPort(x_display, port, CurrentTime) == Success)
	    {
		xv_port_ = port;
		break;
	    }
	}
	if (j == adaptor_info[i].num_ports)
	    std::cerr << "ERROR: Could not grab an Xv port\n";
    }

    XvFreeAdaptorInfo(adaptor_info);

    if (xv_port_ == invalid_xv_port)
	return false;

    if (XShmSegmentInfo * xv_shm_info = new (std::nothrow) XShmSegmentInfo)
    {
	// Allocate frame buffer in shared memory.  Note we allocate an
	// extra row to allow space for in-place conversion.
	if (XvImage * xv_image =
	    XvShmCreateImage(x_display, xv_port_, xv_pix_fmt, 0,
			     FRAME_WIDTH, height + 1, xv_shm_info))
	{
	    if ((xv_image->data = allocate_x_shm(x_display, xv_shm_info,
						 xv_image->data_size)))
	    {
		pix_fmt_ = pix_fmt;
		height_ = height;
		xv_image_ = xv_image;
		xv_shm_info_ = xv_shm_info;
	    }
	    else
	    {
		free(xv_image);
		delete xv_shm_info;
	    }
	}
	else
	{
	    delete xv_shm_info;
	}
    }

    if (!xv_image_)
	std::cerr << "ERROR: Could not create Xv image\n";

    return xv_image_;
}

void dv_full_display_widget::fini_xvideo() throw()
{
    if (xv_port_ != invalid_xv_port)
    {
	Display * x_display = get_x_display(*this);

	XvStopVideo(x_display, xv_port_, get_x_window(*this));

	if (XvImage * xv_image = static_cast<XvImage *>(xv_image_))
	{
	    xv_image_ = 0;
	    free(xv_image);
	    XShmSegmentInfo * xv_shm_info =
		static_cast<XShmSegmentInfo *>(xv_shm_info_);
	    xv_shm_info_ = 0;
	    free_x_shm(xv_shm_info);
	    delete xv_shm_info;
	}

	XvUngrabPort(x_display, xv_port_, CurrentTime);
	xv_port_ = invalid_xv_port;

	fini_x_shm_events();
    }

    pix_fmt_ = PIX_FMT_NONE;
    height_ = 0;
}

AVFrame * dv_full_display_widget::get_frame_header()
{
    return &frame_header_;
}

AVFrame * dv_full_display_widget::get_frame_buffer(AVFrame * header,
						   PixelFormat pix_fmt,
						   unsigned height)
{
    if (!try_init_xvideo(pix_fmt, height))
	return 0;

    XvImage * xv_image = static_cast<XvImage *>(xv_image_);

    if (pix_fmt == PIX_FMT_YUV420P)
    {
	for (int plane = 0; plane != 3; ++plane)
	{
	    header->data[plane] =
		reinterpret_cast<uint8_t *>(xv_image->data
					    + xv_image->offsets[plane]);
	    header->linesize[plane] = xv_image->pitches[plane];
	}
    }
    else if (pix_fmt == PIX_FMT_YUV411P)
    {
	uint8_t * data = reinterpret_cast<uint8_t *>(xv_image->data
						     + xv_image->offsets[0]);
	unsigned linesize = xv_image->pitches[0];

	// Interleave the lines in the buffer so we can convert it to
	// 4:2:2 in-place.
	header->data[0] = data + linesize;
	header->linesize[0] = linesize;
	header->data[1] = data + linesize + linesize / 2;
	header->linesize[1] = linesize;
	header->data[2] = data + linesize + linesize * 3 / 4;
	header->linesize[2] = linesize;
    }
    else
    {
	assert(!"unknown pixel format");
    }

    header->data[3] = 0;
    header->linesize[3] = 0;

    header->type = FF_BUFFER_TYPE_USER;
    return &frame_header_;
}

void dv_full_display_widget::put_frame_buffer(
    const display_region & source_region)
{
    raw_frame_ref frame_ref;
    for (int plane = 0; plane != 4; ++plane)
    {
	frame_ref.planes.data[plane] = frame_header_.data[plane];
	frame_ref.planes.linesize[plane] = frame_header_.linesize[plane];
    }
    frame_ref.pix_fmt = pix_fmt_;
    frame_ref.height = height_;

    video_effect_show_title_safe(frame_ref);

    if (sel_enabled_)
    {
	selection_ &= source_region;
	video_effect_brighten(frame_ref, selection_);
    }

    if (pix_fmt_ == PIX_FMT_YUV411P)
    {
	// Lines are interleaved in the buffer; convert them in-place.

	XvImage * xv_image = static_cast<XvImage *>(xv_image_);
	uint8_t * data = reinterpret_cast<uint8_t *>(xv_image->data
						     + xv_image->offsets[0]);
	unsigned linesize = xv_image->pitches[0];

	for (unsigned y = 0; y != height_; ++y)
	{
	    uint8_t * out = data + y * linesize;
	    uint8_t * end = out + FRAME_WIDTH * 2;
	    const uint8_t * in_y = out + linesize;
	    const uint8_t * in_u = in_y + linesize / 2;
	    const uint8_t * in_v = in_u + linesize / 4;

	    do
	    {
		*out++ = *in_y++;
		*out++ = *in_u;
		*out++ = *in_y++;
		*out++ = *in_v;
		*out++ = *in_y++;
		*out++ = *in_u++;
		*out++ = *in_y++;
		*out++ = *in_v++;
	    }
	    while (out != end);
	}
    }

    if (source_region.pixel_width > source_region.pixel_height)
    {
	dest_height_ = source_region.bottom - source_region.top;
	dest_width_ = div_round_nearest((source_region.right
					 - source_region.left)
					* source_region.pixel_width,
					source_region.pixel_height);
    }
    else
    {
	dest_width_ = source_region.right - source_region.left;
	dest_height_ = div_round_nearest((source_region.bottom
					  - source_region.top)
					 * source_region.pixel_height,
					 source_region.pixel_width);
    }
    source_region_ = source_region;
    set_size_request(dest_width_, dest_height_);
}

void dv_full_display_widget::window_to_frame_coords(
    int & frame_x, int & frame_y,
    int window_x, int window_y) throw()
{
    frame_x = (source_region_.left +
	       div_round_nearest<int>(window_x
				      * (source_region_.right - source_region_.left),
				      dest_width_));
    frame_y = (source_region_.top +
	       div_round_nearest<int>(window_y
				      * (source_region_.bottom - source_region_.top),
				      dest_height_));
}

void dv_full_display_widget::update_selection(int x2, int y2)
{
    if (source_region_.empty())
	return;

    int frame_width = source_region_.right - source_region_.left;
    int frame_height = source_region_.bottom - source_region_.top;

    int dir_x, x1, scale_x_max;
    if (x2 < sel_start_x_)
    {
	dir_x = -1;
	x1 = sel_start_x_ + 1;
	scale_x_max = (x1 - source_region_.left) * frame_height;
    }
    else
    {
	dir_x = 1;
	x1 = sel_start_x_;
	x2 += 1;
	scale_x_max = (source_region_.right - x1) * frame_height;
    }
    int scale_x = (x2 - x1) * dir_x * frame_height;

    int dir_y, y1, scale_y_max;
    if (y2 < sel_start_y_)
    {
	dir_y = -1;
	y1 = sel_start_y_ + 1;
	scale_y_max = (y1 - source_region_.top) * frame_width;
    }
    else
    {
	dir_y = 1;
	y1 = sel_start_y_;
	y2 += 1;
	scale_y_max = (source_region_.bottom - y1) * frame_width;
    }
    int scale_y = (y2 - y1) * dir_y * frame_width;

    // Expand to maintain aspect ratio and shrink to fit the display region
    int scale = std::min(std::max(scale_x, scale_y),
			 std::min(scale_x_max, scale_y_max));
    x2 = x1 + dir_x * scale / frame_height;
    y2 = y1 + dir_y * scale / frame_width;

    selection_.left = dir_x < 0 ? x2 : x1;
    selection_.right = dir_x < 0 ? x1 : x2;
    selection_.top = dir_y < 0 ? y2 : y1;
    selection_.bottom = dir_y < 0 ? y1 : y2;
    queue_draw();
}

bool dv_full_display_widget::on_button_press_event(GdkEventButton * event)
    throw()
{
    if (sel_enabled_ && (event->button == 1 || event->button == 2))
    {
	sel_in_progress_ = true;
	add_modal_grab();

	int x, y;
	window_to_frame_coords(x, y, int(event->x), int(event->y));
	if (event->button == 1)
	{
	    sel_start_x_ = x;
	    sel_start_y_ = y;
	}
	update_selection(x, y);
	return true;
    }

    return false;
}

bool dv_full_display_widget::on_button_release_event(GdkEventButton * event)
    throw()
{
    if (sel_in_progress_ && (event->button == 1 || event->button == 2))
    {
	sel_in_progress_ = false;
	remove_modal_grab();
	return true;
    }

    return false;
}

bool dv_full_display_widget::on_motion_notify_event(GdkEventMotion * event)
    throw()
{
    int x, y;
    window_to_frame_coords(x, y, int(event->x), int(event->y));
    update_selection(x, y);
    return true;
}

bool dv_full_display_widget::on_expose_event(GdkEventExpose *) throw()
{
    Glib::RefPtr<Gdk::Drawable> drawable;
    int dest_x, dest_y;

    if (xv_image_)
    {
	get_window()->get_internal_paint_info(drawable, dest_x, dest_y);
	drawable->reference(); // get_internal_paint_info() doesn't do this!
    }
    else
    {
	drawable = get_window();
    }

    if (Glib::RefPtr<Gdk::GC> gc = Gdk::GC::create(drawable))
    {
	if (xv_image_)
	{
	    XvShmPutImage(get_x_display(drawable), xv_port_,
			  get_x_window(drawable),
			  gdk_x11_gc_get_xgc(gc->gobj()),
			  static_cast<XvImage *>(xv_image_),
			  source_region_.left, source_region_.top,
			  source_region_.right - source_region_.left,
			  source_region_.bottom - source_region_.top,
			  dest_x, dest_y,
			  dest_width_, dest_height_,
			  /*send_event=*/ True);
	    set_shm_busy();
	}
	else
	{
	    Gdk::Color colour;
	    colour.set_grey(0); // black
	    gc->set_rgb_fg_color(colour);
	    drawable->draw_rectangle(gc, true, 0, 0, dest_width_, dest_height_);
	}
    }

    return true;
}

void dv_full_display_widget::on_unrealize() throw()
{
    fini_xvideo();

    dv_display_widget::on_unrealize();
}

// dv_thumb_display_widget

namespace
{
    const unsigned dv_block_size_log2 = 3;
    const unsigned dv_block_size = 1 << dv_block_size_log2;

    const unsigned frame_thumb_linesize_4 =
	(FRAME_WIDTH / dv_block_size + 15) & ~15;
    const unsigned frame_thumb_linesize_2 =
	(FRAME_WIDTH / 2 / dv_block_size + 15) & ~15;
}

struct dv_thumb_display_widget::raw_frame_thumb
{
    AVFrame header;
    enum PixelFormat pix_fmt;
    dv_frame_aspect aspect;
    struct
    {
	uint8_t y[frame_thumb_linesize_4 * FRAME_HEIGHT_MAX / dv_block_size];
	uint8_t c_dummy[frame_thumb_linesize_2];
    } buffer __attribute__((aligned(16)));
};

dv_thumb_display_widget::dv_thumb_display_widget()
    : dv_display_widget(dv_block_size_log2),
      raw_frame_(new raw_frame_thumb),
      x_image_(0),
      x_shm_info_(0),
      dest_width_(0),
      dest_height_(0),
      error_pixbuf_(load_icon("gtk-dialog-warning", 64)),
      error_(false)
{
    // We don't know what the frame format will be, but assume "PAL"
    // 4:3 frames and therefore an active image size of 702x576 and
    // pixel aspect ratio of 59:54.
    set_size_request(192, 144);
}

dv_thumb_display_widget::~dv_thumb_display_widget()
{
}

bool dv_thumb_display_widget::try_init_xshm(PixelFormat pix_fmt,
					    unsigned height) throw()
{
    assert(pix_fmt == PIX_FMT_YUV420P || pix_fmt == PIX_FMT_YUV422P
	   || pix_fmt == PIX_FMT_YUV410P || pix_fmt == PIX_FMT_YUV411P);
    assert(height <= FRAME_HEIGHT_MAX / dv_block_size);

    if (x_image_)
    {
	raw_frame_->pix_fmt = pix_fmt;
	return true;
    }

    if (!init_x_shm_events())
	return false;

    Display * x_display = get_x_display(*this);

    Glib::RefPtr<Gdk::Drawable> drawable;
    int dest_x, dest_y;
    get_window()->get_internal_paint_info(drawable, dest_x, dest_y);
    drawable->reference(); // get_internal_paint_info() doesn't do this!
    Visual * visual =
	gdk_x11_visual_get_xvisual(drawable->get_visual()->gobj());
    int depth = drawable->get_depth();

    if ((visual->c_class == TrueColor || visual->c_class == DirectColor)
	&& (depth == 24 || depth == 32))
    {
	if (XShmSegmentInfo * x_shm_info = new (std::nothrow) XShmSegmentInfo)
	{
	    if (XImage * x_image = XShmCreateImage(
		    x_display, visual, depth, ZPixmap,
		    0, x_shm_info,
		    // Calculate maximum dimensions assuming widest pixel
		    // ratio and full frame (slightly over-conservative).
		    div_round_nearest<unsigned>(FRAME_WIDTH * 118,
						81 * thumb_scale_denom),
		    div_round_nearest<unsigned>(FRAME_HEIGHT_MAX,
						thumb_scale_denom)))
	    {
		if ((x_image->data = allocate_x_shm(
			 x_display, x_shm_info,
			 x_image->height * x_image->bytes_per_line)))
		{
		    raw_frame_->pix_fmt = pix_fmt;
		    x_image_ = x_image;
		    x_shm_info_ = x_shm_info;
		}
		else
		{
		    free(x_image);
		}
	    }
	    if (!x_shm_info_)
		delete x_shm_info;
	}

	if (!x_image_)
	    std::cerr << "ERROR: Could not create Xshm image\n";
    }
    else
    {
	std::cerr << "ERROR: Window does not support 24- or 32-bit colour\n";
    }

    return x_image_;
}

void dv_thumb_display_widget::fini_xshm() throw()
{
    if (XImage * x_image = static_cast<XImage *>(x_image_))
    {
	XShmSegmentInfo * x_shm_info =
	    static_cast<XShmSegmentInfo *>(x_shm_info_);
	free_x_shm(x_shm_info);
	delete x_shm_info;
	x_shm_info_ = 0;
	free(x_image);
	x_image_ = 0;

	fini_x_shm_events();
    }
}

void dv_thumb_display_widget::on_unrealize() throw()
{
    fini_xshm();

    dv_display_widget::on_unrealize();
}

AVFrame * dv_thumb_display_widget::get_frame_header()
{
    return &raw_frame_->header;
}

AVFrame * dv_thumb_display_widget::get_frame_buffer(AVFrame * header,
						    PixelFormat pix_fmt,
						    unsigned height)
{
    if (!try_init_xshm(pix_fmt, height))
	return 0;

    header->data[0] = raw_frame_->buffer.y;
    header->linesize[0] = frame_thumb_linesize_4;
    header->data[1] = raw_frame_->buffer.c_dummy;
    header->linesize[1] = 0;
    header->data[2] = raw_frame_->buffer.c_dummy;
    header->linesize[2] = 0;
    header->data[3] = 0;
    header->linesize[3] = 0;

    header->type = FF_BUFFER_TYPE_USER;
    return header;
}

void dv_thumb_display_widget::put_frame_buffer(
    const display_region & source_region)
{
    XImage * x_image = static_cast<XImage *>(x_image_);

    dest_width_ = div_round_nearest<unsigned>((source_region.right - source_region.left)
					      * source_region.pixel_width,
					      source_region.pixel_height
					      * thumb_scale_denom);
    dest_height_ = div_round_nearest<unsigned>(source_region.bottom - source_region.top,
					       thumb_scale_denom);

    // Scale the image up using Bresenham's algorithm

    assert(x_image->bits_per_pixel == 24 || x_image->bits_per_pixel == 32);

    const unsigned source_width = ((source_region.right - source_region.left)
				   / dv_block_size);
    const unsigned source_height = ((source_region.bottom - source_region.top)
				    / dv_block_size);
    assert(source_width <= dest_width_);
    assert(source_height <= dest_height_);
    unsigned source_y = source_region.top / dv_block_size, dest_y = 0;
    unsigned error_y = source_height / 2;
    do
    {
	const uint8_t * source =
	    raw_frame_->buffer.y + frame_thumb_linesize_4 * source_y
	    + source_region.left / dv_block_size;
	uint8_t * dest = reinterpret_cast<uint8_t *>(
	    x_image->data + x_image->bytes_per_line * dest_y);
	uint8_t * dest_row_end =
	    dest + x_image->bits_per_pixel / 8 * dest_width_;
	unsigned error_x = source_width / 2;
	uint8_t source_value = *source;
	do
	{
	    // Write Y component to each byte of the pixel
	    *dest++ = source_value;
	    *dest++ = source_value;
	    *dest++ = source_value;
	    if (x_image->bits_per_pixel == 32)
		*dest++ = source_value;

	    error_x += source_width;
	    if (error_x >= dest_width_)
	    {
		source_value = *++source;
		error_x -= dest_width_;
	    }
	}
	while (dest != dest_row_end);

	error_y += source_height;
	if (error_y >= dest_height_)
	{
	    ++source_y;
	    error_y -= dest_height_;
	}
	++dest_y;
    }
    while (dest_y != dest_height_);

    set_size_request(dest_width_, dest_height_);
}

void dv_thumb_display_widget::set_error(bool error)
{
    error_ = error;
}

bool dv_thumb_display_widget::on_expose_event(GdkEventExpose *) throw()
{
    if (!x_image_ || !dest_width_ || !dest_height_)
	return true;

    Glib::RefPtr<Gdk::Drawable> drawable;
    int dest_x, dest_y;
    get_window()->get_internal_paint_info(drawable, dest_x, dest_y);
    drawable->reference(); // get_internal_paint_info() doesn't do this!

    if (Glib::RefPtr<Gdk::GC> gc = Gdk::GC::create(drawable))
    {
	XShmPutImage(get_x_display(drawable),
		     get_x_window(drawable),
		     gdk_x11_gc_get_xgc(gc->gobj()),
		     static_cast<XImage *>(x_image_),
		     0, 0,
		     dest_x, dest_y,
		     dest_width_, dest_height_,
		     /*send_event=*/ True);
	set_shm_busy();

	if (error_)
	{
	    drawable->draw_pixbuf(
		gc, error_pixbuf_, 0, 0,
		dest_x + (dest_width_ - error_pixbuf_->get_width()) / 2,
		dest_y + (dest_height_ - error_pixbuf_->get_height()) / 2,
		-1, -1, Gdk::RGB_DITHER_NORMAL, 0, 0);
	}
    }

    return true;
}
