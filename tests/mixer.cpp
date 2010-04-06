// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

#include <cstdlib>
#include <iostream>
#include <ostream>

#include "frame.h"
#include "frame_pool.hpp"
#include "mixer.hpp"

// The use of volatile in this test program is not an endorsement of its
// use in production multithreaded code.  It probably works here, but I
// wouldn't want to depend on it.

namespace
{
    class dummy_source : public mixer::source
    {
    public:
	dummy_source() {}
    private:
	virtual void set_active(mixer::source_activation flags)
	{
	    std::cout << "video source "
		      << ((flags & mixer::source_active_video) ? "" : "de")
		      << "activated\n";
	}
    };

    class dummy_sink : public mixer::sink
    {
    public:
	dummy_sink(volatile unsigned & sink_count)
	    : sink_count_(sink_count)
	{}
    private:
	virtual void put_frame(const dv_frame_ptr &)
	{
	    std::cout << "sinked frame\n";
	    ++sink_count_;
	}
	virtual void cut()
	{
	    std::cout << "sinked cut\n";
	}
	volatile unsigned & sink_count_;
    };
}

int main()
{
    volatile unsigned sink_count = 0;
    unsigned source_count = 0;
    mixer the_mixer;
    mixer::source_settings dummy_settings;
    the_mixer.add_source(new dummy_source, dummy_settings);
    the_mixer.add_sink(new dummy_sink(sink_count), false);
    for (;;)
    {
	if (source_count - sink_count < 8)
	{
	    dv_frame_ptr frame(allocate_dv_frame());
	    frame->buffer[3] = 0xBF; // 625/50 frame
	    the_mixer.put_frame(0, frame);
	    ++source_count;
	    std::cout << "sourced frame\n";
	    if ((std::rand() & 0x1F) == 0)
	    {
		the_mixer.cut();
		std::cout << "cut\n";
	    }
	}
	usleep(10000);
    }
}
