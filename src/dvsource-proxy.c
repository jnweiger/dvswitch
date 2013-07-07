/* dvsource-proxy.c
 * (C) 2013 jw@suse.de
 * based on dvsource-file.c, which has the following copyright.
 *
 * dvsource-file works like this:
 * - Read the first DIF_SEQUENCE_SIZE from the file to buf. 
 * - Determine the system, pal or ntsc using dv_buffer_system()
 * - if system changes, initialize frame_timestamp = frame_timer_get()
 *   and calculate frame_interval in nanoseconds.
 * - read the remainder of a frame (completing system->size) and append it to buf.
 * - write the entire frame, to the mixer socket
 * - advance the timestamp by frame_interval, call frame_timer_wait()
 * Repeat.
 *
 * This proxy bridges from non DV devices to dvswitch, keeping an eye on 
 * accurate dv frame timing. It will drop or repeat frames as needed.
 *
 * Input sources currently supported: plain file, v4l /dev/video0
 * for this, we need to convert to DV format ourselves.
 * Steps needed:
 * - initialize an avcodec from ffmpeg.
 *
 * - read from v4l2
 * - scale to pal-dv, format convert from packed rgb to planar yuv into an AVFrame 
 * - 
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



static int sighup_seen = 0;
static void sighup()
{
  sighup_seen = 1;
}

struct transfer_params {
    const char *   fallback_filename;
    char *         fallback_buffer;
    char *         buffer;
    int            proxy_sock;
    int            mixer_sock;
};

struct enc_pal_dv
{
  AVCodec *codec;
  AVCodecContext *ctx;
  struct SwsContext *sws_ctx;
  AVFrame *frame;		// source
  int raw_width;
  int raw_height;
  int raw_stride[3];
  int raw_size;
  int seq_count;
  int enc_size;
};

struct enc_pal_dv *encode_pal_dv_init(int w, int h)
{
  struct enc_pal_dv *s = malloc(sizeof(struct enc_pal_dv));

  avcodec_register_all();
  s->codec = avcodec_find_encoder(CODEC_ID_DVVIDEO);
  s->ctx = avcodec_alloc_context();
  avcodec_get_context_defaults(s->ctx);
  // s->ctx->bit_rate = 400000;		// FIXME
  s->ctx->width = 720;			// from dvswitch/srv/dif.c
  s->ctx->height = 576;			// from dvswitch/srv/dif.c
  s->ctx->time_base = (AVRational){1,25};
  // s->ctx->gop_size = 10;		// FIXME one i-frame every 10
  // s->ctx->max_b_frames = 1;		// FIXME
  s->ctx->pix_fmt = PIX_FMT_YUV420P;	// from dvswitch/src/frame.c or PIX_FMT_YUV411P
  s->seq_count = 12;
  s->enc_size  = s->seq_count * DIF_SEQUENCE_SIZE;

  if (avcodec_open(s->ctx, s->codec) < 0) 
    {
      printf("could not open codec DVVIDEO\n");
      exit(1);
    }

  s->frame = avcodec_alloc_frame();
  s->frame->format = s->ctx->pix_fmt;
  s->frame->width  = s->ctx->width;
  s->frame->height = s->ctx->height;
  av_image_alloc(s->frame->data, s->frame->linesize, 
  	         s->frame->width, s->frame->height, s->frame->format, 32);

  s->raw_width = w;
  s->raw_height = h;
  s->raw_size = avpicture_get_size(PIX_FMT_RGB24, w, h);
  s->raw_stride[0] = avpicture_get_size(PIX_FMT_RGB24, w, 1);
  s->raw_stride[2] = s->raw_stride[1] = s->raw_stride[0];

  s->sws_ctx = sws_getContext(w, h, PIX_FMT_RGB24,
  			      s->ctx->width, s->ctx->height, s->ctx->pix_fmt,
                              SWS_FAST_BILINEAR, NULL, NULL, NULL);
  return s;
}

void render_aa_line(char *aa_buf, int aa_width, uint8_t *pgm_line, int pgm_width)
{
  // gray ramps from http://paulbourke.net/dataformats/asciiart/
  // we choose the long ramp, as we want to see change, even in small amounts.
  char gray_ramp[] = "$@B%8&WM#*oahkbdpqwmZO0QLCJUYXzcvunxrjft/\\|()1{}[]?-_+~<>i!lI;:,\"^`'. ";
  // char gray_ramp[10] = " .:-=+*#%@";
  int i;
  double aa_x_scale = (double)pgm_width/(double)aa_width;
  double aa_c_scale = (double)sizeof(gray_ramp)/255.;

  for (i = 0; i < aa_width; i++)
    {
      int x = (int)(i*aa_x_scale+.5);
      unsigned char c = pgm_line[x];
      *aa_buf++ = gray_ramp[(int)(c*aa_c_scale+.5)];
    }
  *aa_buf++ = '\0';
}

int encode_pal_dv(struct enc_pal_dv *enc, char *rgb, char **outp, int print_aa)
{
  const uint8_t *in_rgb[3];
  in_rgb[0] = in_rgb[1] = in_rgb[2] = (uint8_t *)rgb;
  int scaled_h = sws_scale(enc->sws_ctx, in_rgb, enc->raw_stride,
   	       0, enc->raw_height, enc->frame->data, enc->frame->linesize);
  // printf("raw_size = %d\n", enc->raw_size);
  // printf("scaled h = %d\n", scaled_h);
  if (scaled_h != enc->frame->height)
    {
      printf("sws_scale failed: height %d != %d\n", scaled_h, enc->frame->height);
    }

  int ret;

  AVPacket pkt;
  int got_output;

  av_init_packet(&pkt);
  pkt.data = NULL;    // packet data will be allocated by the encoder
  pkt.size = 0;
  ret = avcodec_encode_video2(enc->ctx, &pkt, enc->frame, &got_output);
  if (ret < 0)
  {
     fprintf(stderr, "Error encoding video frame\n");
     return ret;
  }

  // pick a horizontal line vertically centered.
  static char aa_buf[80];
  if (print_aa)
    {
      render_aa_line(aa_buf, 60, enc->frame->data[0]+
	    enc->frame->linesize[0]*(enc->frame->height>>1), enc->frame->width);
      fprintf(stderr, "%s ret=%d got=%d sz=%d\r", aa_buf, ret, got_output, pkt.size);
    }

  return got_output;
}


static void transfer_frames(struct transfer_params * params)
{
  uint64_t frame_timestamp = 0;
  unsigned int frame_interval = 0;

  int ret;

  AVPacket pkt;
  int got_output;

  av_init_packet(&pkt);
  pkt.data = NULL;    // packet data will be allocated by the encoder
  pkt.size = 0;

  frame_timer_init();
  frame_timestamp = frame_timer_get();
  frame_interval = (1000000000 / enc->ctx->time_base->den
			       * enc->ctx->time_base->num);
  for (;;)
    {
      if (write(params->sock, pkt.data, pkt.size) != (ssize_t)pkt.size)
	{
	    perror("ERROR: write");
	    exit(1);
	}
      frame_timestamp += frame_interval;
      frame_timer_wait(frame_timestamp);
    }
}

int main(int argc, char ** argv)
{
    /* Initialise settings from configuration files. */
    dvswitch_read_config(handle_config);

    (void)signal(SIGHUP, &sighup);

    struct transfer_params params;
    params.opt_loop = false;
    params.timings = false;
    params.filename = NULL;

    /* Parse arguments. */

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:lt", options, NULL)) != -1)
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
	case 'l':
	    params.opt_loop = true;
	    break;
	case 'H': /* --help */
	    usage(argv[0]);
	    return 0;
	case 't':
	    params.timings = true;
	    break;
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

    if (optind != argc - 1)
    {
	if (optind == argc)
	{
	    fprintf(stderr, "%s: missing filename\n",
		    argv[0]);
	}
	else
	{
	    fprintf(stderr, "%s: excess argument \"%s\"\n",
		    argv[0], argv[optind + 1]);
	}
	usage(argv[0]);
	return 2;
    }

    signal(SIGPIPE, SIG_IGN);	// make Broken pipe visible in perror write.

    const char * filename = argv[optind];

    /* Prepare to read the file and connect a socket to the mixer. */
    if (strcmp(filename, "-"))
    {
	printf("INFO: Reading from %s\n", filename);
	params.filename = filename;
	params.file = open(filename, O_RDONLY, 0);
    }
    else
    {
	printf("INFO: Reading from STDIN\n");
	params.file = fileno(stdin);
    }
    if (params.file < 0)
    {
	perror("ERROR: open");
	return 1;
    }
    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    params.sock = create_connected_socket(mixer_host, mixer_port);
    assert(params.sock >= 0); /* create_connected_socket() should handle errors */
    if (write(params.sock, GREETING_SOURCE, GREETING_SIZE) != GREETING_SIZE)
    {
	perror("ERROR: write");
	exit(1);
    }
    printf("INFO: Connected.\n");

    transfer_frames(&params);

    close(params.sock);
    close(params.file);

    return 0;
}
