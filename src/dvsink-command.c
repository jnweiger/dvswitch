// Copyright 2007 Ben Hutchings.
// See the file "COPYING" for licence details.

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>
#include <unistd.h>

#include "config.h"
#include "protocol.h"
#include "socket.h"

static struct option options[] = {
    {"host",   1, NULL, 'h'},
    {"port",   1, NULL, 'p'},
    {"help",   0, NULL, 'H'},
    {NULL,     0, NULL, 0}
};

static char * mixer_host = NULL;
static char * mixer_port = NULL;

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
}

static void usage(const char * progname)
{
    fprintf(stderr,
	    "\
Usage: %s [-h HOST] [-p PORT] COMMAND...\n",
	    progname);
}

int main(int argc, char ** argv)
{
    // Initialise settings from configuration files.
    dvswitch_read_config(handle_config);

    // Parse arguments.

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:", options, NULL)) != -1)
    {
	switch (opt)
	{
	case 'h':
	    free(mixer_host);
	    mixer_host = strdup(optarg);
	    break;
	case 'p':
	    free(mixer_port);
	    mixer_port = strdup(optarg);
	    break;
	case 'H': // --help
	    usage(argv[0]);
	    return 0;
	default:
	    usage(argv[0]);
	    return 2;
	}
    }

    if (!mixer_host || !mixer_port)
    {
	fprintf(stderr, "%s: mixer hostname and port not defined\n",
		argv[0]);
	return 2;
    }

    if (optind == argc)
    {
	fprintf(stderr, "%s: missing command\n", argv[0]);
	usage(argv[0]);
	return 2;
    }

    // Connect to the mixer, set that as stdin, and run given command.

    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    int sock = create_connected_socket(mixer_host, mixer_port);
    assert(sock >= 0); // create_connected_socket() should handle errors
    if (write(sock, GREETING_RAW_SINK, GREETING_SIZE) != GREETING_SIZE)
    {
	perror("ERROR: write");
	exit(1);
    }
    if (dup2(sock, STDIN_FILENO) < 0)
    {
	perror("ERROR: dup2");
	return 1;
    }
    close(sock);
    execvp(argv[optind], argv + optind);
    perror("ERROR: execvp");
    return 1;
}
