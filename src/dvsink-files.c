// Copyright 2007-2008 Ben Hutchings.
// Copyright 2008 Petter Reinholdtsen.
// Copyright 2014 Jürgen Weigert <jnweiger@gmail.com>
//
// See the file "COPYING" for licence details.

// Sink that creates DIF ("raw DV") files

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
#include <sys/statvfs.h>

#include "config.h"
#include "dif.h"
#include "protocol.h"
#include "socket.h"

#define MAX_PATH  4096
#define MBYTES_PER_HOUR 6500	// Standard DV-files (slight over-estimation)

static struct option options[] = {
    {"host",    1, NULL, 'h'},
    {"port",    1, NULL, 'p'},
    {"help",    0, NULL, 'H'},
    {"pidfile", 1, NULL, 'P'},
    {NULL,      0, NULL, 0}
};

static char * mixer_host = NULL;
static char * mixer_port = NULL;
static char * output_name_format = NULL;
static char * pidfile_name = NULL;

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
Usage: %s [-h HOST] [-p PORT] [-P PID filename] [NAME-FORMAT]\n\
   or\n\
       %s -t PATH_WHERE_TO_CHECK_DISK_SPACE\n",
	    progname, progname);
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
    int file;

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
	file = open(name_buf, O_CREAT | O_EXCL | O_WRONLY, 0666);
	if (file >= 0)
	{
	    *name = name_buf;
	    return file;
	}
	else if (errno == EEXIST)
	{
	    // Name collision; try changing the suffix
	    sprintf(name_buf + name_len, "-%d.dv", ++suffix_num);
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
 
static void print_disk_full_estimate(char *filename)
{
  char *path = strdup(filename);

  if (path[0] != '/')	// path = abs_path(path)
    {
      char cwd[MAX_PATH];
      char *c = getcwd(cwd, MAX_PATH);
      char *old = path;
      path = (char *)malloc(strlen(old)+strlen(c)+3);
      path[0] = '\0';
      strcat(path, c);
      if (path[strlen(path)] != '/')
        strcat(path, "/");
      strcat(path, old);
    }

  char *p = path+strlen(path);

  while (p > path)	// path = dirname(path); # file may not yet exist.
    {
      if (p[-1] == '/')
        {
	  *p = '\0';
	  break;
	}
      p--;
    }

  struct statvfs buf;
  int r = statvfs(path, &buf);

  if (r)
    {
      printf("WARN: %s: cannot calculate free space\n", path);
    }
  else
    {
      unsigned long mbytes = buf.f_bsize*buf.f_bavail/1024/1024;
      double hours = mbytes / MBYTES_PER_HOUR;

      // printf("r=%d, blocksize = %ld, available blocks = %ld bytes=%ld\n", r, buf.f_bsize, buf.f_bavail, buf.f_bsize*buf.f_bavail);
      printf("%s: %.1fGB free; disk full in %.1f hours\n", path, mbytes/1024., hours);
    }
  free(path);
}

static void transfer_frames(struct transfer_params * params)
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
	        print_disk_full_estimate(name);
		fflush(stdout);
		continue;
	    }

	    file = create_file(output_name_format, &name);
	    if (starting)
		printf("INFO: Started recording\n");
	    print_disk_full_estimate(name);
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
    int test_only = 0;

    // Parse arguments.

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:P:t", options, NULL)) != -1)
    {
	switch (opt)
	{
	case 't':
            test_only = 1;
	    break;
	case 'h':
	    free(mixer_host);
	    mixer_host = strdup(optarg);
	    break;
	case 'p':
	    free(mixer_port);
	    mixer_port = strdup(optarg);
	    break;
	case 'P':
	    free(pidfile_name);
	    pidfile_name = strdup(optarg);
	    break;
	case 'H': // --help
	    usage(argv[0]);
	    return 0;
	default:
	    usage(argv[0]);
	    return 2;
	}
    }

    print_disk_full_estimate(output_name_format);
    if (test_only) 
      exit(0);

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
	fprintf(stderr, "%s: output name format not defined or empty\n",
		argv[0]);
	return 2;
    }

    if (pidfile_name)
    {
	FILE* pidf = fopen(pidfile_name, "w");
	if (pidf == NULL)
	{
	    fprintf(stderr, "%s: could not open for writing\n",
			    pidfile_name);
	    return 2;
	}

	fprintf(pidf, "%d\n", (int)getpid());
	fclose(pidf);
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
    printf("INFO: Connected. (Waiting for start record)\n");

    transfer_frames(&params);

    close(params.sock);

    return 0;
}
