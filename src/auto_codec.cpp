// Copyright 2008 Ben Hutchings <ben@decadent.org.uk>.
// See the file "COPYING" for licence details.

// RAII support for AVCodec (libavcodec context)

#include <cerrno>

#include <boost/thread/mutex.hpp>

#include "auto_codec.hpp"
#include "os_error.hpp"

namespace
{
    boost::mutex avcodec_mutex;

    struct avcodec_initialiser
    {
	avcodec_initialiser()
	{
	    avcodec_init();
	    avcodec_register_all();
	}
    } initialiser;
}

auto_codec auto_codec_open_decoder(CodecID codec_id)
{
    auto_codec result(avcodec_alloc_context());
    if (!result.get())
	throw std::bad_alloc();
    auto_codec_open_decoder(result, codec_id);
    return result;
}

void auto_codec_open_decoder(const auto_codec & context, CodecID codec_id)
{
    boost::mutex::scoped_lock lock(avcodec_mutex);
    AVCodec * codec = avcodec_find_decoder(codec_id);
    if (!codec)
	throw os_error("avcodec_find_decoder", ENOENT);
    os_check_error("avcodec_open", -avcodec_open(context.get(), codec));
}

auto_codec auto_codec_open_encoder(CodecID codec_id)
{
    auto_codec result(avcodec_alloc_context());
    if (!result.get())
	throw std::bad_alloc();
    auto_codec_open_encoder(result, codec_id);
    return result;
}

void auto_codec_open_encoder(const auto_codec & context, CodecID codec_id)
{
    boost::mutex::scoped_lock lock(avcodec_mutex);
    AVCodec * codec = avcodec_find_encoder(codec_id);
    if (!codec)
	throw os_error("avcodec_find_encoder", ENOENT);
    os_check_error("avcodec_open", -avcodec_open(context.get(), codec));
}

void auto_codec_closer::operator()(AVCodecContext * context) const
{
    if (context)
    {
	if (context->codec)
	{
	    boost::mutex::scoped_lock lock(avcodec_mutex);
	    avcodec_close(context);
	}
	av_free(context);
    }
}
