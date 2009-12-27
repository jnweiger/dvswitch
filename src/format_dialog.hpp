// Copyright 2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm dialog for choosing DV format settings

#ifndef DVSWITCH_FORMAT_DIALOG_HPP
#define DVSWITCH_FORMAT_DIALOG_HPP

#include <gtkmm/button.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/dialog.h>
#include <gtkmm/label.h>

#include "mixer.hpp"

class format_dialog : public Gtk::Dialog
{
public:
    format_dialog(Gtk::Window & parent, mixer::format_settings);
    mixer::format_settings get_settings() const;
private:
    Gtk::Button apply_button_;
    Gtk::Button cancel_button_;
    Gtk::Label system_label_;
    Gtk::ComboBoxText system_combo_;
    Gtk::Label frame_aspect_label_;
    Gtk::ComboBoxText frame_aspect_combo_;
    Gtk::Label sample_rate_label_;
    Gtk::ComboBoxText sample_rate_combo_;
};

#endif // !defined(DVSWITCH_FORMAT_DIALOG_HPP)
