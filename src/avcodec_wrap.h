// Copyright 2009-2010 Ben Hutchings.
// See the file "COPYING" for licence details.

#ifndef AVCODEC_WRAP_H
#define AVCODEC_WRAP_H

// Some versions of ffmpeg define an ABS macro, which glib also does.
// The two definitions are equivalent but the duplicate definitions
// provoke a warning.
#undef ABS

// <avcodec.h> may need UINT64_C while <stdint.h> may or may not
// define that for C++.  Therefore, include <stdint.h> here and then
// define UINT64_C if it didn't get defined.
#include <stdint.h>
#ifndef UINT64_C
#define UINT64_C(n) n ## ULL
#endif

#include "config.h"

// These guards were removed from <avcodec.h>... what were they thinking?
#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>

#ifdef __cplusplus
}
#endif

#undef ABS

#ifndef AV_VERSION_INT
#define AV_VERSION_INT(a, b, c) (a<<16 | b<<8 | c)
#endif

#ifndef AVC_HAVE_FRAME_ALLOC
#define av_frame_alloc()	(AVFrame*)calloc(sizeof(AVFrame), 1)
#define av_frame_dealloc(f)	free(f);
#else
/* The actual API uses a refcounter, so there is no such thing as a dealloc.
 * However, we need it to simulate the API as above.
 *
 * Yes, this is ugly.
 */
#define av_frame_dealloc(f)
#endif /* AVC_HAVE_FRAME_ALLOC */

#endif /* AVCODEC_WRAP_H */
