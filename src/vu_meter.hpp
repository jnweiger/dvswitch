// Copyright 2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Gtkmm widget for displaying stereo VU-style volume meters

#include <gtkmm/drawingarea.h>

class vu_meter : public Gtk::DrawingArea
{
public:
    vu_meter(int minimum, int maximum);

    static const int channel_count = 2;

    void set_levels(const int * levels);

private:
    virtual bool on_expose_event(GdkEventExpose *) throw();

    int minimum_, maximum_, levels_[channel_count];
};
