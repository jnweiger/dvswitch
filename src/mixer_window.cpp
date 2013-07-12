// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// The top-level window

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>

#include <fcntl.h>
#include <unistd.h>

#include <libintl.h>

#include <gdk/gdkkeysyms.h>
#include <gtkmm/entry.h>
#include <gtkmm/main.h>
#include <gtkmm/stock.h>
#include <gtkmm/stockid.h>
#include <gtkmm/messagedialog.h>

#include "connector.hpp"
#include "format_dialog.hpp"
#include "frame.h"
#include "gui.hpp"
#include "mixer.hpp"
#include "mixer_window.hpp"
#include "sources_dialog.hpp"

// Window layout:
//
// +-------------------------------------------------------------------+
// | ╔═══════════════════════════════════════════════════════════════╗ |
// | ║                          menu_bar_                            ║ |
// | ╠═══════════════════════════════════════════════════════════════╣ |
// | ║+-----╥-------------------------------------------------------+║main_box_
// | ║|     ║                                                       |upper_box_
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|comm-║                                                       |║ |
// | ║|and_-║                     osd_/display_                     |║ |
// | ║|box_ ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║|     ║                                                       |║ |
// | ║+-----╨-------------------------------------------------------+║ |
// | ╠═══════════════════════════════════════════════════════════════╣ |
// | ║                                                               ║ |
// | ║                                                               ║ |
// | ║                          selector_                            ║ |
// | ║                                                               ║ |
// | ║                                                               ║ |
// | ╚═══════════════════════════════════════════════════════════════╝ |
// +-------------------------------------------------------------------+

mixer_window::mixer_window(mixer & mixer, connector & connector)
    : mixer_(mixer),
      connector_(connector),
      file_menu_item_(gettext("_File"), true),
      quit_menu_item_(Gtk::StockID("gtk-quit")),
      settings_menu_item_(gettext("_Settings"), true),
      format_menu_item_(gettext("_Format"), true),
      sources_menu_item_(gettext("_Sources"), true),
      safe_area_menu_item_(gettext("_Highlight safe area"), true),
      fullscreen_menu_item_(gettext("Fu_ll screen"), true),
      status_bar_menu_item_(gettext("Status _Bar"), true),
      status_bar_radio_grp(),
      status_bar_on_menu_item_(status_bar_radio_grp, gettext("On"), false),
      status_bar_off_menu_item_(status_bar_radio_grp, gettext("Off"), false),
      status_bar_blink_menu_item_(status_bar_radio_grp, gettext("Blink"), false),
      record_button_(),
      record_icon_(Gtk::Stock::MEDIA_RECORD, Gtk::ICON_SIZE_BUTTON),
      cut_button_(),
      cut_icon_(Gtk::Stock::CUT, Gtk::ICON_SIZE_BUTTON),
      effects_frame_(gettext("Effects")),
      none_button_(effect_group_, gettext("No effect/transition")),
      pip_button_(effect_group_, gettext("_Pic-in-pic"), true),
      mfade_button_(effect_group_, gettext("_Manual fade"), true),
      mfade_area_choice_(),
      tfade_button_(effect_group_, gettext("Timed fa_de"), true),
      tfade_label_(gettext("Transition speed [ms]:")),
      tfade_value_(40, 15040, 40),
      mfade_label_(gettext("Manual fade A/B:")),
      mfade_ab_(0, 256, 1),
      apply_button_(),
      apply_icon_(Gtk::Stock::APPLY, Gtk::ICON_SIZE_BUTTON),
      trans_frame_(gettext("Transitions")),
      vu_meter_(-56, 0),
      pri_video_source_id_(0),
      sec_video_source_id_(0),
      pip_active_(false),
      pip_pending_(false),
      mfade_active_(false),
      mfade_area_(0),
      progress_active_(false),
      fullscreen_state_(false),
      wakeup_pipe_(O_NONBLOCK, O_NONBLOCK),
      next_source_id_(0),
      osc_(NULL),
      source_count_(0)
{
    // In some locales (e.g., Dutch), the shortcuts of the stock button
    // labels conflict (e.g., _record -> op_nemen, cu_t -> k_nippen).
    // So, rather than using stock buttons, use buttons with our own
    // gettexted strings, and the same stock labels; the look will be
    // the same, but now the shortcut can be overridden for localized
    // variants.
    record_button_.set_label(gettext("_Record"));
    record_button_.set_use_underline();
    record_button_.set_image(record_icon_);
    cut_button_.set_label(gettext("Cu_t"));
    cut_button_.set_use_underline();
    cut_button_.set_image(cut_icon_);
    apply_button_.set_label(gettext("_Apply"));
    apply_button_.set_use_underline();
    apply_button_.set_image(apply_icon_);

    Glib::RefPtr<Glib::IOSource> pipe_io_source(
	Glib::IOSource::create(wakeup_pipe_.reader.get(), Glib::IO_IN));
    pipe_io_source->set_priority(Glib::PRIORITY_DEFAULT_IDLE);
    pipe_io_source->connect(sigc::mem_fun(this, &mixer_window::update));
    pipe_io_source->attach();

    set_mnemonic_modifier(Gdk::ModifierType(0));

    quit_menu_item_.signal_activate().connect(sigc::mem_fun(this, &mixer_window::open_quit_dialog));
    quit_menu_item_.show();
    file_menu_.add(quit_menu_item_);
    file_menu_item_.set_submenu(file_menu_);
    file_menu_item_.show();
    menu_bar_.add(file_menu_item_);
    format_menu_item_.signal_activate().connect(
	sigc::mem_fun(this, &mixer_window::open_format_dialog));
    format_menu_item_.show();
    sources_menu_item_.signal_activate().connect(
	sigc::mem_fun(this, &mixer_window::open_sources_dialog));
    sources_menu_item_.show();
    safe_area_menu_item_.signal_toggled().connect(
	sigc::mem_fun(this, &mixer_window::toggle_safe_area_display));
    safe_area_menu_item_.show();
    safe_area_menu_item_.set_active(true);
    fullscreen_menu_item_.signal_toggled().connect(
    	sigc::mem_fun(this, &mixer_window::toggle_fullscreen));
    fullscreen_menu_item_.show();
    fullscreen_menu_item_.set_active(false);

    settings_menu_.add(sources_menu_item_);
    settings_menu_.add(format_menu_item_);
    settings_menu_.add(safe_area_menu_item_);
    settings_menu_.add(fullscreen_menu_item_);

    status_bar_on_menu_item_.signal_toggled().connect(
	sigc::bind<0>(sigc::mem_fun(osd_, &status_overlay::set_bar_mode), status_overlay::BAR_ON));
    status_bar_off_menu_item_.signal_toggled().connect(
	sigc::bind<0>(sigc::mem_fun(osd_, &status_overlay::set_bar_mode), status_overlay::BAR_OFF));
    status_bar_blink_menu_item_.signal_toggled().connect(
	sigc::bind<0>(sigc::mem_fun(osd_, &status_overlay::set_bar_mode), status_overlay::BAR_BLINK));

    status_bar_on_menu_item_.set_active(true);
    status_bar_on_menu_item_.show();
    status_bar_off_menu_item_.show();
    status_bar_blink_menu_item_.show();
    status_bar_menu_.add(status_bar_on_menu_item_);
    status_bar_menu_.add(status_bar_off_menu_item_);
    status_bar_menu_.add(status_bar_blink_menu_item_);
    status_bar_menu_item_.show();
    status_bar_menu_item_.set_submenu(status_bar_menu_);
    settings_menu_.add(status_bar_menu_item_);

    settings_menu_item_.set_submenu(settings_menu_);
    settings_menu_item_.show();
    menu_bar_.add(settings_menu_item_);
    menu_bar_.show();

    record_button_.set_mode(/*draw_indicator=*/false);
    record_button_.signal_toggled().connect(
	sigc::mem_fun(*this, &mixer_window::toggle_record));
    record_button_.set_sensitive(false);
    record_button_.show();

    cut_button_.set_sensitive(false);
    cut_button_.signal_clicked().connect(sigc::mem_fun(mixer_, &mixer::cut));
    cut_button_.show();

    command_sep_.show();

    none_button_.set_mode(/*draw_indicator=*/false);
    none_button_.set_sensitive(false);
    none_button_.signal_clicked().connect(
	sigc::mem_fun(this, &mixer_window::cancel_effect));
    none_button_.add_accelerator("activate",
				 get_accel_group(),
				 GDK_Escape,
				 Gdk::ModifierType(0),
				 Gtk::AccelFlags(0));
    none_button_.show();

    pip_button_.set_mode(/*draw_indicator=*/false);
    pip_button_.set_sensitive(false);
    pip_button_.signal_clicked().connect(
	sigc::mem_fun(this, &mixer_window::begin_pic_in_pic));
    pip_button_.show();

    mfade_button_.set_mode(/*draw_indicator=*/false);
    mfade_button_.set_sensitive(false);
    mfade_button_.signal_clicked().connect(
    	sigc::mem_fun(this, &mixer_window::begin_mfade));
    mfade_button_.show();

    // CAUTION: keep in sync with video_effect.c:video_effect_fade():320ff
    mfade_area_choice_.append_text(gettext("Full"));
    mfade_area_choice_.append_text(gettext("1/2b"));
    mfade_area_choice_.append_text(gettext("1/3b"));
    mfade_area_choice_.append_text(gettext("1/4b"));
    mfade_area_choice_.append_text(gettext("1/6b"));
    mfade_area_choice_.append_text(gettext("1/6t"));
    mfade_area_choice_.append_text(gettext("1/4t"));
    mfade_area_choice_.append_text(gettext("1/3t"));
    mfade_area_choice_.append_text(gettext("1/2t"));
    mfade_area_choice_.set_sensitive(true);
    mfade_area_choice_.set_active(0);	// activate first entry
    mfade_area_choice_.signal_changed().connect(
    	sigc::mem_fun(this, &mixer_window::mfade_update));
    mfade_area_choice_.show();

    tfade_button_.set_mode(/*draw_indicator=*/false);
    tfade_button_.set_sensitive(false);
    tfade_button_.signal_clicked().connect(
        sigc::mem_fun(this, &mixer_window::begin_tfade));
    tfade_button_.show();

    // there are no stacked effects: manual and timed fading is exclusive:
    // starting a timed fade will cancel the manual one.
    // a short fade (2-3 frames) can still trick the viewer.
    // once effect stacking is possible this should likley be set to 
    // a sane default - i.e 500 ms - 2 seconds is rather long.
    tfade_value_.set_value(100);

    tfade_value_.set_sensitive(false);
    tfade_value_.set_value_pos(Gtk::POS_BOTTOM);
    tfade_value_.show();
    tfade_label_.show();

    mfade_ab_.set_value(0);
    mfade_ab_.set_draw_value(false);
    mfade_ab_.set_sensitive(false);
    mfade_ab_.show();
    mfade_label_.show();

    mfade_ab_.signal_value_changed().connect(
	sigc::mem_fun(this, &mixer_window::mfade_update));

    apply_button_.set_sensitive(false);
    apply_button_.signal_clicked().connect(
	sigc::mem_fun(this, &mixer_window::apply_effect));
    apply_button_.add_accelerator("activate",
				  get_accel_group(),
				  GDK_Return,
				  Gdk::ModifierType(0),
				  Gtk::AccelFlags(0));
    apply_button_.add_accelerator("activate",
				  get_accel_group(),
				  GDK_KP_Enter,
				  Gdk::ModifierType(0),
				  Gtk::AccelFlags(0));
    apply_button_.show();

    progress_.set_text(gettext("Transition Progress"));
    progress_.show();

    vu_meter_.show();

    display_.show();

    osd_.add(display_);
    osd_.set_status(gettext("STOPPED"), "gtk-media-stop");
    osd_.show();

    selector_.set_border_width(gui_standard_spacing);
    selector_.set_accel_group(get_accel_group());
    selector_.signal_pri_video_selected().connect(
	sigc::mem_fun(*this, &mixer_window::set_pri_video_source));
    selector_.signal_sec_video_selected().connect(
	sigc::mem_fun(*this, &mixer_window::set_sec_video_source));
    selector_.signal_audio_selected().connect(
	sigc::mem_fun(mixer_, &mixer::set_audio_source));
    selector_.show();

    effects_mf_box_.set_border_width(0);
    effects_mf_box_.set_spacing(gui_standard_spacing);
    effects_mf_box_.pack_start(mfade_button_, Gtk::PACK_EXPAND_WIDGET);
    effects_mf_box_.pack_start(mfade_area_choice_, Gtk::PACK_SHRINK);
    effects_mf_box_.show();

    effects_box_.set_border_width(gui_standard_spacing);
    effects_box_.set_spacing(gui_standard_spacing);
    effects_box_.pack_start(apply_button_, Gtk::PACK_SHRINK);
    effects_box_.pack_start(pip_button_, Gtk::PACK_SHRINK);
    effects_box_.pack_start(effects_mf_box_, Gtk::PACK_SHRINK);
    effects_box_.pack_start(mfade_label_, Gtk::PACK_SHRINK);
    effects_box_.pack_start(mfade_ab_, Gtk::PACK_SHRINK);
    effects_box_.show();
    effects_frame_.add(effects_box_);
    effects_frame_.show();

    trans_box_.set_border_width(gui_standard_spacing);
    trans_box_.set_spacing(gui_standard_spacing);
    trans_box_.pack_start(tfade_button_, Gtk::PACK_SHRINK);
    trans_box_.pack_start(tfade_label_, Gtk::PACK_SHRINK);
    trans_box_.pack_start(tfade_value_, Gtk::PACK_SHRINK);
    trans_box_.pack_start(progress_, Gtk::PACK_SHRINK);
    trans_box_.show();
    trans_frame_.add(trans_box_);
    trans_frame_.show();

    command_box_.set_spacing(gui_standard_spacing);
    command_box_.pack_start(record_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(cut_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(command_sep_, Gtk::PACK_SHRINK);
    command_box_.pack_start(none_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(effects_frame_,Gtk::PACK_SHRINK);
    command_box_.pack_start(trans_frame_,Gtk::PACK_SHRINK);
    command_box_.pack_start(vu_meter_, Gtk::PACK_EXPAND_WIDGET);
    command_box_.show();

    upper_box_.set_border_width(gui_standard_spacing);
    upper_box_.set_spacing(gui_standard_spacing);
    upper_box_.pack_start(command_box_, Gtk::PACK_SHRINK);
    upper_box_.pack_start(osd_, Gtk::PACK_EXPAND_PADDING);
    upper_box_.show();

    main_box_.pack_start(menu_bar_, Gtk::PACK_SHRINK);
    main_box_.pack_start(upper_box_, Gtk::PACK_SHRINK);
    main_box_.pack_start(selector_, Gtk::PACK_EXPAND_PADDING);
    main_box_.show();
    add(main_box_);
}

mixer_window::~mixer_window()
{
    // display_ will be destroyed before osd_ so we must remove it first
    osd_.remove(display_);
}

void mixer_window::init_osc_connection(OSC * osc)
{
    osc_ = osc;
    osc_->signal_pri_video_selected().connect(
	sigc::mem_fun(selector_, &dv_selector_widget::select_pri));
    osc_->signal_sec_video_selected().connect(
	sigc::mem_fun(selector_, &dv_selector_widget::select_sec));
    osc_->signal_audio_selected().connect(
	sigc::mem_fun(selector_, &dv_selector_widget::select_snd));

    osc_->signal_mfade_set().connect(
	sigc::mem_fun(*this, &mixer_window::mfade_set));
    osc_->signal_tfade_set().connect(
	sigc::mem_fun(*this, &mixer_window::tfade_set));

    osc_->signal_cut_recording().connect(
	sigc::mem_fun(mixer_, &mixer::cut));
    osc_->signal_stop_recording().connect(
	sigc::mem_fun(*this, &mixer_window::rec_stop));
    osc_->signal_start_recording().connect(
	sigc::mem_fun(*this, &mixer_window::rec_start));
}

void mixer_window::rec_start()
{
    if (!mixer_.can_record()) return;
    if (record_button_.get_active()) return;
    record_button_.set_active(true);
    toggle_record();
}

void mixer_window::rec_stop()
{
    if (!record_button_.get_active()) return;
    record_button_.set_active(false);
    toggle_record();
}

void mixer_window::mfade_set(int val)
{
    if (!allow_mfade_) {
	return;
    }
    mfade_button_.set_active(allow_mfade_);
    mfade_ab_.set_value(val);
    mfade_update();
}

void mixer_window::tfade_set(int val)
{
    if (val < 10 || val > 60000)
    {
	cancel_effect();
	none_button_.set_active(true);
	return;
    }
    tfade_button_.set_active(true);
    tfade_value_.set_value(val);
    begin_mfade();
}

void mixer_window::cancel_effect()
{
    pip_pending_ = false;
    pip_active_ = false;
    tfade_pending_ = false;
    mfade_active_ = false;
    mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
    display_.set_selection_enabled(false);
    apply_button_.set_sensitive(false);
    tfade_value_.set_sensitive(false);
}

void mixer_window::begin_pic_in_pic()
{
    pip_pending_ = true;
    mfade_active_ = false;
    tfade_pending_ = false;
    display_.set_selection_enabled(true);
    apply_button_.set_sensitive(true);
}

void mixer_window::begin_tfade()
{
    tfade_pending_ = true;
    mfade_active_ = false;
    tfade_value_.set_sensitive(true);
    // pic-in-pic doesn't actually work well with fade ATM, but at least try to
    // handle it to some degree
    if (pip_pending_)
    {
	pip_pending_ = false;
	display_.set_selection_enabled(false);
	apply_button_.set_sensitive(false);
    }
}

void mixer_window::begin_mfade()
{
    mfade_active_ = true;
    tfade_pending_ = false;
    if (pip_pending_)
    {
    	pip_pending_ = false;
	display_.set_selection_enabled(false);
	apply_button_.set_sensitive(false);
    }
    pip_active_ = false;
    mfade_mix();
}

void mixer_window::effect_status(int min, int cur, int max, bool more)
{
    if (tfade_pending_)
    {
	if (!more)
	{
	    pri_video_source_id_ = tfade_target_;
	    // cancel manual fade after timed one
	    // timed fade effect currently overrides manual fade effect anyway
	    mixer_.set_video_mix(
		mixer_.create_video_mix_simple(pri_video_source_id_));
	    progress_active_ = false;
	    return;
	}
	progress_val_ = ((double)(cur - min)) / ((double)(max - min));
	progress_active_ = true;
    }
}

void mixer_window::apply_effect()
{
    if (source_count_ < 1)
    {
	pip_pending_ = false;
	return;
    }

    if (pip_pending_)
    {
	rectangle region = display_.get_selection();
	if (region.empty())
	    return;

	pip_pending_ = false;
	pip_active_ = true;
	mfade_active_ = false;
	mixer_.set_video_mix(
	    mixer_.create_video_mix_pic_in_pic(
		pri_video_source_id_, sec_video_source_id_, region));
	display_.set_selection_enabled(false);
    }
    apply_button_.set_sensitive(false);
}

void mixer_window::open_format_dialog()
{
    format_dialog dialog(*this, mixer_.get_format());
    if (dialog.run())
    {
	mixer::format_settings format = dialog.get_settings();
	mixer_.set_format(format);
    }
}

void mixer_window::open_sources_dialog()
{
    sources_dialog dialog(*this, mixer_, connector_);
    dialog.run();
}

void mixer_window::open_quit_dialog()
{
    Gtk::MessageDialog dialog (*this, gettext("Really quit?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE);
    dialog.add_button(gettext("No, continue running dvswitch."), Gtk::RESPONSE_CANCEL);
    dialog.add_button(gettext("Yes, exit dvswitch."), Gtk::RESPONSE_YES);
    dialog.set_default_response(Gtk::RESPONSE_CANCEL);
    dialog.set_position (Gtk::WIN_POS_CENTER_ON_PARENT);
    switch (dialog.run())
    {
	case Gtk::RESPONSE_ACCEPT:
	case Gtk::RESPONSE_YES:
	    Gtk::Main::quit();
	    break;
	default:
	    break;
    }
}

void mixer_window::toggle_record() throw()
{
    bool flag = record_button_.get_active();
    mixer_.enable_record(flag);
    cut_button_.set_sensitive(flag);
    if (flag)
	osd_.set_status(gettext("RECORDING"), "gtk-media-record", 2);
    else
	osd_.set_status(gettext("STOPPED"), "gtk-media-stop");
}
void mixer_window::toggle_safe_area_display()
{
    display_.set_safe_area_highlight(safe_area_menu_item_.get_active());
}

void mixer_window::toggle_fullscreen()
{
    fullscreen_state_ = !fullscreen_state_;

    if(fullscreen_state_)
    {
    	fullscreen();
    } else {
    	unfullscreen();
    }
}

void mixer_window::set_pri_video_source(mixer::source_id id)
{
    if (id >= source_count_) return;

    // If the secondary source is becoming the primary source, cancel
    // the effect rather than mixing it with itself.
    if (id == sec_video_source_id_)
    {
        if (pip_active_ || pip_pending_)
        {
	    pip_active_ = false;
	    none_button_.set_active();
	}
	allow_mfade_ = false;
    }
    else
    {
        allow_mfade_ = true;
    }

    mfade_mix();

    // If the fade is active, apply the transition rather than switching the
    // primary source.
    if (tfade_pending_)
    {
	tfade_target_ = id;
	mixer_.set_video_mix(
	    mixer_.create_video_mix_fade(pri_video_source_id_, tfade_target_,
					 true, int(tfade_value_.get_value()), 0));
        pip_active_ = false;
	mfade_active_ = false;
	return;
    }

    pri_video_source_id_ = id;

    if (pip_active_)
    {
	mixer_.set_video_mix(
	    mixer_.create_video_mix_pic_in_pic(
		pri_video_source_id_, sec_video_source_id_, display_.get_selection()));
	return;
    }

    if (mfade_active_)
    {
        return;
    }
    mixer_.set_video_mix(
    	mixer_.create_video_mix_simple(pri_video_source_id_));
}

void mixer_window::set_sec_video_source(mixer::source_id id)
{
    if (id >= source_count_) return;

    sec_video_source_id_ = id;

    if (pip_active_)
    {
	mixer_.set_video_mix(
	    mixer_.create_video_mix_pic_in_pic(
		pri_video_source_id_, sec_video_source_id_, display_.get_selection()));
	allow_mfade_ = false;
    }
    if (pri_video_source_id_ != sec_video_source_id_)
    {
        allow_mfade_ = true;
    }
    else
    {
    	allow_mfade_ = false;
	if (mfade_active_)
	{
	    none_button_.set_active();
	}
    }
    mfade_mix();
}

void mixer_window::mfade_mix()
{
    if (pri_video_source_id_ >= source_count_) return;
    if (sec_video_source_id_ >= source_count_) return;

    if (sec_video_source_id_ != pri_video_source_id_)
    {
	int fade = mfade_ab_.get_value();
	if (fade < 1)
	{
	    mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
	}
	else if (fade > 254 && mfade_area_ == 0)
	{
	    mixer_.set_video_mix(mixer_.create_video_mix_simple(sec_video_source_id_));
	}
	else
	{
	    mixer_.set_video_mix(mixer_.create_video_mix_fade(pri_video_source_id_, sec_video_source_id_, false, 0, fade, mfade_area_));
	}
    }
    else
    {
	mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
    }
    mfade_ab_.set_sensitive(allow_mfade_);
    mfade_button_.set_sensitive(allow_mfade_);
}

void mixer_window::mfade_update()
{
    mfade_area_ = mfade_area_choice_.get_active_row_number();
    if (mfade_active_)
    {
	mfade_mix();
    }
}

void mixer_window::put_frames(unsigned source_count,
			      const dv_frame_ptr * source_dv,
			      mixer::mix_settings mix_settings,
			      const dv_frame_ptr & mixed_dv,
			      const raw_frame_ptr & mixed_raw)
{
    {
	boost::mutex::scoped_lock lock(frame_mutex_);
	source_dv_.assign(source_dv, source_dv + source_count);
	mix_settings_ = mix_settings;
	mixed_dv_ = mixed_dv;
	mixed_raw_ = mixed_raw;
    }

    // Poke the event loop.
    static const char dummy[1] = {0};
    write(wakeup_pipe_.writer.get(), dummy, sizeof(dummy));
}

bool mixer_window::update(Glib::IOCondition) throw()
{
    // Empty the pipe (if frames have been dropped there's nothing we
    // can do about that now).
    static char dummy[4096];
    read(wakeup_pipe_.reader.get(), dummy, sizeof(dummy));

    try
    {
	dv_frame_ptr mixed_dv;
	std::vector<dv_frame_ptr> source_dv;
	raw_frame_ptr mixed_raw;

	{
	    boost::mutex::scoped_lock lock(frame_mutex_);
	    mixed_dv = mixed_dv_;
	    mixed_dv_.reset();
	    source_dv = source_dv_;
	    source_dv_.clear();
	    mixed_raw = mixed_raw_;
	    mixed_raw_.reset();
	}

	bool can_record = mixer_.can_record();
	record_button_.set_sensitive(can_record);
	if (record_button_.get_active())
	    record_button_.set_active(can_record);

	if (mixed_raw)
	    display_.put_frame(mixed_raw);
	else if (mixed_dv)
	    display_.put_frame(mixed_dv);
	if (mixed_dv)
	{
	    int levels[2];
	    dv_buffer_get_audio_levels(mixed_dv->buffer, levels);
	    vu_meter_.set_levels(levels);
	}

	std::size_t count = source_dv.size();
	selector_.set_source_count(count);
	none_button_.set_sensitive(count >= 1);
	pip_button_.set_sensitive(count >= 2);
	tfade_button_.set_sensitive(count >= 2);
	mfade_button_.set_sensitive((count >= 2) && allow_mfade_);

	source_count_ = count;

	// Update the thumbnail displays of sources.  If a new mixed frame
	// arrives while we were doing this, return to the event loop.
	// (We want to handle the next mixed frame but we need to let it
	// handle other events as well.)  Use a rota for source updates so
	// even if we don't have time to run them all at full frame rate
	// they all get updated at roughly the same rate.

	for (std::size_t i = 0; i != source_dv.size(); ++i)
	{
	    if (next_source_id_ >= source_dv.size())
		next_source_id_ = 0;
	    mixer::source_id id = next_source_id_++;

	    if (source_dv[id])
	    {
		selector_.put_frame(id, source_dv[id]);

		boost::mutex::scoped_lock lock(frame_mutex_);
		if (mixed_dv_)
		    break;
	    }
	}
    }
    catch (std::exception & e)
    {
	std::cerr << "ERROR: Failed to update window: " << e.what() << "\n";
    }

    if (progress_active_)
    {
	progress_.set_fraction(progress_val_);
	progress_.set_sensitive(true);
    }
    else
    {
	progress_.set_fraction(0.0);
	progress_.set_sensitive(false);
    }

    return true; // call again
}
