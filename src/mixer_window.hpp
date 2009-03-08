// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

#ifndef DVSWITCH_MIXER_WINDOW_HPP
#define DVSWITCH_MIXER_WINDOW_HPP

#include <sys/types.h>

#include <boost/thread/mutex.hpp>

#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/menu.h>
#include <gtkmm/menubar.h>
#include <gtkmm/imagemenuitem.h>
#include <gtkmm/separator.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include "auto_pipe.hpp"
#include "dv_display_widget.hpp"
#include "dv_selector_widget.hpp"
#include "mixer.hpp"
#include "vu_meter.hpp"

namespace Glib
{
    class IOSource;
}

class mixer_window : public Gtk::Window, public mixer::monitor
{
public:
    explicit mixer_window(mixer & mixer);

private:
    void cancel_effect();
    void begin_pic_in_pic();
    void apply_effect();
    void open_format_dialog();

    void toggle_record() throw();
    bool update(Glib::IOCondition) throw();

    void set_pri_video_source(mixer::source_id);
    void set_sec_video_source(mixer::source_id);

    virtual void put_frames(unsigned source_count,
			    const dv_frame_ptr * source_dv,
			    mixer::mix_settings,
			    const dv_frame_ptr & mixed_dv,
			    const raw_frame_ptr & mixed_raw);

    mixer & mixer_;

    Gtk::VBox main_box_;
    Gtk::MenuBar menu_bar_;
    Gtk::MenuItem file_menu_item_;
    Gtk::Menu file_menu_;
    Gtk::ImageMenuItem quit_menu_item_;
    Gtk::MenuItem settings_menu_item_;
    Gtk::Menu settings_menu_;
    Gtk::MenuItem format_menu_item_;
    Gtk::HBox upper_box_;
    Gtk::VBox command_box_;
    Gtk::ToggleButton record_button_;
    Gtk::Button cut_button_;
    Gtk::HSeparator command_sep_;
    Gtk::RadioButtonGroup effect_group_;
    Gtk::RadioButton none_button_;
    Gtk::RadioButton pip_button_;
    Gtk::Button apply_button_;
    Gtk::HSeparator meter_sep_;
    vu_meter vu_meter_;
    dv_full_display_widget display_;
    dv_selector_widget selector_;

    mixer::source_id pri_video_source_id_, sec_video_source_id_;
    bool pip_active_;
    bool pip_pending_;

    auto_pipe wakeup_pipe_;

    boost::mutex frame_mutex_; // controls access to the following
    std::vector<dv_frame_ptr> source_dv_;
    mixer::source_id next_source_id_;
    mixer::mix_settings mix_settings_;
    dv_frame_ptr mixed_dv_;
    raw_frame_ptr mixed_raw_;
};

#endif // !defined(DVSWITCH_MIXER_WINDOW_HPP)
