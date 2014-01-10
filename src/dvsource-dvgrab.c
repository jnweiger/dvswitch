/* Copyright 2007-2009 Ben Hutchings.
 * Copyright 2008 Petter Reinholdtsen.
 * Copyright 2009 Wouter Verhelst.
 * See the file "COPYING" for licence details.
 */
/* Source that reads DIF over USB or Firewire using dvgrab */

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
    {"card",     1, NULL, 'c'},
    {"guid",	  1, NULL, 'g'},
    {"firewire", 0, NULL, 'F'},
    {"v4l2",     0, NULL, 'V'},
    {"host",     1, NULL, 'h'},
    {"port",     1, NULL, 'p'},
    {"tally",    0, NULL, 't'},
    {"verbose",  0, NULL, 'v'},
    {"help",     0, NULL, 'H'},
    {NULL,       0, NULL, 0}
};

enum mode {
    mode_unknown,
    mode_firewire,
    mode_v4l2,
    mode_dvgrab_max, // marker: below this, modes use dvgrab; above, modes use avconv
    mode_v4l2_raw,
};

static enum mode mode = mode_unknown;
static char * device_name = NULL;
static char * firewire_card = NULL;
static char * firewire_guid = NULL;
static char * mixer_host = NULL;
static char * mixer_port = NULL;
static int do_tally = 0;
static int verbose = 0;

static enum mode program_mode(const char * progname)
{
    const char * slash = strrchr(progname, '/');

    if (slash)
	progname = slash + 1;

    if (strcmp(progname, "dvsource-firewire") == 0)
	return mode_firewire;
    if (strcmp(progname, "dvsource-v4l2-dv") == 0)
	return mode_v4l2;
    if (strcmp(progname, "dvsource-v4l2-raw") == 0)
	return mode_v4l2_raw;
    return mode_unknown;
}

static void handle_config(const char * name, const char * value)
{
    if (strcmp(name, "FIREWIRE_CARD") == 0)
    {
	free(firewire_card);
	firewire_card = strdup(value);
    }
    else if (strcmp(name, "FIREWIRE_DEVICE") == 0 && mode == mode_firewire)
    {
	free(device_name);
	device_name = strdup(value);
    }
    else if (strcmp(name, "V4L2_DV_DEVICE") == 0 && (mode == mode_v4l2 || mode == mode_v4l2_raw))
    {
	free(device_name);
	device_name = strdup(value);
    }
    else if (strcmp(name, "MIXER_HOST") == 0)
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
    static const char firewire_args[] =
	"[-c CARD-NUMBER | DEVICE] [-g GUID]";
    static const char v4l2_args[] = "[DEVICE]";
    static const char other_args[] =
	"[-t] [-v] [-h HOST] [-p PORT]";

    switch (program_mode(progname))
    {
    case mode_unknown:
	fprintf(stderr,
		"Usage: %s %s \\\n"
		"           --firewire %s\n"
		"       %s %s \\\n"
		"           --v4l2 %s\n",
		progname, other_args, firewire_args,
		progname, other_args, v4l2_args);
	break;
    case mode_firewire:
	fprintf(stderr,
		"Usage: %s %s \\\n"
		"           %s\n",
		progname, other_args, firewire_args);
	break;
    case mode_v4l2:
    case mode_v4l2_raw:
	fprintf(stderr,
		"Usage: %s %s \\\n"
		"           %s\n",
		progname, other_args, v4l2_args);
	break;
    default:
	break;
    }
}

static ssize_t read_retry(int fd, void * buf, size_t count)
{
    ssize_t chunk, total = 0;

    do
    {
        chunk = read(fd, buf, count);
        if (chunk <= 0)
        {
            if (total == 0)
                return chunk;
            break;
        }
        total += chunk;
        buf = (char *)buf + chunk;
        count -= chunk;
    }
    while (count);

    return total;
}

static void tally(int sock)
{
    // Messages should never be buffered
    setbuf(stdout, NULL);

    for (;;)
    {
	char act_msg[ACT_MSG_SIZE];
	ssize_t read_size = read_retry(sock, act_msg, ACT_MSG_SIZE);
	if (read_size < ACT_MSG_SIZE)
	{
	    if (read_size < 0)
		perror("ERROR: read");
	    break;
	}

	if (act_msg[ACT_MSG_VIDEO_POS])
	    printf("TALLY: on\n");
	else
	    printf("TALLY: off\n");
    }

}

int main(int argc, char ** argv)
{
    mode = program_mode(argv[0]);

    /* Initialise settings from configuration files. */
    dvswitch_read_config(handle_config);

    /* Parse arguments. */

    int opt;
    while ((opt = getopt_long(argc, argv, "c:g:h:p:tv", options, NULL)) != -1)
    {
	switch (opt)
	{
	case 'c':
	    free(firewire_card);
	    firewire_card = strdup(optarg);
	    break;
	case 'g':
	    free(firewire_guid);
	    firewire_guid = strdup(optarg);
	    break;
	case 'F':
	    mode = mode_firewire;
	    break;
	case 'V':
	    mode = mode_v4l2;
	    break;
	case 'h':
	    free(mixer_host);
	    mixer_host = strdup(optarg);
	    break;
	case 'p':
	    free(mixer_port);
	    mixer_port = strdup(optarg);
	    break;
	case 't':
	    do_tally = 1;
	    break;
	case 'v':
	    verbose = 1;
	    break;
	case 'H': /* --help */
	    usage(argv[0]);
	    return 0;
	default:
	    usage(argv[0]);
	    return 2;
	}
    }

    if (optind != argc)
    {
	free(device_name);
	device_name = strdup(argv[optind++]);
    }

    if (!mixer_host || !mixer_port)
    {
	fprintf(stderr, "%s: mixer hostname and port not defined\n",
		argv[0]);
	return 2;
    }

    if (optind != argc)
    {
	fprintf(stderr, "%s: excess argument \"%s\"\n",
		argv[0], argv[optind]);
	usage(argv[0]);
	return 2;
    }

    if (mode == mode_firewire)
    {
	if (device_name)
	    printf("INFO: Reading from Firewire device %s\n", device_name);
	else if (firewire_card)
	    printf("INFO: Reading from Firewire card %s\n", firewire_card);
	else
	    printf("INFO: Reading from first Firewire card with camera\n");
    }
    else if (mode == mode_v4l2 || mode == mode_v4l2_raw)
    {
	if (!device_name)
	    device_name = "/dev/video";
	printf("INFO: Reading from V4L2 device %s\n", device_name);
    }
    else
    {
	fprintf(stderr, "%s: mode not defined (Firewire or V4L2)\n", argv[0]);
	return 2;
    }

    /* Connect to the mixer. */

    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    int sock = create_connected_socket(mixer_host, mixer_port);
    assert(sock >= 0); /* create_connected_socket() should handle errors */
    if (write(sock, do_tally ? GREETING_ACT_SOURCE : GREETING_SOURCE,
              GREETING_SIZE) != GREETING_SIZE)
    {
	perror("ERROR: write");
	exit(1);
    }
    printf("INFO: Connected\n");

    if (do_tally)
    {
	fflush(NULL);
	int child_pid = fork();

	if (child_pid < 0)
	{
	    perror("ERROR: fork");
	    return 1;
	}
	if (child_pid == 0)
	{
	    tally(sock);
	    _exit(0);
	}
    }

    /* Run dvgrab with the socket as stdout. */

    char * dvgrab_argv[13];
    char ** argp = dvgrab_argv;
    if (mode <= mode_dvgrab_max)
    {
	*argp++ = "dvgrab";
	if (mode == mode_v4l2)
	    *argp++ = "-v4l2";
	if (device_name)
	{
	    *argp++ = "-input";
	    *argp++ = device_name;
	}
	else if (firewire_card)
	{
	    *argp++ = "-card";
	    *argp++ = firewire_card;
	}
	if (firewire_guid)
	{
	    *argp++ = "-guid";
	    *argp++ = firewire_guid;
	}
	*argp++ = "-noavc";
	*argp++ = "-";
    } else {
	assert(mode == mode_v4l2_raw);
	*argp++ = "avconv";
	*argp++ = "-v";
	*argp++ = "quiet";
	*argp++ = "-f";
	*argp++ = "video4linux2";
	*argp++ = "-i";
	*argp++ = device_name;
	*argp++ = "-r";
	*argp++ = "25";
	*argp++ = "-target";
	*argp++ = "pal-dv";
	*argp++ = "-";
    }
    *argp = NULL;
    assert(argp < dvgrab_argv + sizeof(dvgrab_argv) / sizeof(dvgrab_argv[0]));

    if (verbose)
    {
	printf("INFO: Running");
	for (argp = dvgrab_argv; *argp != NULL; argp++)
	    printf(" %s", *argp);
	printf("\n");
    }

    if (dup2(sock, STDOUT_FILENO) < 0)
    {
	perror("ERROR: dup2");
	return 1;
    }
    close(sock);

    execvp(dvgrab_argv[0], dvgrab_argv);
    perror("ERROR: execvp");
    return 1;
}
