// Copyright 2009 Ben Hutchings
// See the file "COPYING" for licence details

// The source management dialog

#ifndef DVSWITCH_SOURCES_DIALOG_HPP
#define DVSWITCH_SOURCES_DIALOG_HPP

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/treeview.h>

class connector;
class mixer;

class sources_dialog : public Gtk::Dialog
{
public:
    sources_dialog(Gtk::Window & owner,
		   mixer & mixer, connector & connector);

private:
    void add_source();

    mixer & mixer_;
    connector & connector_;

    Gtk::HBox upper_box_;
    Gtk::TreeView sources_view_;
    Gtk::VBox command_box_;
    Gtk::Button add_button_;
    Gtk::Button edit_button_;
    Gtk::Button remove_button_;
};

#endif // DVSWITCH_SOURCES_DIALOG_HPP
