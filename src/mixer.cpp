// Copyright 2007-2009 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// Copyright 2009 Wouter Verhelst.
// See the file "COPYING" for licence details.

// The mixer.  This holds the current mixing settings and small
// buffers for each source.  It maintains a frame clock, selects and
// mixes frames at each clock tick, and passes frames in the sinks and
// monitor.

#include <cstddef>
#include <cstring>
#include <iostream>
#include <ostream>
#include <stdexcept>

#include <limits.h>
#include <unistd.h>

#include <boost/bind.hpp>
#include <boost/thread/thread.hpp>

#include "auto_codec.hpp"
#include "frame.h"
#include "frame_timer.h"
#include "mixer.hpp"
#include "os_error.hpp"
#include "ring_buffer.hpp"
#include "video_effect.h"

// Video mix settings abstract base class
struct mixer::video_mix
{
    virtual void validate(const mixer &) = 0;
    virtual void set_active(const mixer &, bool active) = 0;
    virtual bool apply(const mix_data &, const auto_codec &,
		       raw_frame_ptr &, dv_frame_ptr &) = 0;
    virtual void status(mixer::monitor * monitor) = 0;
};

mixer::mixer()
    : clock_state_(run_state_wait),
      clock_thread_(boost::bind(&mixer::run_clock, this)),
      mixer_queue_(10),
      mixer_state_(run_state_wait),
      mixer_thread_(boost::bind(&mixer::run_mixer, this)),
      recorders_count_(0),
      monitor_(0)
{
    format_.system = NULL;
    format_.frame_aspect = dv_frame_aspect_auto;
    format_.sample_rate = dv_sample_rate_auto;
    settings_.video_mix = create_video_mix_simple(0);
    settings_.audio_source_id = 0;
    settings_.do_record = false;
    settings_.cut_before = false;
    sources_.reserve(5);
    sinks_.reserve(5);
}

mixer::~mixer()
{
    {
	boost::mutex::scoped_lock lock(source_mutex_);
	clock_state_ = run_state_stop;
	clock_state_cond_.notify_one(); // in case it's still waiting
    }
    {
	boost::mutex::scoped_lock lock(mixer_mutex_);
	mixer_state_ = run_state_stop;
	mixer_state_cond_.notify_one();
    }

    clock_thread_.join();
    mixer_thread_.join();
}

mixer::source_id mixer::add_source(source * src, const source_settings &)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    source_id id;
    for (id = 0; id != sources_.size(); ++id)
    {
	if (!sources_[id].src)
	{
	    sources_[id].src = src;
	    return id;
	}
    }
    sources_.resize(id + 1);
    sources_[id].src = src;
    return id;
}

void mixer::remove_source(source_id id)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    sources_.at(id).src = NULL;
}

void mixer::put_frame(source_id id, const dv_frame_ptr & frame)
{
    bool was_full;
    bool should_notify_clock = false;

    {
	boost::mutex::scoped_lock lock(source_mutex_);

	source_data & source = sources_.at(id);
	was_full = source.frames.full();

	if (!was_full)
	{
	    frame->timestamp = frame_timer_get();
	    source.frames.push(frame);

	    // Start clock ticking once first source has reached the
	    // target queue length
	    if (clock_state_ == run_state_wait
		&& id == 0 && source.frames.size() == target_queue_len)
	    {
		clock_state_ = run_state_run;
		should_notify_clock = true; // after we unlock the mutex
	    }

	    // Auto-select format
	    if (clock_state_ == run_state_run)
	    {
		format_settings format;
		format.system = dv_frame_system(frame.get());
		format.frame_aspect = dv_frame_get_aspect(frame.get());
		format.sample_rate = dv_frame_get_sample_rate(frame.get());

		frame->format_error = false;

		if (format_.system == NULL)
		    format_.system = format.system;
		else if (format_.system != format.system)
		{
		    std::cerr << "WARN: Source " << 1 + id
			      << " using wrong video system\n";
		    frame->format_error = true;
		}

		if (format_.frame_aspect == dv_frame_aspect_auto)
		    format_.frame_aspect = format.frame_aspect;
		else if (format_.frame_aspect != format.frame_aspect)
		    // Override frame aspect ratio
		    dv_frame_set_aspect(frame.get(), format_.frame_aspect);

		if (format_.sample_rate == dv_sample_rate_auto &&
		    format.sample_rate >= 0)
		{
		    format_.sample_rate = format.sample_rate;
		}
		else if (format_.sample_rate != format.sample_rate)
		{
		    std::cerr << "WARN: Source " << 1 + id 
			      << " (" << format_.sample_rate << " vs " << format.sample_rate << " ) "
			      << "using wrong sample rate\n";

		    frame->format_error = true;
		}
	    }
	}
    }

    if (should_notify_clock)
	clock_state_cond_.notify_one();

    if (was_full)
	std::cerr << "WARN: Dropped frame from source " << 1 + id
		  << " due to full queue\n";
}

mixer::sink_id mixer::add_sink(sink * sink, bool will_record)
{
    boost::mutex::scoped_lock lock(sink_mutex_);
    // XXX We may want to be able to reuse sink slots.
    sinks_.push_back(sink);
    if (will_record)
	++recorders_count_;
    return sinks_.size() - 1;
}

void mixer::remove_sink(sink_id id, bool will_record)
{
    boost::mutex::scoped_lock lock(sink_mutex_);
    if (will_record)
    {
	assert(recorders_count_ != 0);
	--recorders_count_;
    }
    sinks_.at(id) = 0;
}

mixer::format_settings mixer::get_format() const
{
    boost::mutex::scoped_lock lock(source_mutex_);
    mixer::format_settings result = format_;
    lock.unlock();
    return result;
}

void mixer::set_format(format_settings format)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    format_ = format;
}

void mixer::set_audio_source(source_id id)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    if (id < sources_.size())
	settings_.audio_source_id = id;
    else
	throw std::range_error("audio source id out of range");
}

void mixer::set_monitor(monitor * monitor)
{
    assert(monitor && !monitor_);
    monitor_ = monitor;
}

void mixer::enable_record(bool flag)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    settings_.do_record = flag;
}

void mixer::cut()
{
    boost::mutex::scoped_lock lock(source_mutex_);
    settings_.cut_before = true;
}

bool mixer::can_record() const
{
    // Not locking: an incorrect result isn't a big issue here, but speed is
    return recorders_count_ != 0;
}

namespace
{
    // Ensure the frame timer is initialised at startup
    struct timer_initialiser { timer_initialiser(); } timer_dummy;
    timer_initialiser::timer_initialiser()
    {
	frame_timer_init();
    }
}

void mixer::run_clock()
{
    const struct dv_system * audio_source_system = 0;

    {
	boost::mutex::scoped_lock lock(source_mutex_);
	while (clock_state_ == run_state_wait)
	    clock_state_cond_.wait(lock);
	if (clock_state_ == run_state_run)
	    settings_.video_mix->set_active(*this, true);
    }

    // Interval to the next frame (in ns)
    unsigned int frame_interval = 0;
    // Weighted rolling average frame interval
    unsigned int average_frame_interval = 0;

    for (uint64_t tick_timestamp = frame_timer_get();
	 ;
	 tick_timestamp += frame_interval, frame_timer_wait(tick_timestamp))
    {
	mix_data m;

	// Select the mixer settings and source frame(s)
	{
	    boost::mutex::scoped_lock lock(source_mutex_);

	    if (clock_state_ == run_state_stop)
		break;

	    m.format = format_;
	    m.settings = settings_;
	    settings_.cut_before = false;

	    m.source_frames.resize(sources_.size());
	    for (source_id id = 0; id != sources_.size(); ++id)
	    {
		if (sources_[id].frames.empty())
		{
		    m.source_frames[id].reset();
		}
		else
		{
		    m.source_frames[id] = sources_[id].frames.front();
		    sources_[id].frames.pop();
		}
	    }
	}

	assert(m.settings.audio_source_id < m.source_frames.size());

	// Frame timer is based on the audio source.  Synchronisation
	// with the audio source matters more because audio
	// discontinuities are even more annoying than dropped or
	// repeated video frames.
	if (dv_frame * audio_source_frame =
	    m.source_frames[m.settings.audio_source_id].get())
	{
	    if (audio_source_system != dv_frame_system(audio_source_frame))
	    {
		audio_source_system = dv_frame_system(audio_source_frame);

		// Use standard frame timing initially.
		frame_interval = (1000000000
				  / audio_source_system->frame_rate_numer
				  * audio_source_system->frame_rate_denom);
		average_frame_interval = frame_interval;
	    }
	    else
	    {
		// The delay for this frame has a large effect on the
		// interval to the next frame because we want to
		// correct clock deviations quickly, but a much
		// smaller effect on the rolling average so that we
		// don't over-correct.  This has experimentally been
		// found to work well.
		static const unsigned next_average_weight = 3;
		static const unsigned next_delay_weight = 1;
		static const unsigned average_rolling_weight = 15;
		static const unsigned average_next_weight = 1;

		// Try to keep target_queue_len - 0.5 frame intervals
		// between delivery of source frames and mixing them.
		// The "obvious" way to feed the delay into the
		// frame_time is to divide it by target_queue_len-0.5.
		// But this is inverse to the effect we want it to
		// have: if the delay is long, we need to reduce,
		// not increase, frame_time.  So we calculate a kind
		// of inverse based on the amount of queue space
		// that should remain free.
		const uint64_t delay =
		    tick_timestamp > audio_source_frame->timestamp
		    ? tick_timestamp - audio_source_frame->timestamp
		    : 0;
		const unsigned free_queue_time =
		    full_queue_len * frame_interval > delay
		    ? full_queue_len * frame_interval - delay
		    : 0;
		frame_interval =
		    (average_frame_interval * next_average_weight
		     + (free_queue_time
			* 2 / (2 * (full_queue_len - target_queue_len) + 1)
			* next_delay_weight))
		    / (next_average_weight + next_delay_weight);

		average_frame_interval =
		    (average_frame_interval * average_rolling_weight
		     + frame_interval * average_next_weight)
		    / (average_rolling_weight + average_next_weight);
	    }
	}

	std::size_t free_len;

	{
	    boost::mutex::scoped_lock lock(mixer_mutex_);
	    free_len = mixer_queue_.capacity() - mixer_queue_.size();
	    if (free_len != 0)
	    {
		mixer_queue_.push(m); // really want to move m here
		mixer_state_ = run_state_run;
	    }
	}

	if (free_len != 0)
	{
	    mixer_state_cond_.notify_one();
	}
	else
	{
	    std::cerr << "ERROR: Dropped source frames due to"
		" full mixer queue\n";
	}
    }
}

namespace
{
    raw_frame_ref make_raw_frame_ref(const raw_frame_ptr & frame)
    {
	struct raw_frame_ref result;
	for (int i = 0; i != 4; ++i)
	{
	    result.planes.data[i] = frame->header.data[i];
	    result.planes.linesize[i] = frame->header.linesize[i];
	}
	result.pix_fmt = frame->pix_fmt;
	result.height = raw_frame_system(frame.get())->frame_height;
	return result;
    }

    raw_frame_ptr decode_video_frame(
 	const auto_codec & decoder, const dv_frame_ptr & dv_frame)
    {
	const struct dv_system * system = dv_frame_system(dv_frame.get());
	raw_frame_ptr result = allocate_raw_frame();

	AVPacket packet;
	av_init_packet(&packet);
	packet.data = dv_frame->buffer;
	packet.size = system->size;

	int got_frame;
	decoder.get()->opaque = result.get();
	int used_size = avcodec_decode_video2(decoder.get(),
					      &result->header, &got_frame,
					      &packet);
	assert(got_frame && size_t(used_size) == system->size);

	result->header.opaque =
	    const_cast<void *>(static_cast<const void *>(system));
	result->aspect = dv_frame_get_aspect(dv_frame.get());
	return result;
    }

    inline unsigned bcd(unsigned v)
    {
	assert(v < 100);
	return ((v / 10) << 4) + v % 10;
    }

    void set_times(dv_frame & dv_frame)
    {
	// XXX We should work this out in the clock loop.
	time_t now;
	time(&now);
	tm now_tm;
	localtime_r(&now, &now_tm);

	// Generate nominal frame count and frame rate.
	unsigned frame_num = dv_frame.serial_num;
	unsigned frame_rate;
	if (dv_frame.buffer[3] & 0x80)
	{
	    frame_rate = 25;
	}
	else
	{
	    // Skip the first 2 frame numbers of each minute, except in
	    // minutes divisible by 10.  This results in a "drop frame
	    // timecode" with a nominal frame rate of 30 Hz.
	    frame_num = frame_num + 2 * frame_num / (60 * 30 - 2)
		- 2 * (frame_num + 2) / (10 * 60 * 30 - 18);
	    frame_rate = 30;
	}

	// Timecode format is based on SMPTE LTC
	// <http://en.wikipedia.org/wiki/Linear_timecode>:
	// 0: pack id = 0x13
	// 1: LTC bits 0-3, 8-11
	//    bits 0-5: frame part (BCD)
	//    bit 6: drop frame timecode flag
	// 2: LTC bits 16-19, 24-27
	//    bits 0-6: second part (BCD)
	// 3: LTC bits 32-35, 40-43
	//    bits 0-6: minute part (BCD)
	// 4: LTC bits 48-51, 56-59
	//    bits 0-5: hour part (BCD)
	// the remaining bits are meaningless in DV and we use zeroes
	uint8_t timecode[DIF_PACK_SIZE] = {
	    0x13,
	    (uint8_t)bcd(frame_num % frame_rate) | (1 << 6),
	    (uint8_t)bcd(frame_num / frame_rate % 60),
	    (uint8_t)bcd(frame_num / (60 * frame_rate) % 60),
	    (uint8_t)bcd(frame_num / (60 * 60 * frame_rate) % 24)
	};

	// Record date format:
	// 0: pack id = 0x62 (video) or 0x52 (audio)
	// 1: some kind of time zone indicator or 0xff for unknown
	// 2: bits 6-7: unused? reserved?
	//    bits 0-5: day part (BCD)
	// 3: bits 5-7: unused? reserved? day of week?
	//    bits 0-4: month part (BCD)
	// 4: year part (BCD)
	uint8_t video_record_date[DIF_PACK_SIZE] = {
	    0x62,
	    0xff,
	    (uint8_t)bcd(now_tm.tm_mday),
	    (uint8_t)bcd(1 + now_tm.tm_mon),
	    (uint8_t)bcd(now_tm.tm_year % 100)
	};
	uint8_t audio_record_date[DIF_PACK_SIZE] = {
	    0x52,
	    0xff,
	    (uint8_t)bcd(now_tm.tm_mday),
	    (uint8_t)bcd(1 + now_tm.tm_mon),
	    (uint8_t)bcd(now_tm.tm_year % 100)
	};

	// Record time format (similar to timecode format):
	// 0: pack id = 0x63 (video) or 0x53 (audio)
	// 1: bits 6-7: reserved, set to 1
	//    bits 0-5: frame part (BCD) or 0x3f for unknown
	// 2: bit 7: unused? reserved?
	//    bits 0-6: second part (BCD)
	// 3: bit 7: unused? reserved?
	//    bits 0-6: minute part (BCD)
	// 4: bits 6-7: unused? reserved?
	//    bits 0-5: hour part (BCD)
	uint8_t video_record_time[DIF_PACK_SIZE] = {
	    0x63,
	    0xff,
	    (uint8_t)bcd(now_tm.tm_sec),
	    (uint8_t)bcd(now_tm.tm_min),
	    (uint8_t)bcd(now_tm.tm_hour)
	};
	uint8_t audio_record_time[DIF_PACK_SIZE] = {
	    0x53,
	    0xff,
	    (uint8_t)bcd(now_tm.tm_sec),
	    (uint8_t)bcd(now_tm.tm_min),
	    (uint8_t)bcd(now_tm.tm_hour)
	};

        // In DIFs 1 and 2 (subcode) of sequence 6 onward:
	// - Write timecode at offset 6 and 30
	// - Write video record date at offset 14 and 38
	// - Write video record time at offset 22 and 46
	// In DIFs 3, 4 and 5 (VAUX) of even sequences:
	// - Write video record date at offset 13 and 58
	// - Write video record time at offset 18 and 63
	// In DIF 86 of even sequences and DIF 38 of odd sequences (AAUX):
	// - Write audio record date at offset 3
	// In DIF 102 of even sequences and DIF 54 of odd sequences (AAUX):
	// - Write audio record time at offset 3

	for (unsigned seq_num = 0;
	     seq_num != dv_frame_system(&dv_frame)->seq_count;
	     ++seq_num)
	{
	    if (seq_num >= 6)
	    {
		for (unsigned block_num = 1; block_num <= 3; ++block_num)
		{
		    for (unsigned i = 0; i <= 1; ++i)
		    {
			memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
			       + block_num * DIF_BLOCK_SIZE + i * 24 + 6,
			       timecode,
			       DIF_PACK_SIZE);
			memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
			       + block_num * DIF_BLOCK_SIZE + i * 24 + 14,
			       video_record_date,
			       DIF_PACK_SIZE);
			memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
			       + block_num * DIF_BLOCK_SIZE + i * 24 + 22,
			       video_record_time,
			       DIF_PACK_SIZE);
		    }
		}
	    }

	    for (unsigned block_num = 3; block_num <= 5; ++block_num)
	    {
		for (unsigned i = 0; i <= 1; ++i)
		{
		    memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
			   + block_num * DIF_BLOCK_SIZE + i * 45 + 13,
			   video_record_date,
			   DIF_PACK_SIZE);
		    memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
			   + block_num * DIF_BLOCK_SIZE + i * 45 + 18,
			   video_record_time,
			   DIF_PACK_SIZE);
		}
	    }

	    memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
		   + ((seq_num & 1) ? 38 : 86) * DIF_BLOCK_SIZE + 3,
		   audio_record_date,
		   DIF_PACK_SIZE);
	    memcpy(dv_frame.buffer + seq_num * DIF_SEQUENCE_SIZE
		   + ((seq_num & 1) ? 54 : 102) * DIF_BLOCK_SIZE + 3,
		   audio_record_time,
		   DIF_PACK_SIZE);
	}
    }
}

void mixer::set_video_mix(std::tr1::shared_ptr<video_mix> video_mix)
{
    boost::mutex::scoped_lock lock(source_mutex_);
    video_mix->validate(*this);
    settings_.video_mix->set_active(*this, false);
    settings_.video_mix = video_mix;
    settings_.video_mix->set_active(*this, true);
}

// Simple video mix - selects a single source

class mixer::video_mix_simple : public video_mix
{
public:
    explicit video_mix_simple(source_id id)
	: source_id_(id)
    {}
private:
    virtual void validate(const mixer &);
    virtual void set_active(const mixer &, bool active);
    virtual bool apply(const mix_data &, const auto_codec &,
		       raw_frame_ptr &, dv_frame_ptr &);
    virtual void status(mixer::monitor *) {}
    source_id source_id_;
};

void mixer::video_mix_simple::validate(const mixer & mixer)
{
    if (source_id_ >= mixer.sources_.size())
	throw std::range_error("video source id out of range");
}

void mixer::video_mix_simple::set_active(const mixer & mixer, bool active)
{
    if (mixer.sources_[source_id_].src)
	mixer.sources_[source_id_].src->set_active(
	    active ? source_active_video : source_active_none);
}

bool mixer::video_mix_simple::apply(const mix_data & m, const auto_codec &,
				    raw_frame_ptr &, dv_frame_ptr & mixed_dv)
{
    const dv_frame_ptr & source_dv = m.source_frames[source_id_];

    if (source_dv && dv_frame_system(source_dv.get()) == m.format.system)
	mixed_dv = source_dv;
    return false;
}

// Picture-in-picture video mix - replaces some region of primary source with
// a secondary source scaled to fit

class mixer::video_mix_pic_in_pic : public video_mix
{
public:
    video_mix_pic_in_pic(source_id pri_source_id,
			 source_id sec_source_id, rectangle dest_region)
	: pri_source_id_(pri_source_id),
	  sec_source_id_(sec_source_id),
	  dest_region_(dest_region)
    {}
private:
    virtual void validate(const mixer &);
    virtual void set_active(const mixer &, bool active);
    virtual bool apply(const mix_data &, const auto_codec &,
		       raw_frame_ptr &, dv_frame_ptr &);
    virtual void status(mixer::monitor *) {}
    source_id pri_source_id_, sec_source_id_;
    rectangle dest_region_;
};

void mixer::video_mix_pic_in_pic::validate(const mixer & mixer)
{
    if (pri_source_id_ >= mixer.sources_.size() ||
	sec_source_id_ >= mixer.sources_.size())
	throw std::range_error("video source id out of range");
}

void mixer::video_mix_pic_in_pic::set_active(const mixer & mixer, bool active)
{
    if (mixer.sources_[pri_source_id_].src)
	mixer.sources_[pri_source_id_].src->set_active(
	    active ? source_active_video : source_active_none);
    if (mixer.sources_[sec_source_id_].src)
	mixer.sources_[sec_source_id_].src->set_active(
	    active ? source_active_video : source_active_none);
}

bool mixer::video_mix_pic_in_pic::apply(const mix_data & m,
					const auto_codec & decoder,
					raw_frame_ptr & mixed_raw,
					dv_frame_ptr &)
{
    const dv_frame_ptr & pri_source_dv = m.source_frames[pri_source_id_];
    const dv_frame_ptr & sec_source_dv = m.source_frames[sec_source_id_];

    if (pri_source_dv &&
	dv_frame_system(pri_source_dv.get()) == m.format.system &&
	sec_source_dv &&
	dv_frame_system(sec_source_dv.get()) == m.format.system)
    {
	// Decode sources
	mixed_raw = decode_video_frame(decoder, pri_source_dv);
	raw_frame_ptr sec_source_raw =
	    decode_video_frame(decoder, sec_source_dv);

	// Mix raw video
	video_effect_pic_in_pic(
	    make_raw_frame_ref(mixed_raw), dest_region_,
	    make_raw_frame_ref(sec_source_raw),
	    raw_frame_system(sec_source_raw.get())->active_region);
    }
    return false;
}

// Fade video mix - performs a linear interpolation of the values of both
// sources on an 8-bit scale. Useful for fading.
// area: 0: normal=all
//       1,2,3,4: bottom 1/2, 1/3, 1/4, 1/6;
//       5,6,7,8: top 1/6, 1/4, 1/3, 1/2;
// area=3 is useful for superimposed subtitles.

class mixer::video_mix_fade : public video_mix
{
public:
    video_mix_fade(source_id pri_source_id,
		   source_id sec_source_id,
		   bool timed, unsigned int ms,
		   uint8_t scale=0,
		   uint8_t area=0)
	: pri_source_id_(pri_source_id),
	  sec_source_id_(sec_source_id),
	  timed_(timed),
	  scale_(scale),
	  area_(area),
	  bucketsize_(ms * 1000 / 255),
	  modulo_(0),
	  us_per_frame_(0)
    {}
    uint8_t get_scale() { return scale_; };

private:
    virtual void validate(const mixer &);
    virtual void set_active(const mixer &, bool active);
    virtual bool apply(const mix_data &, const auto_codec &, raw_frame_ptr &, dv_frame_ptr &);
    virtual void status(mixer::monitor * monitor);

    source_id pri_source_id_, sec_source_id_;
    bool timed_;
    uint8_t scale_;
    uint8_t area_;
    int bucketsize_;
    int modulo_;
    int us_per_frame_;
};

void mixer::video_mix_fade::validate(const mixer & mixer)
{
    if (pri_source_id_ >= mixer.sources_.size() ||
	sec_source_id_ >= mixer.sources_.size())
	throw std::range_error("video source id out of range");
    if (!bucketsize_ && timed_)
	throw std::range_error("timeout too short");
}

void mixer::video_mix_fade::set_active(const mixer & mixer, bool active)
{
    if (mixer.sources_[pri_source_id_].src)
	mixer.sources_[pri_source_id_].src->set_active(
	    active ? source_active_video : source_active_none);
    if (mixer.sources_[sec_source_id_].src)
	mixer.sources_[sec_source_id_].src->set_active(
	    active ? source_active_video : source_active_none);
}

void mixer::video_mix_fade::status(mixer::monitor * monitor)
{
    monitor->effect_status(0, scale_, 255, timed_);
}

bool mixer::video_mix_fade::apply(const mix_data & m,
				  const auto_codec & decoder,
				  raw_frame_ptr & mixed_raw,
				  dv_frame_ptr &)
{
    bool retval = false;
    const dv_frame_ptr & pri_source_dv = m.source_frames[pri_source_id_];
    const dv_frame_ptr & sec_source_dv = m.source_frames[sec_source_id_];

    if (timed_)
    {
	int val;
	int step;

	retval = true;
	if (!us_per_frame_)
	    us_per_frame_ = ((1000000 * m.format.system->frame_rate_denom) /
			     m.format.system->frame_rate_numer);
	val = modulo_ + us_per_frame_;
	step = val / bucketsize_;
	modulo_ = val % bucketsize_;
	if ((scale_ + step) >= 255)
	{
	    timed_ = false;
	    scale_ = 255;
	}
	else
	{
	    scale_ += step;
	}
    }
    if (pri_source_dv &&
	dv_frame_system(pri_source_dv.get()) == m.format.system &&
	sec_source_dv &&
	dv_frame_system(sec_source_dv.get()) == m.format.system)
    {
	// Decode sources
	mixed_raw = decode_video_frame(decoder, pri_source_dv);
	raw_frame_ptr sec_source_raw =
	    decode_video_frame(decoder, sec_source_dv);

	// Mix raw video
	video_effect_fade(make_raw_frame_ref(mixed_raw),
			  make_raw_frame_ref(sec_source_raw), scale_, area_);
    }
    return retval;
}

std::tr1::shared_ptr<mixer::video_mix> mixer::create_video_mix_simple(source_id id)
{
    return std::tr1::shared_ptr<mixer::video_mix>(new video_mix_simple(id));
}

std::tr1::shared_ptr<mixer::video_mix>
mixer::create_video_mix_pic_in_pic(source_id pri_source_id,
				   source_id sec_source_id, rectangle dest_region)
{
    return std::tr1::shared_ptr<mixer::video_mix>(
	new video_mix_pic_in_pic(pri_source_id, sec_source_id, dest_region));
}

std::tr1::shared_ptr<mixer::video_mix>
mixer::create_video_mix_fade(source_id pri_source_id,
			     source_id sec_source_id, bool timed,
			     unsigned int ms, uint8_t scale, uint8_t area)
{
    return std::tr1::shared_ptr<mixer::video_mix>(
        new video_mix_fade(pri_source_id, sec_source_id, timed, ms, scale, area));
}

void mixer::run_mixer()
{
    dv_frame_ptr last_mixed_dv;
    unsigned serial_num = 0;
    const mix_data * m = 0;
    unsigned repeating_mixed_frame = 0;

    auto_codec decoder(auto_codec_open_decoder(CODEC_ID_DVVIDEO));
    AVCodecContext * dec = decoder.get();
    dec->get_buffer = raw_frame_get_buffer;
    dec->release_buffer = raw_frame_release_buffer;
    dec->reget_buffer = raw_frame_reget_buffer;

    auto_codec encoder(avcodec_alloc_context3(NULL));
    AVCodecContext * enc = NULL;

    for (;;)
    {
	// Get the next set of source frames and mix settings (or stop
	// if requested)
	{
	    boost::mutex::scoped_lock lock(mixer_mutex_);

	    if (m)
		mixer_queue_.pop();

	    while (mixer_state_ != run_state_stop && mixer_queue_.empty())
		mixer_state_cond_.wait(lock);
	    if (mixer_state_ == run_state_stop)
		break;

	    m = &mixer_queue_.front();
	}

	for (unsigned id = 0; id != m->source_frames.size(); ++id)
	    if (m->source_frames[id])
		m->source_frames[id]->serial_num = serial_num;

	dv_frame_ptr mixed_dv;
	raw_frame_ptr mixed_raw;

	if (m->settings.video_mix->apply(*m, decoder, mixed_raw, mixed_dv))
	    m->settings.video_mix->status(monitor_);

	if (mixed_raw)
	{
	    // Encode mixed video
	    const dv_system * system = m->format.system;
	    if (!enc) {
		enc = encoder.get();
		if (!enc)
		    throw std::bad_alloc();
		enc->width = system->frame_width;
		enc->height = system->frame_height;
		enc->pix_fmt = mixed_raw->pix_fmt;

		// Try to use one thread per CPU, up to a limit of 8
		int enc_thread_count =
		    std::min<int>(8, std::max<long>(sysconf(_SC_NPROCESSORS_ONLN), 1));
		std::cout << "INFO: DV encoder threads: " << enc_thread_count << "\n";
		auto_codec_open_encoder(encoder, CODEC_ID_DVVIDEO, enc_thread_count);
	    }
	    enc->sample_aspect_ratio.num = system->pixel_aspect[m->format.frame_aspect].width;
	    enc->sample_aspect_ratio.den = system->pixel_aspect[m->format.frame_aspect].height;
	    // Work around libavcodec's aspect ratio confusion (bug #790)
	    enc->sample_aspect_ratio.num *= 40;
	    enc->sample_aspect_ratio.den *= 41;
	    enc->time_base.num = system->frame_rate_denom;
	    enc->time_base.den = system->frame_rate_numer;
	    mixed_raw->header.pts = serial_num;
  	    mixed_dv = allocate_dv_frame();
	    // FIXME: deprecated. use avcodec_encode_video2 instead:
	    int out_size = avcodec_encode_video(enc,
						mixed_dv->buffer, system->size,
						&mixed_raw->header);
	    assert(size_t(out_size) == system->size);
	    mixed_dv->serial_num = serial_num;

	    // libavcodec doesn't properly distinguish IEC and SMPTE
	    // variants of NTSC.  Fix the APTs here.
	    if (system == &dv_system_525_60)
	    {
		uint8_t * block = mixed_dv->buffer;
		unsigned apt = 0;
		for (unsigned i = 4; i != 8; ++i)
		    block[i] = (block[i] & 0xf8) | apt;
	    }
	}

	if (!mixed_dv)
	{
	    if (!(repeating_mixed_frame & 0x3ff))
	      {
	        if (repeating_mixed_frame)
	          std::cerr << "\n";
	        std::cerr << "WARN: Repeating mixed frame " << serial_num << "\n"; // XXX not very informative
		repeating_mixed_frame++;
	      }
	    else
	      std::cerr << ".";

	    // Make a copy of the last mixed frame so we can
	    // replace the audio.  (We can't modify the last frame
	    // because sinks may still be reading from it.)
	    mixed_dv = allocate_dv_frame();
	    std::memcpy(mixed_dv.get(),
			last_mixed_dv.get(),
			offsetof(dv_frame, buffer)
			+ dv_frame_system(last_mixed_dv.get())->size);
	    mixed_dv->serial_num = serial_num;
	}
	else
	{
	  if (repeating_mixed_frame)
	    std::cerr << "WARN: End repeating " << serial_num << "\n";
	  repeating_mixed_frame = 0;
	}

	const dv_frame_ptr & audio_source_dv =
	    m->source_frames[m->settings.audio_source_id];

	if (!audio_source_dv ||
	    dv_frame_get_sample_rate(audio_source_dv.get()) != m->format.sample_rate)
	{
	    if (m->format.sample_rate >= 0)
		dv_buffer_silence_audio(mixed_dv->buffer, m->format.sample_rate,
					serial_num);
	}
	else if (mixed_dv != audio_source_dv)
	    dv_buffer_dub_audio(mixed_dv->buffer, audio_source_dv->buffer);

	set_times(*mixed_dv);

	mixed_dv->do_record = m->settings.do_record;
	mixed_dv->cut_before = m->settings.cut_before;

	last_mixed_dv = mixed_dv;
	++serial_num;

	// Sink the frame
	{
	    boost::mutex::scoped_lock lock(sink_mutex_);
	    for (sink_id id = 0; id != sinks_.size(); ++id)
		if (sinks_[id])
		    sinks_[id]->put_frame(mixed_dv);
	}
	if (monitor_)
	    monitor_->put_frames(m->source_frames.size(), &m->source_frames[0],
				 m->settings, mixed_dv, mixed_raw);
    }
}
