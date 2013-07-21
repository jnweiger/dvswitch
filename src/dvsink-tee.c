// Copyright 2007-2008 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// See the file "COPYING" for licence details.
// Copyright 2013 jw@suse.de

// Sink that creates DIF ("raw DV") files and 
// runs an arbitrary command
/*
 * 2013-07-19 jw@suse.de, V0.1  - initial draught
 * 2013-07-21 jw@suse.de, V0.2  - automerge framedrops, unless -s
 */
#define VERSION "0.2"

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
    {"split",   0, NULL, 's'},
    {NULL,     0, NULL, 0}
};

char *output_name_format_default = "output_%F_%H%M%S";
static int always_number = 1;
static char * mixer_host = NULL;
static char * mixer_port = NULL;
static char * pipe_command = NULL;
static char * output_name_format = NULL;
static int automerge = 1;

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
    else if (strcmp(name, "AUTO_MERGE") == 0)
    {
        automerge = atoi(value);
    }
}

static void usage(const char * progname)
{
    fprintf(stderr,
	    "\
Usage: %s [-h HOST] [-p PORT] [-a] [-s] [-c 'COMMAND' ] [NAME-FORMAT]\n",
	    progname);
    fprintf(stderr, "\n");
    fprintf(stderr, " -a     switch off autonumbering, only done when collisons.\n");
    fprintf(stderr, "        Default: always add a '%%04d' numbering suffix.\n");
    fprintf(stderr, " -c 'COMMAND'  Additionally forward the raw dv-stream to stdin of a\n");
    fprintf(stderr, "        command when recording. Note that this differs from dvsink-command,\n");
    fprintf(stderr, "        which would also feed its command, when not recording.\n");
    fprintf(stderr, "        Default: only sink to files\n");
    fprintf(stderr, " -s     Split files when dropping frames, to be compatible with dvsink-files.\n");
    fprintf(stderr, "        Default: automatically merge the stream unless an explicit cut is done.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, " NAME-FORMAT supports all strftime escapes.\n");
    fprintf(stderr, "        Default is '%s' unless overwritten by\n", output_name_format_default);
    fprintf(stderr, "        OUTPUT_NAME_FORMAT=... in /etc/dvswitchrc .\n");
    fprintf(stderr, "\ndvsink-tee V%s\n\n", VERSION);
}

struct transfer_params {
    int            sock;
};

static int create_file(const char * format, const char * suff, char ** name)
{
    static int static_suffix_num = 0;
    time_t now;
    struct tm now_local;
    size_t name_buf_len = 200, name_len;
    char * name_buf = 0;
    int file = -1;

    if (!suff) suff = ".dv";
    size_t suff_len = strlen(suff);

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
    if (name_len <= suff_len || strcmp(name_buf + name_len - suff_len, suff) != 0)
	strcpy(name_buf + name_len, suff);
    else
	name_len -= suff_len;

    if (always_number)
      {
        sprintf(name_buf + name_len, "-%04d%s", ++static_suffix_num, suff);
	suffix_num = static_suffix_num;
      }

    for (;;)
    {
	file = open(name_buf, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (file >= 0)
	{
	    *name = name_buf;
	    return file;
	}
	else if (errno == EEXIST)
	{
	    // Name collision; try changing the suffix
            sprintf(name_buf + name_len, "-%04d%s", ++suffix_num, suff);
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

	if ((buf[SINK_FRAME_CUT_FLAG_POS] == SINK_FRAME_CUT_OVERFLOW) &&
	    automerge)
	  {
	    int cutfd = create_file(output_name_format, ".cut", &name);
	    write(cutfd, "O\n", 2);
	    close(cutfd);
	    buf[SINK_FRAME_CUT_FLAG_POS] = '\0';
	  }

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
	        if (pipe_command)
		  printf("INFO: Stopped piping.\n");
		printf("INFO: Stopped recording.\n");
		fflush(stdout);
		continue;
	    }

	    file = create_file(output_name_format, NULL, &name);
	    if (starting)
	      {
	        if (pipe_command)
		  printf("INFO: Started piping\n");
		printf("INFO: Started recording\n");
	      }
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
    while ((opt = getopt_long(argc, argv, "h:p:asc:", options, NULL)) != -1)
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
	case 's': // --split
	    automerge = 0;
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
        printf("INFO: ready: '%s'\n", pipe_command);
        printf("INFO: will start piping, when record is pressed.\n");
      }

    transfer_frames(&params, cmd_fd);

    if (pipe_command)
      pclose(fp);

    close(params.sock);

    return 0;
}
