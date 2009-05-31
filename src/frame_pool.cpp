// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF and raw video frame buffer pools

#include <boost/pool/object_pool.hpp>
#include <boost/thread/mutex.hpp>

#include "avcodec_wrap.h"

#include "frame.h"
#include "frame_pool.hpp"

namespace
{
    boost::mutex dv_frame_pool_mutex; // controls access to the following
    boost::object_pool<dv_frame> dv_frame_pool(100);

    void free_dv_frame(dv_frame * frame)
    {
	boost::mutex::scoped_lock lock(dv_frame_pool_mutex);
	if (frame)
	    dv_frame_pool.free(frame);
    }

    boost::mutex raw_frame_pool_mutex; // controls access to the following
    boost::object_pool<raw_frame> raw_frame_pool(10);

    void free_raw_frame(raw_frame * frame)
    {
	boost::mutex::scoped_lock lock(raw_frame_pool_mutex);
	if (frame)
	    raw_frame_pool.free(frame);
    }
}

dv_frame_ptr allocate_dv_frame()
{
    boost::mutex::scoped_lock lock(dv_frame_pool_mutex);
    return dv_frame_ptr(dv_frame_pool.malloc(), free_dv_frame);
}

raw_frame_ptr allocate_raw_frame()
{
    boost::mutex::scoped_lock lock(raw_frame_pool_mutex);
    return raw_frame_ptr(raw_frame_pool.malloc(), free_raw_frame);
}
