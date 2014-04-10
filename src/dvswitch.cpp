// Copyright 2007-2008 Ben Hutchings.
// See the file "COPYING" for licence details.

// Top level of DVswitch

#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <ostream>
#include <string>

#include <getopt.h>

#include <locale.h>
#include <libintl.h>

#include <gtkmm/main.h>
#include <sigc++/functors/slot.h>

#include "avcodec_wrap.h"

#include "config.h"
#include "connector.hpp"
#include "mixer.hpp"
#include "mixer_window.hpp"
#include "server.hpp"
#include "osc_ctrl.hpp"

namespace
{
    struct option options[] = {
	{"host",             1, NULL, 'h'},
	{"port",             1, NULL, 'p'},
	{"osc",              1, NULL, 'o'},
	{"help",             0, NULL, 'H'},
	{"safe-area-off",    0, NULL, 'S'},
	{NULL,               0, NULL, 0}
    };

    std::string mixer_host;
    std::string mixer_port;
    std::string listen_addr;
    bool safe_area_flag = true; 

    extern "C"
    {
	void handle_config(const char * name, const char * value)
	{
	    if (std::strcmp(name, "MIXER_HOST") == 0)
		mixer_host = value;
	    else if (strcmp(name, "MIXER_PORT") == 0)
		mixer_port = value;
	    else if (strcmp(name, "LISTEN") == 0)
		listen_addr = value;
	    else if (strcmp(name, "SAFE_AREA") == 0)
		if (!strcasecmp(value, "off")  ||
		    !strcasecmp(value, "false") ||
		    !strcasecmp(value, "0"))
		  safe_area_flag = false;
	}
    }

    void usage(const char * progname)
    {
	std::cerr << "\
Usage: " << progname << " [gtk-options] \\\n\
           [{-h|--host} LISTEN-HOST] [{-p|--port} LISTEN-PORT] [{-o|--osc} OSC-PORT] [{-S|--safe-area-off}]\\\n\
	   (use --host '*'  for INADDR_ANY)\n";
    }
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    bindtextdomain("dvswitch", SHAREDIR "/locale");
    textdomain("dvswitch");
    try
    {
	dvswitch_read_config(handle_config);

	// Initialise Gtk
	Gtk::Main kit(argc, argv);

	// Complete option parsing with Gtk's options out of the way.

	int osc_port = 0;
	int opt;
	while ((opt = getopt_long(argc, argv, "h:p:o:S", options, NULL)) != -1)
	{
	    switch (opt)
	    {
	    case 'h':
		listen_addr = mixer_host = optarg;
		break;
	    case 'p':
		mixer_port = optarg;
		break;
	    case 'o': /* --osc */
		osc_port = atoi(optarg);
		break;
	    case 'H': /* --help */
		usage(argv[0]);
		return 0;
	    case 'S': /* --safe-area-off */
		safe_area_flag = false;
		break;
	    default:
		usage(argv[0]);
		return 2;
	    }
	}

	if (mixer_host.empty() || mixer_port.empty())
	{
	    std::cerr << argv[0] << ": mixer hostname and port not defined\n";
	    return 2;
	}

	if (!listen_addr.empty())
	  mixer_host = listen_addr;

	// The mixer must be created before the window, since we pass
	// a reference to the mixer into the window's constructor to
	// allow it to adjust the mixer's controls.
	// However, the mixer must also be destroyed before the
	// window, since as long as it exists it may call into the
	// window as a monitor.
	// This should probably be fixed by a smarter design, but for
	// now we arrange this by attaching the window to an auto_ptr.
	std::auto_ptr<mixer_window> the_window;
	mixer the_mixer;
	server the_server(mixer_host, mixer_port, the_mixer);
	connector the_connector(the_mixer);
	the_window.reset(new mixer_window(the_mixer, the_connector, safe_area_flag));
	the_mixer.set_monitor(the_window.get());
	the_window->show();
	the_window->signal_hide().connect(sigc::ptr_fun(&Gtk::Main::quit));
	if (osc_port > 0 )
	{
	    OSC *os = new OSC();
	    if (!os->initialize_osc(osc_port))
	    {
		os->setup_thread(Glib::MainContext::get_default());
		the_window->init_osc_connection(os);
	    }
	}
	Gtk::Main::run();
	return EXIT_SUCCESS;
    }
    catch (std::exception & e)
    {
	std::cerr << "ERROR: " << e.what() << "\n";
	return EXIT_FAILURE;
    }
    catch (Glib::Exception & e)
    {
       std::cerr << "ERROR: " << e.what() << "\n";
       return EXIT_FAILURE;
    }
}
