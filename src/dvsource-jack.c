/* Copyright 2007-2009 Ben Hutchings.
 * written 2011 by Robin Gareus <robin@gareus.org> 
 * See the file "COPYING" for licence details.
 */
/* Source that reads audio from JACK and combines with black video */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <getopt.h>
#include <sys/types.h>
#include <unistd.h>
#include <netinet/in.h>

#include <pthread.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

#include "config.h"
#include "dif.h"
#include "pcm.h"
#include "protocol.h"
#include "socket.h"

static struct option options[] = {
    {"host",   1, NULL, 'h'},
    {"port",   1, NULL, 'p'},
    {"system", 1, NULL, 's'},
    {"delay",  1, NULL, 'd'},
    {"help",   0, NULL, 'H'},
    {NULL,     0, NULL, 0}
};

static char * mixer_host = NULL;
static char * mixer_port = NULL;
static int terminate = 0;

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
Usage: %s [-h HOST] [-p PORT] [-s ntsc|pal] \\\n\
           [-d DELAY]\n",
	    progname);
}

struct transfer_params {
    jack_client_t                *j_client;
    jack_port_t                 **j_input_ports;
    jack_default_audio_sample_t **j_in;
    jack_ringbuffer_t            *rb;
    jack_nframes_t                j_sample_rate;
    unsigned int                  j_channel_count;

    pthread_t reader_thread_id;
    pthread_mutex_t reader_thread_lock;
    pthread_cond_t  buffer_ready;
    int             activated;
    long overruns; 

    jack_nframes_t          delay_size;

    const struct dv_system *system;
    enum dv_sample_rate     sample_rate_code;
    int                     sock;
};



/**
 * jack audio process callback
 */
int j_process (jack_nframes_t nframes, void *arg) {
    jack_nframes_t c;
    struct transfer_params *params = (struct transfer_params*) arg;
    const unsigned int nports = params->j_channel_count;

    /* Do nothing until we're ready to begin. */
    if (!params->activated) return 0;

    for(c=0; c< nports; c++) {
	params->j_in[c] = (jack_default_audio_sample_t*) jack_port_get_buffer(params->j_input_ports[c], nframes);
    }
    //j_latency = jack_port_get_total_latency(params->j_client,j_input_port);


    /* interleave - 32 bit for now */
    jack_nframes_t s;
    for(s=0; s<nframes; s++) {
	for(c=0; c< nports; c++) {
	    if (jack_ringbuffer_write(params->rb, (char*) &params->j_in[c][s], sizeof(jack_default_audio_sample_t))
		< sizeof(jack_default_audio_sample_t))
	    {
		    params->overruns++;
		    break;
	    }
	}
    }
#if 1
    /* Tell the writer that there is work to do. */
    if(pthread_mutex_trylock(&params->reader_thread_lock) == 0) {
	pthread_cond_signal(&params->buffer_ready);
	pthread_mutex_unlock(&params->reader_thread_lock);
    }
#endif
  return 0;      
}

void close_jack(struct transfer_params * params) {
    if (params->j_client) {
	jack_deactivate(params->j_client);
	jack_client_close (params->j_client);
    }
    if (params->j_input_ports) free(params->j_input_ports);
    if (params->j_in) free(params->j_in);
    params->j_client = NULL;
    params->j_in = NULL;
    params->j_input_ports = NULL;
    terminate = 1;
}

void j_on_shutdown (void *arg) {
  fprintf(stderr,"recv. shutdown request from jackd.\n");
  close_jack((struct transfer_params *) arg);
}


/**
 * open a client connection to the JACK server 
 */
int init_jack(struct transfer_params * params)
{
    // TODO use client_name etc, from params
    char * client_name = "dvsource";
    const char * server_name = NULL;
    jack_options_t options = JackNullOption;

    jack_status_t status;
    params->j_client = jack_client_open (client_name, options, &status, server_name);
    if (params->j_client == NULL) {
	fprintf (stderr, "jack_client_open() failed, status = 0x%2.0x\n", status);
    if (status & JackServerFailed) {
	fprintf (stderr, "Unable to connect to JACK server\n");
    }
		return -1;
    }
    if (status & JackServerStarted) {
	fprintf (stderr, "JACK server started\n");
    }
    if (status & JackNameNotUnique) {
	client_name = jack_get_client_name(params->j_client);
	fprintf (stderr, "unique name `%s' assigned\n", client_name);
    }

    jack_set_process_callback (params->j_client, j_process, (void*) params);
    jack_on_shutdown (params->j_client, j_on_shutdown, (void*) params);
    params->j_sample_rate=jack_get_sample_rate (params->j_client);

    return 0;
}

/**
 *
 */
int jack_portsetup(struct transfer_params * params) {
    jack_nframes_t i;

    const unsigned int nports = params->j_channel_count;
    const size_t in_size =  nports * sizeof (jack_default_audio_sample_t *);

    params->j_in = (jack_default_audio_sample_t **) malloc (in_size);
    params->j_input_ports = (jack_port_t **) malloc (sizeof (jack_port_t *) * nports);

    for (i=0; i < nports; i++)
    {
	char name[64];
	sprintf (name, "input%i", i+1);
	if ((params->j_input_ports[i] = jack_port_register (params->j_client, name, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0)) == 0)
	{
	    fprintf (stderr, "cannot register input port \"%s\"!\n", name);
	    jack_client_close (params->j_client);
	    params->j_client=NULL;
	    return -1;
	}
    }
// TODO:  autoconnect
#if 0
    for (i = 0; i < nports; i++)
    {
	if (jack_connect (params->client, source_names[i], jack_port_name (params->j_input_ports[i])))
	{
	    fprintf (stderr, "cannot connect input port %s to %s\n", jack_port_name (params->j_input_ports[i]), source_names[i]);
	    jack_client_close (params->client);
	    params->j_client=NULL;
	    return (-1);
	} 
    }
#endif

    return 0;
}


int open_jack(struct transfer_params * params) {
    if (init_jack(params)) {
	close_jack(params);
	return -1;
    }
  if (jack_portsetup(params)) {
	close_jack(params);
	return -1;
    }
  if (jack_activate (params->j_client)) {
	close_jack(params);
	return -1;
    }
  return 0;
}

static void dv_buffer_fill_dummy(uint8_t * buf, const struct dv_system * system)
{
    unsigned seq_num, block_num;
    uint8_t * block = buf;

    for (seq_num = 0; seq_num != system->seq_count; ++seq_num)
    {
	for (block_num = 0; block_num != DIF_BLOCKS_PER_SEQUENCE; ++block_num)
	{
	    block[1] = (seq_num << 4) | 7;

	    if (block_num == 0)
	    {
		// Header
		block[0] = 0x1f;
		block[2] = 0;

		memset(block + DIF_BLOCK_ID_SIZE,
		       0xff, DIF_BLOCK_SIZE - DIF_BLOCK_ID_SIZE);

		// Header pack
		block[DIF_BLOCK_ID_SIZE] = (system == &dv_system_625_50) ? 0xbf : 0x3f;
		int apt = 0; // IEC 61834 only for now
		block[DIF_BLOCK_ID_SIZE + 1] = 0xf8 | apt;
		block[DIF_BLOCK_ID_SIZE + 2] = 0x78 | apt; // audio valid
		block[DIF_BLOCK_ID_SIZE + 3] = 0xf8 | apt; // video invalid
		block[DIF_BLOCK_ID_SIZE + 4] = 0xf8 | apt; // subcode invalid
	    }
	    else if (block_num < 3)
	    {
		// Subcode
		block[0] = 0x3f;
		block[2] = block_num - 1;

		memset(block + DIF_BLOCK_ID_SIZE,
		       0xff, DIF_BLOCK_SIZE - DIF_BLOCK_ID_SIZE);
	    }
	    else if (block_num < 6)
	    {
		// VAUX
		block[0] = 0x56;
		block[2] = block_num - 3;

		memset(block + DIF_BLOCK_ID_SIZE,
		       0xff, DIF_BLOCK_SIZE - DIF_BLOCK_ID_SIZE);

		int offset = 0;
		if (!(seq_num & 1) && block_num == 5)
		    offset = DIF_BLOCK_ID_SIZE;
		else if ((seq_num & 1) && block_num == 3)
		    offset = DIF_BLOCK_ID_SIZE + 9 * DIF_PACK_SIZE;
		if (offset)
		{
		    // VS pack
		    int dsf = (system == &dv_system_625_50) ? 1 : 0;
		    block[offset] = 0x60;
		    block[offset + 3] = 0xc0 | (dsf << 5);
		    // VSC pack
		    block[offset + DIF_PACK_SIZE] = 0x61;
		    block[offset + DIF_PACK_SIZE + 1] = 0x3f;
		    block[offset + DIF_PACK_SIZE + 2] = 0xc8;
		    block[offset + DIF_PACK_SIZE + 3] = 0xfc;
		}
	    }
	    else if (block_num % 16 == 6)
	    {
		// Audio
		block[0] = 0x76;
		block[2] = block_num / 16;

		memset(block + DIF_BLOCK_ID_SIZE, 0xff, DIF_PACK_SIZE);
		memset(block + DIF_BLOCK_ID_SIZE + DIF_PACK_SIZE,
		       0, DIF_BLOCK_SIZE - DIF_BLOCK_ID_SIZE - DIF_PACK_SIZE);
	    }
	    else
	    {
		// Video
		block[0] = 0x96;
		block[2] = (block_num - 7) - (block_num - 7) / 16;

		// A macroblock full of black; no need for overspill
		block[DIF_BLOCK_ID_SIZE] = 0x0f;
		int i;
		// 4 luma blocks of 14 bytes
		for (i = DIF_BLOCK_ID_SIZE + 1; i != DIF_BLOCK_ID_SIZE + 57; i += 14)
		{
		    block[i] = 0x90;
		    block[i + 1] = 0x06;
		    memset(block + i + 2, 0, 14 - 2);
		}
		// 2 chroma blocks of 10 bytes
		for (; i != DIF_BLOCK_SIZE; i += 10)
		{
		    block[i] = 0x00;
		    block[i + 1] = 0x16;
		    memset(block + i + 2, 0, 10 - 2);
		}
	    }

	    block += DIF_BLOCK_SIZE;
	}
    }
}

void *transfer_frames(void *arg)
{
    struct transfer_params *params = (struct transfer_params*) arg;
    static uint8_t buf[DIF_MAX_FRAME_SIZE];
    unsigned serial_num = 0;

    dv_buffer_fill_dummy(buf, params->system);

    pthread_setcanceltype (PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    pthread_mutex_lock (&params->reader_thread_lock);

    /* enable jack processing/buffering now. -- sync point */
    params->activated = 1;
    jack_nframes_t allocsize = 0;
    void *framebuf = NULL;

    while (!terminate)
    {
	const unsigned frame_count =
	    params->system->audio_frame_counts[params->sample_rate_code].std_cycle[
		serial_num % params->system->audio_frame_counts[params->sample_rate_code].std_cycle_len];

	const jack_nframes_t bytes_per_frame = frame_count * sizeof(jack_default_audio_sample_t) * params->j_channel_count;

	if (bytes_per_frame != allocsize) {
	    allocsize = bytes_per_frame;
	    if (framebuf) free(framebuf);
	    framebuf = malloc (bytes_per_frame);
	    printf("[re] allocated framebuffer: %lu bytes \n", (long int)bytes_per_frame);
	}

	while (params->activated &&
		   (jack_ringbuffer_read_space (params->rb) >= bytes_per_frame + params->delay_size)) {

	    jack_ringbuffer_read (params->rb, framebuf, bytes_per_frame);
	    dv_buffer_set_audio(buf, params->sample_rate_code, frame_count, framebuf);

	    if (write(params->sock, buf, params->system->size)
		!= (ssize_t)params->system->size)
	    {
		perror("ERROR: write");
		exit(1);
	    }
	    ++serial_num;
	}

	/* wait until process() signals more data */
	pthread_cond_wait (&params->buffer_ready, &params->reader_thread_lock);
    }
    close(params->sock);
    params->activated = 0;
    free (framebuf);
    pthread_mutex_unlock(&params->reader_thread_lock);
}

int main(int argc, char ** argv)
{
    /* Initialise settings from configuration files. */
    dvswitch_read_config(handle_config);
    char * system_name = NULL;
    double delay = 0.2;

    struct transfer_params params;

    /* init and default params */
    pthread_mutex_init(&params.reader_thread_lock, NULL);
    pthread_cond_init(&params.buffer_ready, NULL);
    params.activated = params.overruns = 0;
    params.j_client = NULL; 
    params.rb = NULL; 
    params.j_in = NULL; 
    params.j_input_ports = NULL;
    params.j_channel_count = 2; /* hardcoded stereo */

    /* Parse arguments. */
    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:s:r:d:", options, NULL)) != -1)
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
	case 's':
	    free(system_name);
	    system_name = strdup(optarg);
	    break;
	case 'd':
	    delay = strtod(optarg, NULL);
	    break;
	case 'H': /* --help */
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

    if (!system_name || !strcasecmp(system_name, "pal"))
    {
	params.system = &dv_system_625_50;
    }
    else if (!strcasecmp(system_name, "ntsc"))
    {
	params.system = &dv_system_525_60;
    }
    else
    {
	fprintf(stderr, "%s: invalid system name \"%s\"\n", argv[0], system_name);
	return 2;
    }

    if (open_jack(&params)) {
	fprintf(stderr, "%s: can not connect to JACK\n", argv[0]);
	return 2;
    }

    if (params.j_sample_rate == 32000)
    {
	params.sample_rate_code = dv_sample_rate_32k;
    }
    else if (params.j_sample_rate == 48000)
    {
	params.sample_rate_code = dv_sample_rate_48k;
    }
    else
    {
	fprintf(stderr, "%s: invalid sample rate %ld (need 48k or 32k SPS)\n", argv[0], (long int) params.j_sample_rate);
	close_jack(&params);
	return 2;
    }

    if (delay >= 0.0)
    {
	params.delay_size = delay 
	    * params.j_sample_rate * params.j_channel_count
	    * sizeof(jack_default_audio_sample_t); 
    }
    else
    {
	fprintf(stderr, "%s: delays do not work that way!\n", argv[0]);
	close_jack(&params);
	return 2;
    }


    if (argc > optind)
    {
	close_jack(&params);
	fprintf(stderr, "%s: excess argument \"%s\"\n",
		argv[0], argv[optind + 1]);
	usage(argv[0]);
	return 2;
    }

    const size_t rbsize = params.delay_size  /* + 1 sec extra buffer: */
	+  params.j_sample_rate * params.j_channel_count
	   * sizeof(jack_default_audio_sample_t); 
    params.rb = jack_ringbuffer_create(rbsize);
    memset(params.rb->buf, 0, rbsize);


    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    params.sock = create_connected_socket(mixer_host, mixer_port);
    assert(params.sock >= 0); /* create_connected_socket() should handle errors */
    if (write(params.sock, GREETING_SOURCE, GREETING_SIZE) != GREETING_SIZE)
    {
	close_jack(&params);
	perror("ERROR: write");
	exit(1);
    }
    printf("INFO: Connected.\n");

    pthread_create(&params.reader_thread_id, NULL, transfer_frames, &params);
    pthread_yield();

    /* TODO: interactively wait here (inc/dec delay, quit, etc) */
    while (!terminate) sleep (1);

    /* terminate and clean up*/

    close_jack(&params);

    if(params.activated) {
	terminate = 1;
	if(pthread_mutex_trylock(&params.reader_thread_lock) == 0) {
	    pthread_cond_signal(&params.buffer_ready);
	    pthread_mutex_unlock(&params.reader_thread_lock);
	}
	pthread_join(params.reader_thread_id, NULL);
    }

    //close(params.sock);
    if (params.rb) jack_ringbuffer_free(params.rb);

    printf("bye. and BTW: there were %li buffer overruns\n", params.overruns);

    exit(0);
    return 0;
}
/* vim: set sw=4 ts=8 sts=4: */
