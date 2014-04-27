// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm widgets for displaying DV video

#ifndef DVSWITCH_DV_DISPLAY_WIDGET_HPP
#define DVSWITCH_DV_DISPLAY_WIDGET_HPP

#include <memory>
#include <utility>

#include <gtkmm/drawingarea.h>
#include <gtkmm/image.h>

#include "auto_codec.hpp"
#include "frame.h"
#include "frame_pool.hpp"
#include "geometry.h"

class dv_display_widget : public Gtk::DrawingArea
{
public:
    void put_frame(const dv_frame_ptr &);
    void put_frame(const raw_frame_ptr &);

protected:
    struct display_region : rectangle
    {
	unsigned pixel_width, pixel_height;
    };

    explicit dv_display_widget(int scale=1);
    ~dv_display_widget();

    virtual void put_frame_buffer(const display_region &);
    virtual bool on_expose_event(GdkEventExpose *) throw();

    bool init_x_shm_events();
    void fini_x_shm_events();
    bool try_init_xvideo(PixelFormat pix_fmt, unsigned height) throw();
    void fini_xvideo() throw();
    void set_shm_busy() { shm_busy_ = true; }

    auto_codec decoder_;
    AVFrame* frame_header_;
    PixelFormat pix_fmt_;
    unsigned height_;
    unsigned dest_width_, dest_height_;
    display_region source_region_;

private:
    display_region get_display_region(const dv_system * system,
				      dv_frame_aspect frame_aspect);
    AVFrame * get_frame_header();
    AVFrame * get_frame_buffer(AVFrame * header,
			       PixelFormat pix_fmt, unsigned height);

    virtual void set_error(bool);

    virtual void on_unrealize() throw();

    static int get_buffer(AVCodecContext *, AVFrame *);
    static void release_buffer(AVCodecContext *, AVFrame *);
    static int reget_buffer(AVCodecContext *, AVFrame *);

    static GdkFilterReturn filter_x_shm_event(void * void_event,
					      GdkEvent * event,
					      void * data);

    int scale_;
    unsigned decoded_serial_num_;
    int x_shm_first_event_;
    bool shm_busy_;
    uint32_t xv_port_;
    void * xv_image_;
    void * xv_shm_info_;
};

class dv_full_display_widget : public dv_display_widget
{
public:
    dv_full_display_widget();
    ~dv_full_display_widget();

    void set_selection_enabled(bool);
    rectangle get_selection();
    void set_safe_area_highlight(bool v) { highlight_title_safe_area_ = v; }

private:
    virtual void put_frame_buffer(const display_region &);
    void window_to_frame_coords(int & frame_x, int & frame_y,
				int window_x, int window_y) throw();
    void update_selection(int x, int y);

    virtual bool on_button_press_event(GdkEventButton *) throw();
    virtual bool on_button_release_event(GdkEventButton *) throw();
    virtual bool on_motion_notify_event(GdkEventMotion *) throw();

    bool sel_enabled_;
    bool sel_in_progress_;
    bool highlight_title_safe_area_;
    int sel_start_x_, sel_start_y_;
    rectangle selection_;
};

class dv_thumb_display_widget : public dv_display_widget
{
public:
    dv_thumb_display_widget();
    ~dv_thumb_display_widget();

private:
    virtual void set_error(bool);

    virtual bool on_expose_event(GdkEventExpose *) throw();

    Glib::RefPtr<Gdk::Pixbuf> error_pixbuf_;
    bool error_;
};

#endif // !defined(DVSWITCH_DV_DISPLAY_WIDGET_HPP)
