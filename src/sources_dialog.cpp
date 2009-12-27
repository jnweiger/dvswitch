// Copyright 2009 Ben Hutchings
// See the file "COPYING" for licence details

// The source management dialog

#include <gtkmm/messagedialog.h>

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

void sources_dialog::add_source()
{
    Gtk::Dialog dialog("Add Source", *this, /*modal=*/true);
    Gtk::Entry url_entry;
    url_entry.set_activates_default();
    url_entry.set_text("rtsp://");
    url_entry.show();
    dialog.get_vbox()->add(url_entry);
    dialog.add_button(Gtk::StockID("gtk-add"), 1);
    dialog.add_button(Gtk::StockID("gtk-cancel"), 0);

    if (dialog.run())
    {
	try
	{
	    connector_.add_source(url_entry.get_text());
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
