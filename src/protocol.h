// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

#ifndef DVSWITCH_PROTOCOL_H
#define DVSWITCH_PROTOCOL_H

// Client initially sends a greeting of this length identifying which
// protocol it's going to speak.
#define GREETING_SIZE 4

// Source which sends a raw DIF stream.
#define GREETING_SOURCE "SORC"
// Sink which receives a raw DIF stream.
#define GREETING_RAW_SINK "RSNK"
// Sink which receives a header before each DIF frame.
#define GREETING_SINK "SINK"
// As above, but receives only frames to be recorded.
#define GREETING_REC_SINK "SNKR"

// Length of the frame header.
#define SINK_FRAME_HEADER_SIZE 4

// Position of the "cut" flag byte in the header.  All non-zero values
// indicate a cut immediately before the following frame; the following
// specific values are defined.
#define SINK_FRAME_CUT_FLAG_POS 0
// Cut was initiated by user.
#define SINK_FRAME_CUT_CUT 'C'
// Cut is due to overflow of the mixer's buffer for the sink.
#define SINK_FRAME_CUT_OVERFLOW 'O'
// Recording sink only: cut is the end of recording.  There is no
// immediately following frame.
#define SINK_FRAME_CUT_STOP 'S'

// The remaining bytes of the frame header are reserved and should be 0.

#endif // !defined(DVSWITCH_PROTOCOL_H)
