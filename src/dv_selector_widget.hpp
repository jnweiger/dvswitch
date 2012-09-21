// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm widget for visual selection of DV streams

#ifndef DVSWITCH_DV_SELECTOR_WIDGET_HPP
#define DVSWITCH_DV_SELECTOR_WIDGET_HPP

#include <vector>

#include <gtkmm/accelgroup.h>
#include <gtkmm/image.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/table.h>

#include "dv_display_widget.hpp"
#include "mixer.hpp"

class dv_selector_widget : public Gtk::Table
{
public:
    dv_selector_widget();

    void set_accel_group(const Glib::RefPtr<Gtk::AccelGroup> & accel_group);

    void set_source_count(unsigned);
    void put_frame(mixer::source_id source_id,
		   const dv_frame_ptr & source_frame);

    void select_pri(mixer::source_id source_id);
    void select_sec(mixer::source_id source_id);
    void select_snd(mixer::source_id source_id);

    sigc::signal1<void, mixer::source_id> & signal_pri_video_selected();
    sigc::signal1<void, mixer::source_id> & signal_sec_video_selected();
    sigc::signal1<void, mixer::source_id> & signal_audio_selected();

private:
    Gtk::RadioButton * create_radio_button(
	Gtk::RadioButtonGroup & group,
	const Glib::RefPtr<Gdk::Pixbuf> & pixbuf);
    void on_pri_video_selected(mixer::source_id);
    void on_sec_video_selected(mixer::source_id);
    void on_audio_selected(mixer::source_id);

    Glib::RefPtr<Gtk::AccelGroup> accel_group_;
    Glib::RefPtr<Gdk::Pixbuf> pri_video_source_pixbuf_;
    Glib::RefPtr<Gdk::Pixbuf> sec_video_source_pixbuf_;
    Gtk::RadioButtonGroup pri_video_button_group_;
    Gtk::RadioButtonGroup sec_video_button_group_;
    sigc::signal1<void, mixer::source_id> pri_video_selected_signal_;
    sigc::signal1<void, mixer::source_id> sec_video_selected_signal_;
    Glib::RefPtr<Gdk::Pixbuf> audio_source_pixbuf_;
    Gtk::RadioButtonGroup audio_button_group_;
    sigc::signal1<void, mixer::source_id> audio_selected_signal_;
    std::vector<dv_thumb_display_widget *> thumbnails_;
    std::vector<Gtk::RadioButton *> pri_btn_;
    std::vector<Gtk::RadioButton *> sec_btn_;
    std::vector<Gtk::RadioButton *> snd_btn_;
};

#endif // !defined(DVSWITCH_DV_SELECTOR_WIDGET_HPP)
