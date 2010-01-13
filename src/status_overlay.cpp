// Copyright 2009 Ben Hutchings.
// See the file "COPYING" for licence details.

#include <cassert>

#include "gui.hpp"
#include "status_overlay.hpp"

const int status_scale = 64;
#define STATUS_TEXT_HEIGHT "48" // must be a string :-(

status_overlay::status_overlay()
    : main_widget_(0)
{
    // We do not need a window and do not implement realize()
    set_flags(Gtk::NO_WINDOW);

    status_widget_.set_parent(*this);

    Gdk::Color colour;
    colour.set_grey(0); // black
    status_widget_.modify_bg(Gtk::STATE_NORMAL, colour);
}

status_overlay::~status_overlay()
{
    status_widget_.unparent();
}

void status_overlay::set_status(const Glib::ustring & text,
				const Glib::ustring & icon_name,
				unsigned timeout)
{
    status_widget_.set_status(text, icon_name);
    status_widget_.show();

    // Cancel any timer for the previous status
    if (timer_)
    {
	// Glib::Source deletes itself when destroyed, despite the
	// fact that it's supposed to be reference-counted.
	// Therefore, dispose of the RefPtr before calling destroy.
	Glib::TimeoutSource * timer = timer_.operator->();
	timer_.reset();
	timer->destroy();
    }

    // Start new timer if necessary
    if (timeout)
    {
	timer_ = Glib::TimeoutSource::create(timeout * 1000);
	timer_->connect(sigc::mem_fun(this, &status_overlay::clear));
	timer_->attach(Glib::MainContext::get_default());
    }
}

bool status_overlay::clear()
{
    status_widget_.hide();
    timer_.reset();
    return false;
}

GType status_overlay::child_type_vfunc() const
{
    // If there is no main widget, any widget can be added.
    // If there is a min widget, no widgets can be added.
    return main_widget_ ? G_TYPE_NONE : GTK_TYPE_WIDGET;
}

void status_overlay::forall_vfunc(gboolean include_internals,
				  GtkCallback callback,
				  gpointer callback_data)
{
    if (main_widget_)
	callback(main_widget_->gobj(), callback_data);
    if (include_internals)
	callback(status_widget_.Widget::gobj(), callback_data);
}

void status_overlay::on_add(Gtk::Widget * widget)
{
    assert(!main_widget_ && widget);
    main_widget_ = widget;
    main_widget_->set_parent(*this);
}

void status_overlay::on_remove(Gtk::Widget * widget)
{
    assert(widget == main_widget_);
    main_widget_->unparent();
    main_widget_ = 0;
}

void status_overlay::on_size_allocate(Gtk::Allocation & allocation)
{
    if (main_widget_)
	main_widget_->size_allocate(allocation);

    status_widget_.size_allocate(
	Gtk::Allocation(allocation.get_x(),
			allocation.get_y() + allocation.get_height()
			- status_scale,
			allocation.get_width(), status_scale));
}

void status_overlay::on_size_request(Gtk::Requisition * requisition)
{
    if (main_widget_)
    {
	*requisition = main_widget_->size_request();
    }
    else
    {
	requisition->height = status_scale;
	requisition->width = status_scale * 5;
    }
}

void status_overlay::status_widget::set_status(const Glib::ustring & text,
					       const Glib::ustring & icon_name)
{
    text_ = text;
    try
    {
	icon_ = load_icon(icon_name, status_scale);
    }
    catch (Gtk::IconThemeError &)
    {
	icon_.reset();
    }

    queue_draw();
}

bool status_overlay::status_widget::on_expose_event(GdkEventExpose *) throw()
{
    Glib::RefPtr<Gdk::Drawable> drawable(get_window());

    if (Glib::RefPtr<Gdk::GC> gc = Gdk::GC::create(drawable))
    {
	if (icon_)
	    drawable->draw_pixbuf(gc, icon_, 0, 0, 0, 0,
				  -1, -1, Gdk::RGB_DITHER_NORMAL, 0, 0);

	if (!text_.empty())
	{
	    Glib::RefPtr<Pango::Context> pango = get_pango_context();
	    pango->set_font_description(
		Pango::FontDescription(
		    Glib::ustring("sans " STATUS_TEXT_HEIGHT "px")));

	    Gdk::Color colour;
	    colour.set_grey(65535); // white
	    gc->set_rgb_fg_color(colour);

	    Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create(pango);
	    layout->set_text(text_);
	    drawable->draw_layout(gc, status_scale, status_scale / 8, layout);
	}
    }

    return true;
}
