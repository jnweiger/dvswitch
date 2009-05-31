// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Video effects for raw video frames

#ifndef DVSWITCH_VIDEO_EFFECT_H
#define DVSWITCH_VIDEO_EFFECT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "frame.h"
#include "geometry.h"

void video_effect_show_title_safe(struct raw_frame_ref dest);
void video_effect_brighten(struct raw_frame_ref dest,
			   struct rectangle dest_rect);
void video_effect_pic_in_pic(struct raw_frame_ref dest,
			     struct rectangle dest_rect,
                             struct raw_frame_ref source,
			     struct rectangle source_rect);

#ifdef __cplusplus
}
#endif

#endif // !defined(DVSWITCH_VIDEO_EFFECT_H)
