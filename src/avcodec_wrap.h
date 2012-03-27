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

// These guards were removed from <avcodec.h>... what were they thinking?
#ifdef __cplusplus
extern "C" {
#endif

#include <avcodec.h>

#ifdef __cplusplus
}
#endif

#undef ABS

#ifndef AV_VERSION_INT
#define AV_VERSION_INT(a, b, c) (a<<16 | b<<8 | c)
#endif

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 26, 0)

#include <stdint.h>
#include <string.h>

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(52, 23, 0)
/* avcodec.h doesn't use AVPacket at all, so provide our own minimal
 * definition. */
typedef struct {
    uint8_t *data;
    int size;
} AVPacket;
#endif

static inline void av_init_packet(AVPacket *avpkt)
{
    memset(avpkt, 0, sizeof(*avpkt));
}

static inline int
avcodec_decode_video2(AVCodecContext *avctx, AVFrame *picture,
		      int *got_picture_ptr, AVPacket *avpkt)
{
    return avcodec_decode_video(avctx, picture, got_picture_ptr,
				avpkt->data, avpkt->size);
}

#endif /* < 52.26.0 */

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(53, 5, 0)
static inline AVCodecContext *
avcodec_alloc_context3(AVCodec *codec __attribute__((unused)))
{
    return avcodec_alloc_context();
}

static inline int
avcodec_open2(AVCodecContext *avctx, AVCodec *codec, void **options __attribute__((unused))) 
{
    return avcodec_open(avctx, codec);
}
#endif /* < 53.5.0 */

#endif /* AVCODEC_WRAP_H */
