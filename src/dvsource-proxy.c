/* dvsource-proxy.c
 * (C) 2013 jw@suse.de
 * based on dvsource-file.c, which has the following copyright.
 */
/* Copyright 2007-2009 Ben Hutchings.
 * Copyright 2008 Petter Reinholdtsen.
 *
 * See the file "COPYING" for licence details.
 */
#if 0

first dummy implementation:
a static raw image is on-the-fly encoded to dv-format, then sent repeatedly to the mixer.
#endif


#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>


#include "config.h"
#include "dif.h"
#include "frame_timer.h"
#include "protocol.h"
#include "socket.h"


static struct option options[] = {
    {"host",   1, NULL, 'h'},
    {"port",   1, NULL, 'p'},
    {"proxyport", 1, NULL, 'P'},
    {"format", 1, NULL, 'f'},
    {"help",   0, NULL, 'H'},
    {NULL,     0, NULL, 0}
};

static char * mixer_host = NULL;
static char * mixer_port = NULL;
static char * proxy_port = NULL;
static char * dv_format = NULL;

static void handle_config(const char * name, const char * value)
{
    if (strcmp(name, "MIXER_HOST") == 0)
    {
	free(mixer_host);
	mixer_host = strdup(value);
    }
    else if (strcmp(name, "MIXER_PORT") == 0)
    {
	free(mixer_port);
	mixer_port = strdup(value);
    }
    else if (strcmp(name, "PROXY_PORT") == 0)
    {
	free(proxy_port);
	proxy_port = strdup(value);
    }
}


static void usage(const char * progname)
{
    fprintf(stderr,
	    "\
Usage: %s [-h HOST] [-p PORT] [PROXY_PORT]\n",
	    progname);

    fprintf(stderr, "\n\
dvsource-proxy is a synchronizer and format converter for 
streaming jpeg input.

The default PROXY_PORT is %d, use netcat to push data into the port.
If unconnected, a static builtin test image is reproduced.

Use netcat to connect a webcam vide with this proxy.

\n");

}


struct transfer_params {
    const char *   fallback_filename;
    char *         fallback_buffer;
    char *         buffer;
    int            proxy_sock;
    int            mixer_sock;
};


static int sighup_seen = 0;
static void sighup()
{
  sighup_seen = 1;
}

/*
 * transfer_frames takes a dv frame from the buffer 
 * and repeatedly sends it to the mixer, timed by frame_timer_wait()
 */
static void transfer_frames(struct transfer_params * params);

