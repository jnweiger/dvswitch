// Copyright 2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm dialog for choosing DV format settings

#include <cassert>

#include <gtkmm/stockid.h>

#include "format_dialog.hpp"
#include "gui.hpp"

format_dialog::format_dialog(Gtk::Window & parent,
			     mixer::format_settings settings)
    : Dialog("Format Settings", parent, /*modal=*/true),
      system_label_("Video system"),
      frame_aspect_label_("Video frame aspect ratio"),
      sample_rate_label_("Audio sample rate")
{
    add_button(Gtk::StockID("gtk-apply"), 1);
    add_button(Gtk::StockID("gtk-cancel"), 0);

    Gtk::VBox * box = get_vbox();

    box->set_spacing(gui_standard_spacing);

    system_label_.show();
    box->add(system_label_);

    system_combo_.append_text("Automatic");
    system_combo_.append_text("625 lines, 50 Hz (PAL)");
    system_combo_.append_text("525 lines, 60 Hz (NTSC)");
    if (settings.system == &dv_system_625_50)
	system_combo_.set_active(1);
    else if (settings.system == &dv_system_525_60)
	system_combo_.set_active(2);
    else
	system_combo_.set_active(0);
    system_combo_.show();
    box->add(system_combo_);

    frame_aspect_label_.show();
    box->add(frame_aspect_label_);

    frame_aspect_combo_.append_text("Automatic");
    frame_aspect_combo_.append_text("Normal (4:3)");
    frame_aspect_combo_.append_text("Wide (16:9)");
    frame_aspect_combo_.set_active(1 + settings.frame_aspect);
    frame_aspect_combo_.show();
    box->add(frame_aspect_combo_);

    sample_rate_label_.show();
    box->add(sample_rate_label_);

    sample_rate_combo_.append_text("Automatic");
    sample_rate_combo_.append_text("48 kHz");
    sample_rate_combo_.append_text("32 kHz");
    sample_rate_combo_.set_active(1 + settings.sample_rate);
    sample_rate_combo_.show();
    box->add(sample_rate_combo_);
}

mixer::format_settings format_dialog::get_settings() const
{
    mixer::format_settings settings;
    int row;

    switch (system_combo_.get_active_row_number())
    {
    case 0: settings.system = NULL; break;
    case 1: settings.system = &dv_system_625_50; break;
    case 2: settings.system = &dv_system_525_60; break;
    default: assert(!"impossible selection");
    }

    row = frame_aspect_combo_.get_active_row_number();
    assert(row >= 0 && row <= dv_frame_aspect_count);
    settings.frame_aspect = dv_frame_aspect(row - 1);

    row = sample_rate_combo_.get_active_row_number();
    assert(row >= 0 && row <= dv_sample_rate_count);
    settings.sample_rate = dv_sample_rate(row - 1);

    return settings;
}
