// Copyright 2007 Ben Hutchings.
// See the file "COPYING" for licence details.

#ifndef DVSWITCH_MIXER_WINDOW_HPP
#define DVSWITCH_MIXER_WINDOW_HPP

#include <sys/types.h>

#include <boost/thread/mutex.hpp>

#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include "auto_pipe.hpp"
#include "dv_display_widget.hpp"
#include "dv_selector_widget.hpp"
#include "mixer.hpp"

namespace Glib
{
    class IOSource;
}

class mixer_window : public Gtk::Window, public mixer::monitor
{
public:
    explicit mixer_window(mixer & mixer);

private:
    virtual bool on_key_press_event(GdkEventKey *) throw();
    void toggle_record() throw();
    bool update(Glib::IOCondition) throw();

    virtual void put_frames(unsigned source_count,
			    const dv_frame_ptr * source_frames,
			    mixer::mix_settings,
			    const dv_frame_ptr & mixed_frame);

    mixer & mixer_;

    Gtk::VBox main_box_;
    Gtk::HBox upper_box_;
    Gtk::VBox command_box_;
    Gtk::ToggleButton record_button_;
    Gtk::Button cut_button_;
    dv_full_display_widget display_;
    dv_selector_widget selector_;

    auto_pipe wakeup_pipe_;

    boost::mutex frame_mutex_; // controls access to the following
    std::vector<dv_frame_ptr> source_frames_;
    mixer::source_id next_source_id_;
    mixer::mix_settings mix_settings_;
    dv_frame_ptr mixed_frame_;
};

#endif // !defined(DVSWITCH_MIXER_WINDOW_HPP)
