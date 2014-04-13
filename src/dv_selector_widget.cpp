// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm widget for visual selection of DV streams

#include <iostream>
#include <ostream>

#include <gdk/gdkkeysyms.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>

#include "dv_selector_widget.hpp"
#include "gui.hpp"

namespace
{
    const unsigned thumbs_per_row = 4;
    enum {
	column_labels,
	column_display,
	column_separator,
	column_multiplier
    };
    enum {
	row_text_label,
	row_pri_video_button,
	row_sec_video_button,
	row_audio_button,
	row_multiplier
    };
}

dv_selector_widget::dv_selector_widget()
    : pri_video_source_pixbuf_(
	  Gdk::Pixbuf::create_from_file(SHAREDIR
					"/dvswitch/pri-video-source.png")),
      sec_video_source_pixbuf_(
	  Gdk::Pixbuf::create_from_file(SHAREDIR
					"/dvswitch/sec-video-source.png")),
      audio_source_pixbuf_(
	  Gdk::Pixbuf::create_from_file(SHAREDIR "/dvswitch/audio-source.png"))
{
    set_col_spacings(gui_standard_spacing);
    set_row_spacings(gui_standard_spacing);
}

Gtk::RadioButton * dv_selector_widget::create_radio_button(
    Gtk::RadioButtonGroup & group,
    const Glib::RefPtr<Gdk::Pixbuf> & pixbuf)
{
    Gtk::Image * image = manage(new Gtk::Image(pixbuf));
    image->show();

    Gtk::RadioButton * button = manage(new Gtk::RadioButton(group));
    button->set_image(*image);
    button->set_mode(/*draw_indicator=*/false);

    return button;
}

void dv_selector_widget::set_accel_group(
    const Glib::RefPtr<Gtk::AccelGroup> & accel_group)
{
    assert(!accel_group_);
    accel_group_ = accel_group;
}

void dv_selector_widget::set_source_count(unsigned count)
{
    if (count > thumbnails_.size())
    {
	resize(((count + thumbs_per_row - 1) / thumbs_per_row)
	       * row_multiplier,
	       thumbs_per_row * column_multiplier - 1);
	mixer::source_id first_new_source_id = thumbnails_.size();

	try
	{
	    thumbnails_.resize(count);
	    pri_btn_.resize(count);
	    sec_btn_.resize(count);
	    snd_btn_.resize(count);

	    for (mixer::source_id i = first_new_source_id; i != count; ++i)
	    {
		unsigned column = (i % thumbs_per_row) * column_multiplier;
		unsigned row = (i / thumbs_per_row) * row_multiplier;

		if (i % thumbs_per_row != 0)
		{
		    Gtk::VSeparator * sep = manage(new Gtk::VSeparator);
		    sep->show();
		    attach(*sep,
			   column - 1, column,
			   row, row + row_multiplier,
			   Gtk::FILL, Gtk::FILL,
			   0, 0);
		}

		dv_thumb_display_widget * thumb =
		    manage(new dv_thumb_display_widget(&pri_video_selected_signal_, i));
		// FIXME: I'd like to handle the button event here driectly, rather than in 
		// dv_thumb_display_widget::on_button_press_event() with a function pointer.
		// But I cannot catch it here.
		// thumb->add_events(Gdk::BUTTON_PRESS_MASK | Gdk::BUTTON_RELEASE_MASK
                //    | Gdk::BUTTON1_MOTION_MASK | Gdk::BUTTON2_MOTION_MASK);
		thumb->show();
		attach(*thumb,
		       column + column_display, column + column_display + 1,
		       row, row + row_multiplier,
		       Gtk::FILL, Gtk::FILL,
		       0, 0);
		thumbnails_[i] = thumb;

		char label_text[5];
		snprintf(label_text, sizeof(label_text),
			 (i < 9) ? "_%u _" : "%u _", unsigned(1 + i));
		Gtk::Label * label =
		    manage(new Gtk::Label(label_text, true));
		label->show();
		attach(*label,
		       column + column_labels, column + column_labels + 1,
		       row + row_text_label, row + row_text_label + 1,
		       Gtk::FILL, Gtk::FILL,
		       0, 0);

		Gtk::RadioButton * pri_video_button =
		    create_radio_button(pri_video_button_group_,
					pri_video_source_pixbuf_);
		pri_video_button->signal_pressed().connect(
		    sigc::bind(
			sigc::mem_fun(*this,
				      &dv_selector_widget::on_pri_video_selected),
			i));
		pri_video_button->show();
		attach(*pri_video_button,
		       column + column_labels, column + column_labels + 1,
		       row + row_pri_video_button,
		       row + row_pri_video_button + 1,
		       Gtk::FILL, Gtk::FILL,
		       0, 0);
		pri_btn_[i] = pri_video_button;

		Gtk::RadioButton * sec_video_button =
		    create_radio_button(sec_video_button_group_,
					sec_video_source_pixbuf_);
		sec_video_button->signal_pressed().connect(
		    sigc::bind(
			sigc::mem_fun(*this,
				      &dv_selector_widget::on_sec_video_selected),
			i));
		sec_video_button->show();
		attach(*sec_video_button,
		       column + column_labels, column + column_labels + 1,
		       row + row_sec_video_button,
		       row + row_sec_video_button + 1,
		       Gtk::FILL, Gtk::FILL,
		       0, 0);
		sec_btn_[i] = sec_video_button;

		Gtk::RadioButton * audio_button =
		    create_radio_button(audio_button_group_,
					audio_source_pixbuf_);
		audio_button->signal_pressed().connect(
		    sigc::bind(
			sigc::mem_fun(*this,
				      &dv_selector_widget::on_audio_selected),
			i));
		audio_button->show();
		attach(*audio_button,
		       column + column_labels, column + column_labels + 1,
		       row + row_audio_button, row + row_audio_button + 1,
		       Gtk::FILL, Gtk::FILL,
		       0, 0);
		snd_btn_[i] = audio_button;

		if (i < 9)
		{
		    // Make the mnemonic on the label work.  Also make
		    // the numeric keypad and Alt-keys work.
		    label->set_mnemonic_widget(*pri_video_button);
		    pri_video_button->add_accelerator("activate",
						  accel_group_,
						  GDK_KP_1 + i,
						  Gdk::ModifierType(0),
						  Gtk::AccelFlags(0));
		    pri_video_button->signal_activate().connect(
			sigc::bind(
			    sigc::mem_fun(
				*this,
				&dv_selector_widget::on_pri_video_selected),
			    i));
		    sec_video_button->add_accelerator("activate",
						      accel_group_,
						      '1' + i,
						      Gdk::SHIFT_MASK,
						      Gtk::AccelFlags(0));
		    sec_video_button->add_accelerator("activate",
						      accel_group_,
						      GDK_KP_1 + i,
						      Gdk::SHIFT_MASK,
						      Gtk::AccelFlags(0));
		    sec_video_button->signal_activate().connect(
			sigc::bind(
			    sigc::mem_fun(
				*this,
				&dv_selector_widget::on_sec_video_selected),
			    i));
		    audio_button->add_accelerator("activate",
						  accel_group_,
						  '1' + i,
						  Gdk::MOD1_MASK,
						  Gtk::AccelFlags(0));
		    audio_button->add_accelerator("activate",
						  accel_group_,
						  GDK_KP_1 + i,
						  Gdk::MOD1_MASK,
						  Gtk::AccelFlags(0));
		    audio_button->signal_activate().connect(
			sigc::bind(
			    sigc::mem_fun(
				*this,
				&dv_selector_widget::on_audio_selected),
			    i));
		}
	    }
	}
	catch (std::exception & e)
	{
	    // Roll back size changes
	    thumbnails_.resize(first_new_source_id);
	    std::cerr << "ERROR: Failed to add source display: " << e.what()
		      << "\n";
	}
    }
}

void dv_selector_widget::put_frame(mixer::source_id source_id,
				   const dv_frame_ptr & source_frame)
{
    if (source_id < thumbnails_.size())
	thumbnails_[source_id]->put_frame(source_frame);
}

void dv_selector_widget::select_pri(mixer::source_id id)
{
    if (id >= pri_btn_.size())
	return;
    pri_btn_[id]->set_active();
    on_pri_video_selected(id);
}

void dv_selector_widget::select_sec(mixer::source_id id)
{
    if (id >= sec_btn_.size())
	return;
    sec_btn_[id]->set_active();
    on_sec_video_selected(id);
}

void dv_selector_widget::select_snd(mixer::source_id id)
{
    if (id >= snd_btn_.size())
	return;
    snd_btn_[id]->set_active();
    on_audio_selected(id);
}

sigc::signal1<void, mixer::source_id> &
dv_selector_widget::signal_pri_video_selected()
{
    return pri_video_selected_signal_;
}

sigc::signal1<void, mixer::source_id> &
dv_selector_widget::signal_sec_video_selected()
{
    return sec_video_selected_signal_;
}

sigc::signal1<void, mixer::source_id> &
dv_selector_widget::signal_audio_selected()
{
    return audio_selected_signal_;
}

void dv_selector_widget::on_pri_video_selected(mixer::source_id source_id)
{
    pri_video_selected_signal_(source_id);
}

void dv_selector_widget::on_sec_video_selected(mixer::source_id source_id)
{
    sec_video_selected_signal_(source_id);
}

void dv_selector_widget::on_audio_selected(mixer::source_id source_id)
{
    audio_selected_signal_(source_id);
}

