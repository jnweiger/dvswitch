/* dvsource-alien.c
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
 * dvsource-alien bridges from non DV devices to dvswitch, keeping an eye on 
 * accurate dv frame timing. It will drop or repeat frames as needed.
 *
 * Input sources currently supported: plain file, v4l /dev/video0
 * for this, we need to convert to DV format ourselves.
 * Steps needed:
 * - initialize an avcodec from ffmpeg.
 * - initialize v4l2_grab
 *
 * - read from v4l2
 * - scale to pal-dv, format convert from packed rgb to planar yuv into an AVFrame 
 * - encode to dv frame,
 * - stream to mixer.
 */
/* V4L2 video picture grabber
   Copyright (C) 2009 Mauro Carvalho Chehab <mchehab@infradead.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 */
/*
 * http://ffmpeg.org/doxygen/trunk/api-example_8c-source.html
 * http://ffmpeg.org/doxygen/trunk/output-example_8c-source.html#l00329
 * http://ffmpeg.org/doxygen/trunk/libswscale_2utils_8c_source.html#l01562
 * http://ffmpeg.org/doxygen/trunk/doc_2examples_2decoding_encoding_8c-example.html
 * http://dvswitch.alioth.debian.org/wiki/DV_format/
 *
 * BuildReqiures: libv4l-devel
 * BuildReqiures: libffmpeg-devel
 *
 * (C) 2013, jw@suse.de
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
#include <errno.h>

#include <fcntl.h>
#include <getopt.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>


#include "config.h"
#include "dif.h"
#include "frame_timer.h"
#include "protocol.h"
#include "socket.h"

#include <sys/mman.h>
#include <linux/videodev2.h>
#include <libv4l2.h>
#define CLEAR(x) memset(&(x), 0, sizeof(x))

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>		// av_image_alloc()
#include <libavutil/mathematics.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>		// PIX_FMT_YUV420P
#include <libswscale/swscale.h>		// SWS_FAST_BILINEAR 

static struct option options[] = {
    {"host",   1, NULL, 'h'},
    {"port",   1, NULL, 'p'},
    {"geometry", 1, NULL, 'g'},
    {"help",   0, NULL, 'H'},
    {NULL,     0, NULL, 0}
};

static char * mixer_host = NULL;
static char * mixer_port = NULL;
static char * video_geometry = NULL;

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
    else if (strcmp(name, "VIDEO_GEOMETRY") == 0)
    {
	free(video_geometry);
	video_geometry = strdup(value);
    }
}


static void usage(const char * progname)
{
    fprintf(stderr,
	    "\
Usage: %s [-h HOST] [-p PORT] [-g 640x480] [/dev/video0]\n\
       %s [-h HOST] [-p PORT] http://192.168.178.27:8080\n",
	    progname, progname);

    fprintf(stderr, "\n\
dvsource-alien is a synchronizer and format converter for \n\
streaming motion jpeg or v4l2 input.\n\
\n\
The default input is /dev/video0.\n\
But it can also connect to a http video server like the \n\
android 'IP Webcam' application.\n\
\n\
Options:\n\
-g WIDTHxHEIGHT \n\
	Specify the requested video resolution for v4l.\n\
	This will be scaled to fit into the pal-dv format.\n\
-a\n\
	Enable ascii-art preview. One line from the middle of the video\n\
	is rendered as ascii art gray ramp.\n\
\n\
\n");

}


struct buffer {
        void   *start;
        size_t length;
};

static void xioctl(int fh, int request, void *arg)
{
        int r;

        do {
                r = v4l2_ioctl(fh, request, arg);
        } while (r == -1 && ((errno == EINTR) || (errno == EAGAIN)));

        if (r == -1) {
                fprintf(stderr, "error %d, %s\n", errno, strerror(errno));
                exit(EXIT_FAILURE);
        }
}

struct enc_pal_dv
{
  AVCodec *codec;
  AVCodecContext *ctx;
  struct SwsContext *sws_ctx;
  AVFrame *frame;		// source
  AVPacket pkt;			// dest
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

  av_init_packet(&(s->pkt));
  s->pkt.data = NULL;    // packet data will be allocated by the encoder
  s->pkt.size = 0;

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

int encode_pal_dv(struct enc_pal_dv *enc, char *rgb, int print_aa)
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
  int got_output;

  ret = avcodec_encode_video2(enc->ctx, &(enc->pkt), enc->frame, &got_output);
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
      fprintf(stderr, "%s ret=%d got=%d sz=%d\r", aa_buf, ret, got_output, enc->pkt.size);
    }

  return got_output;
}

struct v4l2_grab 
{
  struct v4l2_format              fmt;
  struct v4l2_buffer              buf;
  struct v4l2_requestbuffers      req;
  char                            *dev_name;
  struct buffer                   *buffers;
  int				  fd;
  unsigned int                    n_buffers;
};

// Configure a grabber, connect to a v4l2 device ('/dev/video0' if dev_name == NULL)
// and ask for a frame size of width and height(if they are nonzero)).
// 
// The actual image configuration can be found in the fields
// fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat
struct v4l2_grab *v4l2_grab_init(char *dev_name, unsigned int width, unsigned int height)
{
  struct v4l2_grab 		  *v;
  enum v4l2_buf_type              type;

  v = (struct v4l2_grab *)malloc(sizeof(struct v4l2_grab));
  if (!dev_name) dev_name = "/dev/video0";
  v->dev_name = dev_name;

  v->fd = v4l2_open(v->dev_name, O_RDWR | O_NONBLOCK, 0);
  if (v->fd < 0) {
          fprintf(stderr, "v4l2_open('%s') failed %d\n", v->dev_name, v->fd);
	  perror("Cannot open device");
	  return NULL;
  }

  CLEAR(v->fmt);
  v->fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  v->fmt.fmt.pix.width       = width;
  v->fmt.fmt.pix.height      = height;
  v->fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_RGB24;
  v->fmt.fmt.pix.field       = V4L2_FIELD_INTERLACED;
  xioctl(v->fd, VIDIOC_S_FMT, &(v->fmt));
  if (v->fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_RGB24) {
	  printf("Libv4l didn't accept RGB24 format. Can't proceed.\n");
	  return NULL;
  }

  if ((v->fmt.fmt.pix.width != width) || (v->fmt.fmt.pix.height != height))
	  printf("Warning: driver is sending image at %dx%d\n",
		  v->fmt.fmt.pix.width, v->fmt.fmt.pix.height);

  CLEAR(v->req);
  v->req.count = 2;
  v->req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  v->req.memory = V4L2_MEMORY_MMAP;
  xioctl(v->fd, VIDIOC_REQBUFS, &(v->req));

  v->buffers = calloc(v->req.count, sizeof(*(v->buffers)));
  for (v->n_buffers = 0; v->n_buffers < v->req.count; ++v->n_buffers) {
	  CLEAR(v->buf);

	  v->buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	  v->buf.memory      = V4L2_MEMORY_MMAP;
	  v->buf.index       = v->n_buffers;

	  xioctl(v->fd, VIDIOC_QUERYBUF, &(v->buf));

	  v->buffers[v->n_buffers].length = v->buf.length;
	  v->buffers[v->n_buffers].start = v4l2_mmap(NULL, v->buf.length,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			v->fd, v->buf.m.offset);

	  if (MAP_FAILED == v->buffers[v->n_buffers].start) {
		  perror("mmap");
		  return NULL;
	  }
  }


  unsigned int i;
  for (i = 0; i < v->n_buffers; ++i) {
	  CLEAR(v->buf);
	  v->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	  v->buf.memory = V4L2_MEMORY_MMAP;
	  v->buf.index = i;
	  xioctl(v->fd, VIDIOC_QBUF, &(v->buf));
  }
  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(v->fd, VIDIOC_STREAMON, &type);


  return v;
}

// tear down the grabber, disconnect from v4l2 device
void v4l2_grab_destruct(struct v4l2_grab *v4l)
{
  enum v4l2_buf_type              type;
  unsigned int i;

  type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  xioctl(v4l->fd, VIDIOC_STREAMOFF, &type);
  for (i = 0; i < v4l->n_buffers; ++i)
	  v4l2_munmap(v4l->buffers[i].start, v4l->buffers[i].length);
  v4l2_close(v4l->fd);
}



// waits for the next image to become ready, grab it, return a 
// buffer pointer, and fill in the length pointer lenp.
// Caller is responsible to invalidate the buffer pointer
// with v4l_grab_release()
char *v4l_grab_acquire(struct v4l2_grab *v4l, int *lenp)
{
  fd_set                          fds;
  struct timeval                  tv;
  int                             r;

  do {
	  FD_ZERO(&fds);
	  FD_SET(v4l->fd, &fds);

	  /* Timeout. */
	  tv.tv_sec = 2;
	  tv.tv_usec = 0;

	  r = select(v4l->fd + 1, &fds, NULL, NULL, &tv);
  } while ((r == -1 && (errno = EINTR)));
  if (r == -1) {
	  perror("select");
	  return NULL;
  }

  CLEAR(v4l->buf);
  v4l->buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  v4l->buf.memory = V4L2_MEMORY_MMAP;
  xioctl(v4l->fd, VIDIOC_DQBUF, &(v4l->buf));

  if (lenp) *lenp = v4l->buf.bytesused;

  return v4l->buffers[v4l->buf.index].start;
}


// Invalidate the buffer returned by v4l_grab_acquire()
// Call this after you have processed the image, 
// before you call v4l_grab_acquire() again.
void v4l_grab_release(struct v4l2_grab *v4l)
{
  xioctl(v4l->fd, VIDIOC_QBUF, &(v4l->buf));
}



static int sighup_seen = 0;
static void sighup()
{
  sighup_seen = 1;
}

struct transfer_params {
    struct v4l2_grab *  v4l;
    struct enc_pal_dv * enc;
    const char *        filename;
    int                 proxy_sock;
    int                 mixer_sock;
    bool		timings, aa_preview;
};


static void transfer_frames(struct transfer_params * params)
{
  uint64_t frame_timestamp = 0;
  unsigned int frame_interval = 0;
  unsigned long	seq_num_in = 0;

  frame_timer_init();
  frame_timestamp = frame_timer_get();
  frame_interval = (1000000000 / params->enc->ctx->time_base.den
			       * params->enc->ctx->time_base.num);
  for (;;)
    {
      int grab_len, r;
      char *grab_buf = v4l_grab_acquire(params->v4l, &grab_len);
      if (!grab_buf) return;
      r = encode_pal_dv(params->enc, grab_buf, params->aa_preview && !(seq_num_in & 0x7));
      if (!r) continue;

      if (write(params->mixer_sock, params->enc->pkt.data, params->enc->pkt.size) != (ssize_t)params->enc->pkt.size)
	{
	    perror("ERROR: write");
	    return;
	}
      v4l_grab_release(params->v4l);

      seq_num_in++;
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
    params.timings = false;
    params.filename = NULL;
    params.aa_preview = false;

    /* Parse arguments. */

    int opt;
    while ((opt = getopt_long(argc, argv, "h:p:g:at", options, NULL)) != -1)
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
	case 'g':
	    free(video_geometry);
	    video_geometry = strdup(optarg);
	    break;
	case 'a':
	    params.aa_preview = true;
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

    if (optind < argc)
    {
        params.filename = argv[optind];
    }

    signal(SIGPIPE, SIG_IGN);	// make Broken pipe visible in perror write.

    unsigned int width = 0;
    unsigned int height = 0;
    if (video_geometry)
      {
        width = atoi(video_geometry);
	while (*video_geometry >= '0' && *video_geometry <= '9') video_geometry++;
	while (*video_geometry < '0'  || *video_geometry > '9') video_geometry++;
        height = atoi(video_geometry);
      }
    params.v4l = v4l2_grab_init(NULL, width, height);
    if (!params.v4l) 
      {
	perror("ERROR: v4l2_grab_init");
	exit(EXIT_FAILURE);
      }
    width  = params.v4l->fmt.fmt.pix.width;	// driver may choose different values.
    height = params.v4l->fmt.fmt.pix.height;	// face reality :-)
    params.enc = encode_pal_dv_init(width, height);

    printf("INFO: Connecting to %s:%s\n", mixer_host, mixer_port);
    params.mixer_sock = create_connected_socket(mixer_host, mixer_port);
    assert(params.mixer_sock >= 0); /* create_connected_socket() should handle errors */
    if (write(params.mixer_sock, GREETING_SOURCE, GREETING_SIZE) != GREETING_SIZE)
    {
	perror("ERROR: write");
	exit(1);
    }
    printf("INFO: Connected.\n");

    transfer_frames(&params);

    v4l2_grab_destruct(params.v4l);
    close(params.mixer_sock);

    return 0;
}
