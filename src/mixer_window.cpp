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
      record_button_(),
      record_icon_(Gtk::Stock::MEDIA_RECORD, Gtk::ICON_SIZE_BUTTON),
      cut_button_(),
      cut_icon_(Gtk::Stock::CUT, Gtk::ICON_SIZE_BUTTON),
      none_button_(effect_group_, gettext("No effect")),
      pip_button_(effect_group_, gettext("_Pic-in-pic"), true),
      fade_button_(effect_group_, gettext("Fa_de"), true),
      fade_label_(gettext("Fade Speed [ms]:")),
      fade_value_(40, 15040, 40),
      overlay_label_(gettext("Overlay A/B:")),
      overlay_ab_(0, 256, 1),
      apply_button_(),
      apply_icon_(Gtk::Stock::APPLY, Gtk::ICON_SIZE_BUTTON),
      vu_meter_(-56, 0),
      pri_video_source_id_(0),
      sec_video_source_id_(0),
      pip_active_(false),
      pip_pending_(false),
      overlay_active_(false),
      progress_active_(false),
      wakeup_pipe_(O_NONBLOCK, O_NONBLOCK),
      next_source_id_(0)
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
    settings_menu_.add(sources_menu_item_);
    settings_menu_.add(format_menu_item_);
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

    fade_button_.set_mode(/*draw_indicator=*/false);
    fade_button_.set_sensitive(false);
    fade_button_.signal_clicked().connect(
        sigc::mem_fun(this, &mixer_window::begin_fade));
    fade_button_.show();

    // Note: default value - before overlay patch - was (2000.0)
    // there are no stacked effects: overlay and fading is exclusive:
    // starting a fade will cancel the overlay.
    // a short fade (2-3 frames) can still trick the viewer.
    // once effect stacking is possible this should likley be set to 
    // a sane default - i.e 500 ms - 2 seconds is rather long.
    fade_value_.set_value(100);

    fade_value_.set_sensitive(false);
    fade_value_.set_value_pos(Gtk::POS_BOTTOM);
    fade_value_.show();
    fade_label_.show();

    overlay_ab_.set_value(0);
    overlay_ab_.set_draw_value(false);
    overlay_ab_.set_sensitive(false);
    overlay_ab_.show();
    overlay_label_.show();
    overlay_sep_.show();

    overlay_ab_.signal_value_changed().connect(
	sigc::mem_fun(this, &mixer_window::overlay_update));

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

    progress_.set_text(gettext("Fade Progress"));
    progress_.show();

    meter_sep_.show();

    vu_meter_.show();

    display_.show();

    osd_.add(display_);
    osd_.set_status(gettext("STOP"), "gtk-media-stop");
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

    command_box_.set_spacing(gui_standard_spacing);
    command_box_.pack_start(record_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(cut_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(command_sep_, Gtk::PACK_SHRINK);
    command_box_.pack_start(none_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(pip_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(apply_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(fade_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(fade_label_, Gtk::PACK_SHRINK);
    command_box_.pack_start(fade_value_, Gtk::PACK_SHRINK);
    command_box_.pack_start(progress_, Gtk::PACK_SHRINK);
    command_box_.pack_start(overlay_sep_, Gtk::PACK_SHRINK);
    command_box_.pack_start(overlay_label_, Gtk::PACK_SHRINK);
    command_box_.pack_start(overlay_ab_, Gtk::PACK_SHRINK);
    command_box_.pack_start(meter_sep_, Gtk::PACK_SHRINK);
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

void mixer_window::cancel_effect()
{
    pip_pending_ = false;
    pip_active_ = false;
    fade_pending_ = false;
    overlay_mix(true);
    mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
    display_.set_selection_enabled(false);
    apply_button_.set_sensitive(false);
    fade_value_.set_sensitive(false);
    overlay_ab_.set_sensitive(overlay_active_);
}

void mixer_window::begin_pic_in_pic()
{
    pip_pending_ = true;
    display_.set_selection_enabled(true);
    apply_button_.set_sensitive(true);
}

void mixer_window::begin_fade()
{
    fade_pending_ = true;
    overlay_active_ = false;
    overlay_ab_.set_sensitive(overlay_active_);
    fade_value_.set_sensitive(true);
    // pic-in-pic doesn't actually work well with fade ATM, but at least try to
    // handle it to some degree
    if (pip_pending_)
    {
	pip_pending_ = false;
	display_.set_selection_enabled(false);
	apply_button_.set_sensitive(false);
    }
}

void mixer_window::effect_status(int min, int cur, int max, bool more)
{
    if (fade_pending_)
    {
	if (!more)
	{
	    pri_video_source_id_ = fade_target_;
#ifdef WANT_RESUME_OVERLAY 
	    // resume A/B overlay after fade A->A'
	    // this should probably become a runtime-option
	    // or checkbox or toggle-button
	    overlay_mix(true);
#else
	    // cancel overlay after fade
	    // fade-effect currently overrides overlay-effect anyway
	    mixer_.set_video_mix(
		mixer_.create_video_mix_simple(pri_video_source_id_));
#endif
	    progress_active_ = false;
	    return;
	}
	progress_val_ = ((double)(cur - min)) / ((double)(max - min));
	progress_active_ = true;
    }
}

void mixer_window::apply_effect()
{
    if (pip_pending_)
    {
	rectangle region = display_.get_selection();
	if (region.empty())
	    return;

	pip_pending_ = false;
	pip_active_ = true;
	overlay_active_ = false;
	overlay_ab_.set_sensitive(overlay_active_);
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
	osd_.set_status(gettext("RECORD"), "gtk-media-record", 2);
    else
	osd_.set_status(gettext("STOP"), "gtk-media-stop");
}

void mixer_window::set_pri_video_source(mixer::source_id id)
{
    // If the fade is active, apply the transition rather than switching the
    // primary source.
    if (fade_pending_)
    {
	fade_target_ = id;
	mixer_.set_video_mix(
	    mixer_.create_video_mix_fade(pri_video_source_id_, fade_target_,
					 true, int(fade_value_.get_value())));
        pip_active_ = false;
	overlay_active_ = false;
	overlay_ab_.set_sensitive(overlay_active_);
	return;
    }

    pri_video_source_id_ = id;

    // If the secondary source is becoming the primary source, cancel
    // the effect rather than mixing it with itself.
    if (pip_active_ && id == sec_video_source_id_)
    {
	pip_active_ = false;
	if (!pip_pending_)
	    none_button_.set_active();
    }

    if (pip_active_)
    {
	mixer_.set_video_mix(
	    mixer_.create_video_mix_pic_in_pic(
		pri_video_source_id_, sec_video_source_id_, display_.get_selection()));
	overlay_active_ = false;
	overlay_ab_.set_sensitive(overlay_active_);
    }
    else
    {
	overlay_mix(true);
    }
}

void mixer_window::set_sec_video_source(mixer::source_id id)
{
    sec_video_source_id_ = id;

    if (pip_active_)
    {
	mixer_.set_video_mix(
	    mixer_.create_video_mix_pic_in_pic(
		pri_video_source_id_, sec_video_source_id_, display_.get_selection()));
	overlay_active_ = false;
	overlay_ab_.set_sensitive(overlay_active_);
    }
    else overlay_mix(false);
}

void mixer_window::overlay_mix(bool force_set)
{
    if (sec_video_source_id_ != pri_video_source_id_)
    {
	if (!overlay_active_)
	{
	    int fade = overlay_ab_.get_value();
	    if (fade < 1)
	    {
		mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
	    }
	    else if (fade > 254)
	    {
		mixer_.set_video_mix(mixer_.create_video_mix_simple(sec_video_source_id_));
	    }
	    else
	    {
		mixer_.set_video_mix(mixer_.create_video_mix_fade(pri_video_source_id_, sec_video_source_id_, false, 0, fade));
	    }
	}
	overlay_active_ = true;
    }
    else if (overlay_active_ || force_set)
    {
	mixer_.set_video_mix(mixer_.create_video_mix_simple(pri_video_source_id_));
	overlay_active_ = false;
    }
    overlay_ab_.set_sensitive(overlay_active_);
}

void mixer_window::overlay_update()
{
    if (overlay_active_)
    {
	overlay_active_ = false;
	overlay_mix(FALSE);
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
	fade_button_.set_sensitive(count >= 2);

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
