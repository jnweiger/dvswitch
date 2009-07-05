// Copyright 2009 Ben Hutchings.
// Copyright 2009 Carl Karsten.
// See the file "COPYING" for licence details.

// Gtkmm widget for displaying stereo VU-style volume meters

#include <limits>

#include <stdio.h> // for snprintf()

#include "vu_meter.hpp"

namespace
{
    Glib::ustring int_to_string(int n)
    {
	char buf[sizeof(n) * 8]; // should always be large enough
	snprintf(buf, sizeof(buf), "%d", n);
	return Glib::ustring(buf);
    }
}

vu_meter::vu_meter(int minimum, int maximum)
    : minimum_(minimum),
      maximum_(maximum)
{
    for (int channel = 0; channel != channel_count; ++channel)
    {
	levels_[channel] = std::numeric_limits<int>::min();
	peak_timers_[channel] = 0;
    }
    set_size_request(16, 32);
}

void vu_meter::set_levels(const int * levels)
{
    for (int channel = 0; channel != channel_count; ++channel)
    {
	levels_[channel] = levels[channel];

	if (peaks_[channel] <= levels_[channel] || peak_timers_[channel] == 0)
	{
	    peaks_[channel] = levels_[channel];

	    // TODO: let user specify the update frequency
	    peak_timers_[channel] = std::max(0, levels_[channel] / 2 + 25);
	}
	else
	{
	    --peak_timers_[channel];
	}
    }

    queue_draw();
}

bool vu_meter::on_expose_event(GdkEventExpose *) throw()
{
    int width = get_width(), height = get_height();
    Glib::RefPtr<Gdk::Drawable> drawable(get_window());

    if (Glib::RefPtr<Gdk::GC> gc = Gdk::GC::create(drawable))
    {
	// Draw segments of height 4 with 2 pixel (black) dividers.
	// The segments fade from green (min) to red (max).
	// Draw ticks at the minimum, maximum and 6 dB intervals,
	// lablled so far as possible without overlap.

	static const int border_thick = 2, tick_width = 6;
	static const int seg_height = 4, seg_vspacing = seg_height + border_thick;
	static const int tick_interval = 6;

	Glib::RefPtr<Pango::Context> pango = get_pango_context();
	Glib::RefPtr<Pango::Layout> layout = Pango::Layout::create(pango);

	int label_width, label_height;
	layout->set_text(int_to_string(minimum_));
	layout->get_pixel_size(label_width, label_height);
	layout->set_alignment(Pango::ALIGN_RIGHT);
	layout->set_width(label_width * Pango::SCALE);

	int scale_width = width - label_width - border_thick - tick_width;
	int scale_height = height - label_height;

	int seg_count = (scale_height - border_thick) / seg_vspacing;
	int seg_hspacing = (scale_width - border_thick) / channel_count;
	int seg_width = seg_hspacing - border_thick;

	if (seg_width <= 0 || height < label_height * 2)
	    return true; // cannot fit the scale in

	int label_interval =
	    tick_interval * std::max(1, (maximum_ - minimum_) / tick_interval
				     / (height / label_height - 1));

	drawable->draw_line(gc,
			    label_width + border_thick,
			    label_height / 2,
			    label_width + border_thick + tick_width,
			    label_height / 2);
	layout->set_text(int_to_string(maximum_));
	drawable->draw_layout(gc, 0, 0, layout);
	drawable->draw_line(gc,
			    label_width + border_thick,
			    label_height / 2 + scale_height - 1,
			    label_width + border_thick + tick_width,
			    label_height / 2 + scale_height - 1);
	layout->set_text(int_to_string(minimum_));
	drawable->draw_layout(gc, 0, height - label_height, layout);

	for (int value = minimum_ / tick_interval * tick_interval;
	     value < maximum_;
	     value += tick_interval)
	{
	    int y =
		(scale_height - 1) * (maximum_ - value) / (maximum_ - minimum_);
	    drawable->draw_line(gc,
				label_width + border_thick,
				y + label_height / 2,
				label_width + border_thick + tick_width,
				y + label_height / 2);
	    if (value % label_interval == 0
		&& y >= label_height && y <= height - label_height * 2)
	    {
		layout->set_text(int_to_string(value));
		drawable->draw_layout(gc, 0, y, layout);
	    }
	}

	Gdk::Color colour;
	colour.set_grey(0);
	gc->set_rgb_fg_color(colour);
	drawable->draw_rectangle(gc, true,
				 label_width + tick_width,
				 label_height / 2,
				 width - label_width - tick_width,
				 height - label_height);

	for (int channel = 0; channel != channel_count; ++channel)
	{
	    if (levels_[channel] >= minimum_)
	    {
		int seg_lit_count = 1 + (((seg_count - 1)
					  * (levels_[channel] - minimum_)
					  + ((maximum_ - minimum_) / 2))
					 / (maximum_ - minimum_));
		for (int seg = 0; seg < seg_lit_count; ++seg)
		{
		    colour.set_rgb(65535 * seg / seg_count,
				   65535 * (seg_count - seg) / seg_count,
				   0);
		    gc->set_rgb_fg_color(colour);
		    drawable->draw_rectangle(gc, true,
					     width - scale_width
					     + channel * seg_hspacing,
					     label_height / 2
					     + (seg_count - seg) * seg_vspacing
					     - seg_height,
					     seg_width, seg_height);
		}
	    }

	    if (peaks_[channel] >= minimum_)
	    {
		int seg = 1 + (((seg_count - 1)
				* (peaks_[channel] - minimum_)
				+ ((maximum_ - minimum_) / 2))
			       / (maximum_ - minimum_));
		colour.set_rgb(65535 * seg / seg_count,
			       65535 * (seg_count - seg) / seg_count,
			       0);
		gc->set_rgb_fg_color(colour);
		drawable->draw_rectangle(gc, true,
					 width - scale_width
					 + channel * seg_hspacing,
					 label_height / 2
					 + (seg_count - seg) * seg_vspacing
					 - seg_height,
					 seg_width, seg_height);
	    }
	}
    }

    return true;
}
