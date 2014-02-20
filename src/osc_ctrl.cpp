// Copyright 2012 Robin Gareus <robin@gareus.org>
// See the file "COPYING" for licence details.

// opensoundcontrol interface

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <unistd.h>
#include <gtkmm/main.h>

#include "osc_ctrl.hpp"

static void oscb_error(int num, const char * m, const char * path)
{
    fprintf(stderr, "OSC server error %d in path %s: %s\n", num, path, m);
}

//////////////////////////////////////////////////////////////////////////////

void OSC::oscb_pri (lo_arg ** argv, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'src/pri' %d %d\n", argc, argv[0]->i);
    pri_video_selected_signal_(argv[0]->i);
}

void OSC::oscb_sec (lo_arg ** argv, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'src/sec' %d %d\n", argc, argv[0]->i);
    sec_video_selected_signal_(argv[0]->i);
}

void OSC::oscb_snd (lo_arg ** argv, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'src/snd' %d %d\n", argc, argv[0]->i);
    audio_selected_signal_(argv[0]->i);
}

void OSC::oscb_start (lo_arg **, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'rec/start' %d \n", argc);
    start_recording_signal_();
}

void OSC::oscb_stop (lo_arg **, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'rec/stop' %d \n", argc);
    stop_recording_signal_();
}

void OSC::oscb_cut (lo_arg **, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'rec/cut' %d \n", argc);
    cut_recording_signal_();
}

void OSC::oscb_overlay (lo_arg ** argv, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'overlay' %d %d\n", argc, argv[0]->i);
    mfade_set(argv[0]->i);
}

void OSC::oscb_fade (lo_arg ** argv, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'fade' %d %d\n", argc, argv[0]->i);
    tfade_set(argv[0]->i);
}

void OSC::oscb_quit (lo_arg **, int argc)
{
    if (want_verbose_)
	fprintf(stderr, "OSC 'quit' %d\n", argc);
    quit_signal_();
}

//////////////////////////////////////////////////////////////////////////////

OSC::OSC(bool want_verbose)
    : want_verbose_ (want_verbose),
      oscst_(NULL),
      oscout_(NULL),
      osc_server_(0)
{
}

OSC::~OSC()
{
    shutdown_osc();
}

int OSC::initialize_osc(int osc_port)
{
    char tmp[8];
    uint32_t port = (osc_port > 100 && osc_port < 60000)?osc_port:5675;

    snprintf(tmp, sizeof(tmp), "%d", port);
    if (want_verbose_)
	fprintf(stderr, "OSC trying port:%i\n",port);
    oscst_ = lo_server_new (tmp, &oscb_error);

    if (!oscst_)
    {
	fprintf(stderr, "OSC start failed.");
	return(1);
    }

    if(want_verbose_)
    {
	char * urlstr;
	urlstr = lo_server_get_url (oscst_);
	fprintf(stderr, "OSC server name: %s\n",urlstr);
	free (urlstr);
    }

#define OSC_REGISTER_CALLBACK(serv,path,types, function) lo_server_add_method (serv, path, types, OSC::_ ## function, this)

    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/src/pri", "i", oscb_pri);
    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/src/sec", "i", oscb_sec);
    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/src/snd", "i",  oscb_snd);

    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/fx/overlay", "i",   oscb_overlay);
    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/fx/fade",    "i",   oscb_fade);

    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/rec/start", "",  oscb_start);
    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/rec/stop",  "",  oscb_stop);
    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/rec/cut",   "",  oscb_cut);

    OSC_REGISTER_CALLBACK(oscst_, "/dvswitch/app/quit", "", oscb_quit);

    if(want_verbose_)
	fprintf(stderr, "OSC server started on port %i\n",port);
    return (0);
}

bool OSC::osc_input_handler (Glib::IOCondition ioc, lo_server srv)
{
    if (ioc & ~Glib::IO_IN)
    {
	return false;
    }
    if (ioc & Glib::IO_IN)
    {
	lo_server_recv (srv);
    }
    return true;
}

void OSC::setup_thread(Glib::RefPtr<Glib::MainContext> main_context)
{
    Glib::RefPtr<Glib::IOSource> src = Glib::IOSource::create (lo_server_get_socket_fd (oscst_), Glib::IO_IN|Glib::IO_HUP|Glib::IO_ERR);
    src->connect(sigc::bind(sigc::mem_fun(*this, &OSC::osc_input_handler), oscst_));
    src->attach(main_context);
    osc_server_ = src->gobj();
    g_source_ref (osc_server_);
}

void OSC::shutdown_osc(void)
{
    if (oscout_)
    {
	lo_address_free(oscout_);
    }

    if (osc_server_) {
	g_source_destroy(osc_server_);
	g_source_unref(osc_server_);
	osc_server_ = 0;
    }

    if (oscst_)
    {
	int fd = lo_server_get_socket_fd(oscst_);
	if (fd >=0)
	{
	    close(fd);
	}
	lo_server_free (oscst_);
	if(want_verbose_)
	    fprintf(stderr, "OSC server shut down.\n");
    }
}

int OSC::initialize_osc_client(int osc_port)
{
    char tmp[8];
    uint32_t port = (osc_port > 100 && osc_port < 60000)?osc_port:5675;
    snprintf(tmp, sizeof(tmp), "%d", port);
    const char * osc_host = "localhost";
    oscout_ = lo_address_new(osc_host, tmp);
    if (!oscout_)
    {
	fprintf(stderr, "OSC client unable to set address.");
	return(1);
    }
    return 0;
}

int OSC::osc_send(const char * msg, const char * type,...)
{
    if (!oscout_)
    {
	return(-1);
    }

    va_list ap;
    va_start(ap, type);
    int rv = lo_send(oscout_, msg, type, ap);
    va_end(ap);
    return rv;
}

// vi:ts=8 sw=4:
