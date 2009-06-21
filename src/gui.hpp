// GUI constants

#ifndef DVSWITCH_GUI_HPP
#define DVSWITCH_GUI_HPP

#include <gdkmm/pixbuf.h>
#include <glibmm/refptr.h>
#include <gtkmm/icontheme.h>

// GNOME HIG: "As a basic rule of thumb, leave space between user
// interface components in increments of 6 pixels, going up as the
// relationship between related elements becomes more distant."
const unsigned gui_standard_spacing = 6;

inline Glib::RefPtr<Gdk::Pixbuf> load_icon(const Glib::ustring & name, int size)
{
    return Gtk::IconTheme::get_default()->
	load_icon(name, size, Gtk::IconLookupFlags(0));
}

#endif
