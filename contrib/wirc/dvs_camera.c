/*
 * dvs_camera.c
 * This device pumps the mjpeg stream to stdout, so that dvsource-alien can consume it.
 *
 * -- written by Jürgen Weigert, based on cam_camera.c
 * (C) 2013 jw@suse.de
 */
/**
 * @file cam_camera.c
 * @brief module to receive and process camera stream
 *
 * $Revision: $
 * $Date: $
 *
 * Author: Balint Viragh <bviragh@dension.com>
 * Copyright (C) 2011 Dension Audio Systems Kft.
 */

/**
 * @addtogroup WRC
 * @{   
 */

#define CHECK_CAMERA_FRAME
#define CHECK_CAMERA_OFFSET

/*================================[ INCLUDES ]===============================*/
/* System headers */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/time.h>

/* Interface of other modules */
#include "log_logger.h"

/* Interface header of this module */
#include "cam_camera.h"

/*================================[ INTERNAL MACROS ]========================*/

/** maximal length of a filename */
#define L_MAX_FILENAME_LEN 127

/** version number of the communication */
#define L_U32_VERSION       0x5503

/** Bit pattern of the LAST flag */
#define L_BIT_LAST          1

/** version value with LAST flag */
#define L_U32_LAST          ((uint32_t)(((L_BIT_LAST) << 16) | (L_U32_VERSION)))

/** macro to return the version number and suppress the flags */
#define L_GET_VERSION(x)    (((uint32_t)((x))) & 0x0000FFFF)
/** expression to check version number */
#define L_IS_VERSION(x)     ((L_GET_VERSION(x)) == L_U32_VERSION)
/** expression to check if the value contains LAST flag */
#define L_IS_LAST(x)        ((((uint32_t)((x))) >> 16) & L_BIT_LAST)

/** maximal frame data can be received */
#define L_U32_MAX_DATA      ((uint32_t)(60*1024u))

/** maximal image size */
#define L_U32_MAX_IMG_SIZE  ((uint32_t)(256*1024u))

/** number of frames used in FPS measurement */
#define L_U32_FPS_FRAMES    10u

/** macro to determine the size of the header of a packet */
#define L_U32_HEADER_SIZE   ((uint32_t) (sizeof(l_s_packet_v3_t) - 1u))

/*================================[ INTERNAL TYPEDEFS ]======================*/

/** structure describes a received packet */
#pragma pack(1)
typedef struct {
    uint32_t u32_version;       /**< version and synchronization flags */
    uint32_t u32_frame_num;     /**< index of the jpeg frame */
    uint32_t u32_offset;        /**< offset of the current data in the jpeg frame */
    uint32_t u32_length;        /**< length of the data field */
    uint8_t  au8_data[1];       /**< data contains a jpeg frame chunk */
} l_s_packet_v3_t;

/*================================[ INTERNAL FUNCTION PROTOTYPES ]===========*/

/* check and process the received packet */
static int l_handle_packet_f(const l_s_packet_v3_t* p_sp_packet, uint32_t p_u32_size,
                             cam_s_handler_t* p_sp_handler);
/* store a frame packet */
static int l_store_f(const l_s_packet_v3_t* p_sp_packet, cam_s_handler_t* p_sp_handler);
/* print status line */
static void l_print_status_f(const cam_s_handler_t* p_sp_handler);
/* caclulate Frames-Per-Sec */
static double l_fps_meas_f(uint32_t p_u32_frames, cam_s_handler_t* p_sp_handler);
/* byte order conversion in a packet */
static void l_ntoh_packet_f(l_s_packet_v3_t* p_sp_packet, uint32_t p_u32_size);

/*================================[ INTERNAL GLOBALS ]=======================*/

/** image buffer to concatenate image fragments */
static uint8_t l_au8_image[L_U32_MAX_IMG_SIZE];

/*================================[ INTERNAL FUNCTION DEFINITIONS ]==========*/

/** Internal function to handle received packet 
 *
 *  @param  p_sp_packet     reference to the received packet 
 *  @param  p_u32_size      the size of the received packet 
 *  @param  p_sp_handler    handler of the camera streaming
 *  @param  0 in case of success, -1 in case of unexpected error, 1 in case of wrong packet.
 */
static int l_handle_packet_f(const l_s_packet_v3_t* p_sp_packet, uint32_t p_u32_size, 
                             cam_s_handler_t* p_sp_handler)
{
    int res;
    uint32_t u32_exp_length;

    /* check argument */
    if(NULL == p_sp_packet) {
        LOG_ERR("NULL argument for packet handler!");
        return -1;
    }

    /* check packet size */
    if(p_u32_size < sizeof(p_sp_packet->u32_version)) {
        LOG_ERR("received packet is too small");
        return 1;
    }

    /* check version */
    if(!L_IS_VERSION(p_sp_packet->u32_version)) {
        LOG_ERR("Version mismatch!");
        return 1;
    }

    /* check if packet is only a header */
    if(sizeof(p_sp_packet->u32_version) == p_u32_size)
        return 0;

    /* check packet size */
    u32_exp_length = p_u32_size - L_U32_HEADER_SIZE;
    if(p_sp_packet->u32_length != u32_exp_length) {
        LOG_ERR("recv size error: length: %d, expected: %d",
                p_sp_packet->u32_length, u32_exp_length);
        return 1;
    }

#ifdef CHECK_CAMERA_FRAME
    /* check frame number */
    if(p_sp_handler->u32_exp_framenum < p_sp_packet->u32_frame_num) {
        LOG_ERR("frame number error: %d expected: %d",
                p_sp_packet->u32_frame_num, p_sp_handler->u32_exp_framenum);
        p_sp_handler->u32_exp_framenum = p_sp_packet->u32_frame_num + 1;
        p_sp_handler->u32_exp_offset = 0u;
        return 1;
    }
#endif

#ifdef CHECK_CAMERA_OFFSET
    /* check offset */
    if(p_sp_handler->u32_exp_offset != p_sp_packet->u32_offset) {
        LOG_ERR("offset error: %d expected: %d in frame %d",
                p_sp_packet->u32_offset, p_sp_handler->u32_exp_offset, 
                p_sp_packet ->u32_frame_num);
        p_sp_handler->u32_exp_framenum = p_sp_packet->u32_frame_num + 1;
        p_sp_handler->u32_exp_offset = 0u;
        return 1;
    }
#endif

    /* calculate next expected */
    if(L_IS_LAST(p_sp_packet->u32_version)) {
        p_sp_handler->u32_exp_framenum++;
        p_sp_handler->u32_exp_offset = 0;
        if(0 == (p_sp_handler->u32_exp_framenum % L_U32_FPS_FRAMES)) {
            p_sp_handler->f32_fps = l_fps_meas_f(L_U32_FPS_FRAMES, p_sp_handler);
        }
    } else {
        p_sp_handler->u32_exp_offset += p_sp_packet->u32_length;
    }

    /* store data */
    res = l_store_f(p_sp_packet, p_sp_handler);
    if(res) 
        return res;

    return 0;
}

/** Internal function to store packet data 
 *
 *  @param  p_sp_packet     packet descriptor 
 *  @param  p_sp_handler    handler of the camera streaming
 *  @return 0 in case of success
 *
 */
static int l_store_f(const l_s_packet_v3_t* p_sp_packet, /*@unused@*/ cam_s_handler_t* p_sp_handler)
{
    uint8_t* u8p_pos;
    uint32_t u32_length;

    u32_length = p_sp_packet->u32_offset + p_sp_packet->u32_length;
    if(u32_length > L_U32_MAX_IMG_SIZE) {
        LOG_ERR("Packet reference is invalid (offs: %d) (len: %d) sum: %d",
                p_sp_packet->u32_offset, p_sp_packet->u32_length, u32_length);
        return -1;
    }
    if(p_sp_packet->u32_length > 0u) {
        u8p_pos = l_au8_image + p_sp_packet->u32_offset;
        memcpy(u8p_pos, p_sp_packet->au8_data, p_sp_packet->u32_length);
    }

    if(L_IS_LAST(p_sp_packet->u32_version)) {
        // fprintf(stderr, "jpeg frame_num=%d len=%d\n", p_sp_packet->u32_frame_num, u32_length);
	if (isatty(1))
	  {
            LOG_ERR("will not stream to your terminal");
	    return -1;
	  }
	printf("\r\n--Ba4oTvQMY8ew04N8dcnM\r\nContent-Type: image/jpeg\r\n\r\n");
	int r = fwrite(l_au8_image, 1, u32_length, stdout);
	if (r < 0)
	  {
            LOG_ERR("fwrite stdout: r=%d, errno=%d\n", r, errno);
	    return -1;
	  }
	if (r != u32_length)
	  {
            LOG_ERR("incomplete fwrite. please fix code. r=%d, expected %d\n", r, u32_length);
	    return -1;
	  }
    }
    return 0;
}

/** Internal function to print status line */
static void l_print_status_f(const cam_s_handler_t* p_sp_handler)
{
#if 0
    double error_rate;

    error_rate = (double) p_sp_handler->u32_error_cnt / p_sp_handler->u32_packet_cnt;
    printf("FPS: %lf, errno: %ld, err.rate: %lf %%        ", 
            p_sp_handler->f32_fps, (long)p_sp_handler->u32_error_cnt, error_rate*100.0);
#endif
}

/** Internal function to measure FPS
 *
 *  @param  p_u32_frames    number of frames have been received since the last call
 *  @param  p_sp_handler    handler of the camera streaming
 *  @return calculated FPS value
 *
 *  FPS = (p_u32_frames) / (curr_time - prev_time).
 */
static double l_fps_meas_f(uint32_t p_u32_frames, cam_s_handler_t* p_sp_handler)
{
    struct timeval curr_time;
    double diff_time;
    double fps;

    /* get current time */
    if(gettimeofday(&curr_time, NULL)) {
        return 0.0;
    }

    /* calculate difference */
    diff_time = curr_time.tv_sec - p_sp_handler->s_prev_time.tv_sec;
    diff_time += (curr_time.tv_usec - p_sp_handler->s_prev_time.tv_usec) / 1e6;

    /* calculate fps */
    if(diff_time > 0)
        fps = (double)p_u32_frames / diff_time;
    else
        fps = 0.0;

    /* store as current time */
    p_sp_handler->s_prev_time = curr_time;

    /* return FPS */
    return fps;
}

/** Internal function to convert the packet fields to host byte order
 *
 *  @param  p_sp_packet     packet shall be converted
 *  @param  p_u32_size      size of the packet shall be converted
 *  
 *  The function converts the fields of a packet.
 *  If the p_u32_size is less than a packet header size 
 *  then only version field is converted.
 */
static void l_ntoh_packet_f(l_s_packet_v3_t* p_sp_packet, uint32_t p_u32_size)
{
    /* check argument */
    if(NULL == p_sp_packet)
        return;

    /* check size */
    if(p_u32_size >= sizeof(p_sp_packet->u32_version)) {
        /* convert version field */
        p_sp_packet->u32_version = ntohl(p_sp_packet->u32_version);
    }
    if(p_u32_size >= L_U32_HEADER_SIZE) {
        /* convert full packet header */
        p_sp_packet->u32_frame_num = ntohl(p_sp_packet->u32_frame_num);
        p_sp_packet->u32_offset = ntohl(p_sp_packet->u32_offset);
        p_sp_packet->u32_length = ntohl(p_sp_packet->u32_length);
    }
}

/*================================[ EXPORTED FUNCTION DEFINITIONS ]==========*/

/* Function to initialize a camera connection */
int cam_stream_init_f(cam_s_handler_t* p_sp_handler)
{
    /* check argument */
    if(NULL == p_sp_handler) {
        LOG_ERR("Camera init argument is NULL");
        return -1;
    }
    /* initialize handler */
    memset(p_sp_handler, 0, sizeof(cam_s_handler_t));

    return 0;
}

/* Function to receive handle MJPEG-stream */
int cam_stream_recv_f(int p_fd_socket, cam_s_handler_t* p_sp_handler)
{
    int return_value = 0;
    uint8_t au8_buffer[L_U32_HEADER_SIZE + L_U32_MAX_DATA];
    l_s_packet_v3_t* sp_packet = (l_s_packet_v3_t*) au8_buffer;
    uint32_t u32_max_size = sizeof(au8_buffer);
    uint32_t u32_size;
    int res;

    /* check arguments */
    if(NULL == p_sp_handler) {
        LOG_ERR("Camera recv argument is NULL");
        return -1;
    }

    /* receive from UDP socket */
    res = recv(p_fd_socket, sp_packet, u32_max_size, 0);
    if(res < 0) {
        LOG_ERR("recv error %d - %s", res, strerror(errno));
        return -1;
    }
    u32_size = res;

    /* byte order convert of the packet */
    l_ntoh_packet_f(sp_packet, u32_size);

    /* handle received packet */
    res = l_handle_packet_f(sp_packet, u32_size, p_sp_handler);
    if(res < 0) {
        LOG_ERR("packet handling error (%d)", res);
    } else if(res > 0) {
        p_sp_handler->u32_error_cnt++;
        return_value = 1;
    }
    p_sp_handler->u32_packet_cnt++;
    l_print_status_f(p_sp_handler);

    return return_value;
}

/* Function to stop Camera stream processing */
void cam_stop_f(void)
{
}
