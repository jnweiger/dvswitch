// Copyright 2009 Ben Hutchings
// See the file "COPYING" for licence details

// The source management dialog

#include <gtkmm/messagedialog.h>
#include <gtkmm/table.h>

#include "sources_dialog.hpp"
#include "connector.hpp"
#include "gui.hpp"
#include "mixer.hpp"

sources_dialog::sources_dialog(Gtk::Window & owner,
			       mixer & mixer, connector & connector)
    : Gtk::Dialog("Sources", owner),
      mixer_(mixer),
      connector_(connector),
      add_button_("gtk-add"),
      edit_button_("gtk-edit"),
      remove_button_("gtk-remove")
{
    // TODO: populate sources view
    sources_view_.show();

    add_button_.set_use_stock();
    add_button_.signal_clicked().connect(
	sigc::mem_fun(*this, &sources_dialog::add_source));
    add_button_.show();

    edit_button_.set_use_stock();
    edit_button_.show();

    remove_button_.set_use_stock();
    remove_button_.show();

    command_box_.set_spacing(gui_standard_spacing);
    command_box_.pack_start(add_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(edit_button_, Gtk::PACK_SHRINK);
    command_box_.pack_start(remove_button_, Gtk::PACK_SHRINK);
    command_box_.show();

    upper_box_.set_border_width(gui_standard_spacing);
    upper_box_.set_spacing(gui_standard_spacing);
    upper_box_.pack_start(sources_view_, Gtk::PACK_EXPAND_WIDGET);
    upper_box_.pack_start(command_box_, Gtk::PACK_SHRINK);
    upper_box_.show();

    Gtk::VBox * main_box = get_vbox();
    main_box->pack_start(upper_box_, Gtk::PACK_EXPAND_WIDGET);

    add_button(Gtk::StockID("gtk-close"), 0);
}

class source_add_dialog : public Gtk::Dialog
{
public:
    explicit source_add_dialog(Gtk::Window &);
    mixer::source_settings get_settings() const;

private:
    void handle_change();
    bool is_valid() const;

    Gtk::Table table_;
    Gtk::Label name_label_;
    Gtk::Entry name_entry_;
    Gtk::Label url_label_;
    Gtk::Entry url_entry_;
    Gtk::CheckButton video_button_;
    Gtk::CheckButton audio_button_;
};

source_add_dialog::source_add_dialog(Gtk::Window & window)
    : Dialog("Add Source", window, /*modal=*/true),
      name_label_("Name"),
      url_label_("URL"),
      video_button_("Use video"),
      audio_button_("Use audio")
{
    Gtk::VBox & vbox = *get_vbox();
    vbox.set_border_width(gui_standard_spacing);
    vbox.set_spacing(gui_standard_spacing);

    table_.set_col_spacings(gui_standard_spacing);			   
    table_.set_row_spacings(gui_standard_spacing);			   

    name_label_.show();
    table_.attach(name_label_, 0, 1, 0, 1, Gtk::FILL, Gtk::FILL);

    name_entry_.show();
    table_.attach(name_entry_, 1, 2, 0, 1, Gtk::FILL | Gtk::EXPAND, Gtk::FILL);

    url_label_.show();
    table_.attach(url_label_, 0, 1, 1, 2, Gtk::FILL, Gtk::FILL);

    url_entry_.set_text("rtsp://");
    url_entry_.show();
    table_.attach(url_entry_, 1, 2, 1, 2, Gtk::FILL | Gtk::EXPAND, Gtk::FILL);

    table_.show();
    vbox.add(table_);

    video_button_.set_active(true);
    video_button_.show();
    vbox.add(video_button_);

    audio_button_.set_active(true);
    audio_button_.show();
    vbox.add(audio_button_);

    name_entry_.signal_changed().connect(
	sigc::mem_fun(*this, &source_add_dialog::handle_change));
    url_entry_.signal_changed().connect(
	sigc::mem_fun(*this, &source_add_dialog::handle_change));
    video_button_.signal_toggled().connect(
	sigc::mem_fun(*this, &source_add_dialog::handle_change));
    audio_button_.signal_toggled().connect(
	sigc::mem_fun(*this, &source_add_dialog::handle_change));

    add_button(Gtk::StockID("gtk-add"), 1);
    set_response_sensitive(1 /*add*/, false);
    add_button(Gtk::StockID("gtk-cancel"), 0);
}

void source_add_dialog::handle_change()
{
    set_response_sensitive(1 /*add*/, is_valid());
}

bool source_add_dialog::is_valid() const
{
    // Name and URL must be set; one or both of video or audio must be
    // enabled
    return (name_entry_.get_text_length() != 0 &&
	    url_entry_.get_text_length() != 0 &&
	    (video_button_.get_active() ||
	     audio_button_.get_active()));
}

mixer::source_settings source_add_dialog::get_settings() const
{
    mixer::source_settings result;
    result.name = name_entry_.get_text();
    result.url = url_entry_.get_text();
    result.use_video = video_button_.get_active();
    result.use_audio = audio_button_.get_active();
    return result;
}

void sources_dialog::add_source()
{
    source_add_dialog dialog(*this);

    if (dialog.run())
    {
	try
	{
	    connector_.add_source(dialog.get_settings());
	}
	catch(std::exception & e)
	{
	    Gtk::MessageDialog(*this, e.what(), /*use_markup=*/false,
			       Gtk::MESSAGE_ERROR, Gtk::BUTTONS_CANCEL,
			       /*modal=*/true)
		.run();
	}
    }
}
