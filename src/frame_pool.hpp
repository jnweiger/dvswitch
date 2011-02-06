// Copyright 2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// DIF and raw video frame buffer pools

#ifndef DVSWITCH_FRAME_POOL_HPP
#define DVSWITCH_FRAME_POOL_HPP

#include <tr1/memory>

// Memory pool for frame buffers.  This should make frame
// (de)allocation relatively cheap.

struct dv_frame;
struct raw_frame;
struct pcm_packet;

// Reference-counting pointers to frames
typedef std::tr1::shared_ptr<dv_frame> dv_frame_ptr;
typedef std::tr1::shared_ptr<raw_frame> raw_frame_ptr;
typedef std::tr1::shared_ptr<pcm_packet> pcm_packet_ptr;

// Allocate a DV frame buffer
dv_frame_ptr allocate_dv_frame();

// Allocate a raw frame buffer
raw_frame_ptr allocate_raw_frame();

// Allocate a PCM pcket buffer
pcm_packet_ptr allocate_pcm_packet();

#endif // !DVSWITCH_FRAME_POOL_HPP
