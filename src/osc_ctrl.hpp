// Copyright 2012 Robin Gareus <robin@gareus.org>
// See the file "COPYING" for licence details.

// opensoundcontrol interface

#ifndef DVSWITCH_OSC_CTRL_HPP
#define DVSWITCH_OSC_CTRL_HPP

#include <lo/lo.h>
#include <sigc++/sigc++.h>
#include "mixer.hpp"

class OSC
{
    public:
	OSC(bool want_verbose = false);
	~OSC();
	int initialize_osc(int osc_port);
	int initialize_osc_client(int osc_port);
	void shutdown_osc(void);
	void setup_thread(Glib::RefPtr<Glib::MainContext> main_context);

	int osc_send(const char * msg, const char * type,...);

	sigc::signal1<void, mixer::source_id> & signal_pri_video_selected() { return pri_video_selected_signal_;}
	sigc::signal1<void, mixer::source_id> & signal_sec_video_selected() { return sec_video_selected_signal_;}
	sigc::signal1<void, mixer::source_id> & signal_audio_selected()     { return audio_selected_signal_;}
	sigc::signal1<void, int> & signal_tfade_set()    { return tfade_set;}
	sigc::signal1<void, int> & signal_mfade_set() { return mfade_set;}
	sigc::signal<void> & signal_cut_recording()   { return cut_recording_signal_;}
	sigc::signal<void> & signal_stop_recording()  { return stop_recording_signal_;}
	sigc::signal<void> & signal_start_recording() { return start_recording_signal_;}

    private:
	bool osc_input_handler (Glib::IOCondition ioc, lo_server srv);

	bool want_verbose_;
	lo_server oscst_;
	lo_address oscout_;
	GSource* osc_server_;

	sigc::signal1<void, mixer::source_id> pri_video_selected_signal_;
	sigc::signal1<void, mixer::source_id> sec_video_selected_signal_;
	sigc::signal1<void, mixer::source_id> audio_selected_signal_;
	sigc::signal1<void, int> tfade_set;
	sigc::signal1<void, int> mfade_set;
	sigc::signal<void> cut_recording_signal_;
	sigc::signal<void> start_recording_signal_;
	sigc::signal<void> stop_recording_signal_;


#define OSC_PATH_CALLBACK(name) \
	static int _ ## name (const char * /*path*/, const char * /*types*/, lo_arg ** argv, int argc, void * /*data*/, void * user_data) \
	{ \
	    static_cast<OSC*>(user_data)->name (argv, argc); \
	    return 0; \
	} \
	void name (lo_arg ** argv, int argc);

	OSC_PATH_CALLBACK(oscb_pri)
	OSC_PATH_CALLBACK(oscb_sec)
	OSC_PATH_CALLBACK(oscb_snd)

	OSC_PATH_CALLBACK(oscb_overlay)
	OSC_PATH_CALLBACK(oscb_fade)

	OSC_PATH_CALLBACK(oscb_cut)
	OSC_PATH_CALLBACK(oscb_start)
	OSC_PATH_CALLBACK(oscb_stop)
};

#endif // !defined(DVSWITCH_OSC_CTRL_HPP)
// vi:ts=8 sw=4:
