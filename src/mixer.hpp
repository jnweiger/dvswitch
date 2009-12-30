// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// The mixer.  This holds the current mixing settings and small
// buffers for each source.  It maintains a frame clock, selects and
// mixes frames at each clock tick, and passes frames in the sinks and
// monitor.

#ifndef DVSWITCH_MIXER_HPP
#define DVSWITCH_MIXER_HPP

#include <cstddef>
#include <vector>

#include <tr1/memory>

#include <boost/thread/condition.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/thread.hpp>

#include "auto_handle.hpp"
#include "frame.h"
#include "frame_pool.hpp"
#include "geometry.h"
#include "ring_buffer.hpp"

namespace boost
{
    class thread;
}

class mixer
{
public:
    // Identifiers to distinguish mixer's sources and sinks
    typedef unsigned source_id, sink_id;
    static const unsigned invalid_id = -1;

    // Settings for mixing/switching
    struct format_settings
    {
	const dv_system * system;
	dv_frame_aspect frame_aspect;
	dv_sample_rate sample_rate;
    };
    struct video_mix;
    struct mix_settings
    {
	std::tr1::shared_ptr<mixer::video_mix> video_mix;
	source_id audio_source_id;
	bool do_record;
	bool cut_before;
    };

    // Source activation flags
    enum source_activation {
	source_active_none = 0,
	// Source is used in the current video mix
	source_active_video = 1,
    };

    // Interface to sinks
    struct sink
    {
	// Put a frame out.
	// The frame is shared with other sinks and must not be
	// modified.  It should be released as soon as possible.
	// This will be called at the appropriate frame rate even
	// if there are no new frames available.  The serial_num
	// member of the frame can be used to check whether the
	// frame is new.
	virtual void put_frame(const dv_frame_ptr &) = 0;
    };

    struct source_settings
    {
	std::string name;
	std::string url;
	bool use_video;
	bool use_audio;
    };

    // Interface to sources
    struct source
    {
	// Tell a source whether it is active, i.e. selected for
	// output.  This may be used to control a tally light, for
	// example.
	virtual void set_active(source_activation) = 0;
    };

    // Interface to monitor
    struct monitor
    {
	// Display or otherwise use frames.
	//
	// source_count is the number of sources assigned, though some
	// may no longer be registered.  source_dv points to an array,
	// length source_count, of pointers to the frames clocked
	// through from these sources.  Any or all of these pointers
	// may be null if the sources are not producing frames.
	// mix_settings is a copy of the settings used to select and
	// mix these source frames.  mixed_dv is a pointer to the
	// mixed frame that was sent to sinks.
	//
	// mixed_raw is a pointer to the raw video for the mixed
	// frame, or null if the mixer did not need to decode video.
	//
	// All DV frames may be shared and must not be modified.  Raw
	// frames may be modified by the monitor.  All references and
	// pointers passed to the function are invalid once it
	// returns; it must copy shared_ptrs to ensure that frames
	// remain valid.
	//
	// This is called in the context of the mixer thread and should
	// return quickly.
	virtual void put_frames(unsigned source_count,
				const dv_frame_ptr * source_dv,
				mix_settings,
				const dv_frame_ptr & mixed_dv,
				const raw_frame_ptr & mixed_raw) = 0;
    };

    mixer();
    ~mixer();

    // Interface for sources
    // Register and unregister sources
    source_id add_source(source *, const source_settings &);
    void remove_source(source_id);
    // Add a new frame from the given source.  This should be called at
    // appropriate intervals to avoid the need to drop or duplicate
    // frames.
    void put_frame(source_id, const dv_frame_ptr &);

    // Interface for sinks
    // Register and unregister sinks
    sink_id add_sink(sink *);
    void remove_sink(sink_id);

    // Interface for monitors
    void set_monitor(monitor *);

    static std::tr1::shared_ptr<video_mix>
    create_video_mix_simple(source_id pri_source_id);
    static std::tr1::shared_ptr<video_mix>
    create_video_mix_pic_in_pic(source_id pri_source_id,
				source_id sec_source_id,
				rectangle dest_region);

    // Mixer interface
    format_settings get_format() const;
    void set_format(format_settings);
    // Set the video mix
    void set_video_mix(std::tr1::shared_ptr<video_mix>);
    // Select the audio source for output
    void set_audio_source(source_id);
    // Make a cut in the output as soon as possible, where appropriate
    // for the sink
    void cut();
    // Enable/disable recording
    void enable_record(bool);

private:
    class video_mix_pic_in_pic;
    class video_mix_simple;

    // Source data.  We want to allow a bit of leeway in the input
    // pipeline before we have to drop or repeat a frame.  At the
    // same time we don't want to add much to latency.  We try to
    // keep the queue half-full so there are 2 frame-times
    // (66-80 ms) of added latency here.
    static const std::size_t target_queue_len = 2;
    static const std::size_t full_queue_len = target_queue_len * 2;
    struct source_data
    {
	source_data() : src(NULL) {}
	ring_buffer<dv_frame_ptr, full_queue_len> frames;
	source * src;
    };

    struct mix_data
    {
	std::vector<dv_frame_ptr> source_frames;
	format_settings format;
	mix_settings settings;
    };

    enum run_state {
	run_state_wait,
	run_state_run,
	run_state_stop
    };

    void run_clock();   // clock thread function
    void run_mixer();   // mixer thread function

    mutable boost::mutex source_mutex_; // controls access to the following
    format_settings format_;
    mix_settings settings_;
    std::vector<source_data> sources_;
    run_state clock_state_;
    boost::condition clock_state_cond_;

    boost::thread clock_thread_;

    boost::mutex mixer_mutex_; // controls access to the following
    ring_buffer<mix_data, 10> mixer_queue_;
    run_state mixer_state_;
    boost::condition mixer_state_cond_;

    boost::thread mixer_thread_;

    boost::mutex sink_mutex_; // controls access to the following
    std::vector<sink *> sinks_;

    monitor * monitor_;
};

#endif // !defined(DVSWITCH_MIXER_HPP)
