// Copyright 2007-2008 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// See the file "COPYING" for licence details.
// Copyright 2013 jw@suse.de

// Sink that creates DIF ("raw DV") files and 
// runs an arbitrary command

#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "dif.h"
#include "protocol.h"
#include "socket.h"

static struct option options[] = {
    {"host",   1, NULL, 'h'},
    {"port",   1, NULL, 'p'},
    {"help",   0, NULL, 'H'},
    {"command", 1,NULL, 'c'},
    {NULL,     0, NULL, 0}
};

char *output_name_format_default = "output_%F_%H%M%S";
static int always_number = 1;
static char * mixer_host = NULL;
static char * mixer_port = NULL;
static char * pipe_command = NULL;
static char * output_name_format = NULL;

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
    else if (strcmp(name, "OUTPUT_NAME_FORMAT") == 0)
    {
	free(output_name_format);
	output_name_format = strdup(value);
    }
}

static void usage(const char * progname)
{
    fprintf(stderr,
	    "\
Usage: %s [-h HOST] [-p PORT] [-a] [-c 'COMMAND' ] [NAME-FORMAT]\n",
	    progname);
    fprintf(stderr, "\n");
    fprintf(stderr, " -a     switch off autonumbering, only done when collisons.\n");
    fprintf(stderr, "        Default: always add a '%%04d' numbering suffix.\n");
    fprintf(stderr, " -c 'COMMAND'  Additionally run a command as with dvsink-command.\n");
    fprintf(stderr, "        Default: only sink to files\n");
    fprintf(stderr, "\n");
    fprintf(stderr, " NAME-FORMAT supports all strftime escapes.\n");
    fprintf(stderr, "        Default is '%s' unless overwritten by a\n", output_name_format_default);
    fprintf(stderr, "        OUTPUT_NAME_FORMAT=... in /etc/dvswitchrc .\n");
}

struct transfer_params {
    int            sock;
};

static int create_file(const char * format, char ** name)
{
    time_t now;
    struct tm now_local;
    size_t name_buf_len = 200, name_len;
    char * name_buf = 0;
    int file = -1;

    now = time(0);
    localtime_r(&now, &now_local);

    // Allocate a name buffer and generate the name in it, leaving room
    // for a suffix.
    for (;;)
    {
	name_buf = realloc(name_buf, name_buf_len);
	if (!name_buf)
	{
	    perror("realloc");
	    exit(1);
	}
	name_len = strftime(name_buf, name_buf_len - 20,
			    format, &now_local);
	if (name_len > 0)
	    break;

	// Try a bigger buffer.
	name_buf_len *= 2;
    }

    // Add ".dv" extension if missing.  Add distinguishing
    // number before it if necessary to avoid collision.
    // Create parent directories as necessary.
    int suffix_num = 0;
    if (name_len <= 3 || strcmp(name_buf + name_len - 3, ".dv") != 0)
	strcpy(name_buf + name_len, ".dv");
    else
	name_len -= 3;
    for (;;)
    {
        if (!always_number)
	  file = open(name_buf, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (file >= 0)
	{
	    *name = name_buf;
	    return file;
	}
	else if (always_number || (errno == EEXIST))
	{
	    // Name collision; try changing the suffix
	    sprintf(name_buf + name_len, "-%04d.dv", ++suffix_num);
	}
	else if (errno == ENOENT)
	{
	    // Parent directory missing
	    char * p = name_buf + 1;
	    while ((p = strchr(p, '/')))
	    {
		*p = 0;
		if (mkdir(name_buf, 0777) < 0 && errno != EEXIST)
		{
		    fprintf(stderr, "ERROR: mkdir %s: %s\n",
			    name_buf, strerror(errno));
		    exit(1);
		}
		*p++ = '/';
	    }
	}
	else
	{
	    fprintf(stderr, "ERROR: open %s: %s\n",
		    name_buf, strerror(errno));
	    exit(1);
	}
    }

    *name = name_buf;
    return file;
}

static ssize_t write_retry(int fd, const void * buf, size_t count)
{
    ssize_t chunk, total = 0;

    do
    {
	chunk = write(fd, buf, count);
	if (chunk < 0)
	    return chunk;
	total += chunk;
	buf = (const char *)buf + chunk;
	count -= chunk;
    }
    while (count);

    return total;
}

static void transfer_frames(struct transfer_params * params, int cmd_fd)
{
    static uint8_t buf[SINK_FRAME_HEADER_SIZE + DIF_MAX_FRAME_SIZE];
    const struct dv_system * system;

    int file = -1;
    char * name;
    ssize_t read_size;

    for (;;)
    {
	size_t wanted_size = SINK_FRAME_HEADER_SIZE;
	size_t buf_pos = 0;
	do
	{
	    read_size = read(params->sock, buf + buf_pos,
			     wanted_size - buf_pos);
	    if (read_size <= 0)
		goto read_failed;
	    buf_pos += read_size;
	}
	while (buf_pos != wanted_size);

	// Open/close files as necessary
	if (buf[SINK_FRAME_CUT_FLAG_POS] || file < 0)
	{
	    bool starting = file < 0;

	    if (file >= 0)
	    {
		close(file);
		file = -1;
	    }

	    // Check for stop indicator
	    if (buf[SINK_FRAME_CUT_FLAG_POS] == SINK_FRAME_CUT_STOP)
	    {
		printf("INFO: Stopped recording\n");
		fflush(stdout);
		continue;
	    }

	    file = create_file(output_name_format, &name);
	    if (starting)
		printf("INFO: Started recording\n");
	    printf("INFO: Created file %s\n", name);
	    fflush(stdout);
	}

	wanted_size = SINK_FRAME_HEADER_SIZE + DIF_SEQUENCE_SIZE;
	do
	{
	    read_size = read(params->sock, buf + buf_pos,
			     wanted_size - buf_pos);
	    if (read_size <= 0)
		goto read_failed;
	    buf_pos += read_size;
	}
	while (buf_pos != wanted_size);

	system = dv_buffer_system(buf + SINK_FRAME_HEADER_SIZE);
	wanted_size = SINK_FRAME_HEADER_SIZE + system->size;
	do
	{
	    read_size = read(params->sock, buf + buf_pos,
			     wanted_size - buf_pos);
	    if (read_size <= 0)
		goto read_failed;
	    buf_pos += read_size;
	}
	while (buf_pos != wanted_size);

	if (write_retry(file, buf + SINK_FRAME_HEADER_SIZE, system->size)
	    != (ssize_t)system->size)
	{
	    perror("ERROR: write");
	    exit(1);
	}

	if (cmd_fd >= 0)
	  {
	    unsigned int written = 0;
	    while (written < system->size)
	      {
	        int r = write(cmd_fd, buf + SINK_FRAME_HEADER_SIZE+written, system->size-written);
	        if (r <= 0)
		  {
		    perror("ERROR: write cmd");
	            exit(1);
		  }
		written += r;
	      }
	  }
    }

read_failed:
    if (read_size != 0)
    {
	perror("ERROR: read");
	exit(1);
    }

    if (file >= 0)
	close(file);
}

int main(int argc, char ** argv)
{
    // Initialise settings from configuration files.
    dvswitch_read_config(handle_config);

    // Parse arguments.

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:ac:", options, NULL)) != -1)
    {
	switch (opt)
	{
	case 'c':
	    if (pipe_command) free(pipe_command);
	    pipe_command = strdup(optarg);
	    break;
	case 'a':
	    always_number = 0;
	    break;
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

    if (optind < argc)
	output_name_format = argv[optind];

    if (optind < argc - 1)
    {
	fprintf(stderr, "%s: excess argument \"%s\"\n",
		argv[0], argv[optind + 1]);
	usage(argv[0]);
	return 2;
    }

    if (!output_name_format || !output_name_format[0])
    {
        output_name_format = strdup(output_name_format_default);
	fprintf(stderr, "Using default output name format: %s\n", output_name_format);
    }

    struct transfer_params params;
    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    fflush(stdout);
    params.sock = create_connected_socket(mixer_host, mixer_port);
    assert(params.sock >= 0); // create_connected_socket() should handle errors
    if (write(params.sock, GREETING_REC_SINK, GREETING_SIZE) != GREETING_SIZE)
    {
	perror("ERROR: write");
	exit(1);
    }
    printf("INFO: Connected.\n");

    int cmd_fd = -1;
    FILE *fp = NULL;
    if (pipe_command)
      {
        fp = popen(pipe_command, "w");
        cmd_fd = fileno(fp);
      }

    transfer_frames(&params, cmd_fd);

    if (pipe_command)
      pclose(fp);

    close(params.sock);

    return 0;
}
