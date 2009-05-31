// Copyright 2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF and raw video frame buffer pools

#ifndef DVSWITCH_FRAME_POOL_HPP
#define DVSWITCH_FRAME_POOL_HPP

#include <tr1/memory>

// Memory pool for frame buffers.  This should make frame
// (de)allocation relatively cheap.

class dv_frame;
class raw_frame;

// Reference-counting pointers to frames
typedef std::tr1::shared_ptr<dv_frame> dv_frame_ptr;
typedef std::tr1::shared_ptr<raw_frame> raw_frame_ptr;

// Allocate a DV frame buffer
dv_frame_ptr allocate_dv_frame();

// Allocate a raw frame buffer
raw_frame_ptr allocate_raw_frame();

#endif // !DVSWITCH_FRAME_POOL_HPP
