/**
 * @file main.c
 * @brief PC client application to test WRC devices
 *
 * $Revision: $
 * $Date: $
 *
 * entry point of the WRC client application
 *
 * Author: Balint Viragh <bviragh@dension.com>
 * Copyright (C) 2011 Dension Audio Systems Kft.
 */

/**
 * @addtogroup WRC
 * @{   
 */

/*================================[ INCLUDES ]===============================*/
/* System headers */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>

#include <pthread.h>

/* external library headers */   

/* Interface of other modules */
#include "log_logger.h"
#include "msg_messages.h"
#include "cam_camera.h"

/*================================[ INTERNAL MACROS ]========================*/

/** system type of the client software */
#define L_U8_SYS_PC             0x00

/** major number of client version */
#define L_U8_VERSION_MAJOR      0
/** minor number of client version */
#define L_U8_VERSION_MINOR      1

/** name of the transmitter (client) */
#define L_ST_TR_NAME            "wrc_client (PC)"
/** name of the WRC device to set */
#define L_ST_WRC_NAME           "wrc device"

/** delimiters used during tokenize command input */
#define L_ST_DELIM  " \t\n"

/** remote TCP port for the control channel */
#define L_U16_TCP_PORT          1984

/** default period time for PCD message sending */
#define L_U32_PCD_PERIOD_US     100000u

/** Default Camera off voltage limit */
#define L_U16_CAM_OFF           5500
/** Default WRC device off voltage limit */
#define L_U16_WRC_OFF           4500
/** Default Channel period values */
#define L_U16_CH_PERIOD         6000u
/** Default Channel failsafe values */
#define L_U16_CH_FAILSAFE       1500u

/** sleep time between WST message checks in [us]*/
#define L_U32_WST_SLEEP_US      100000u
/** timeout for waiting WST message */
#define L_U32_WST_TIMEOUT_US    10000000u

/** maximal number of handled cameras */
#define L_U8_MAX_CAMERA_NUM     255u

/** timeout for BCSA message receiving in seconds */
#define L_F32_BCSA_TIMEOUT_SEC  1.0

/** number of elements the WRC device list is extended in one step */
#define L_U32_BLOCK_WRC_DEV     4u

/*================================[ INTERNAL TYPEDEFS ]======================*/

/** structure type to store channel data (PCD) */
typedef struct {
    uint32_t    u32_period_us;          /**< period time in [us] */
    uint16_t    au16_ch_v[MSG_NUM_CH];  /**< array of channel data to be sent in PCD messages */
} l_s_pcd_t;

/** structure type to store connection information of a WRC */
typedef struct {
    char        st_ip[64];              /**< IP of the WRC */
    int         fd_ctrl;                /**< socket for the control channel (TCP) */
    int         fd_psd;                 /**< socket for periodic status data (PSD) */
    int         fd_pcd;                 /**< socket for periodic channel data (PCD) */
    uint8_t     u8_tr_id;               /**< transmitter ID given by WRCD in WST */
    uint8_t     u8_cam_num;             /**< number of cameras received in WST */
    uint16_t    u16_psd_port;           /**< PSD port number sent in TL */
    uint16_t    u16_pcd_port;           /**< PCD port number received in WST */
    int         b_ctrl_right;           /**< control right flag received in AGR */
} l_s_wrc_t;

/** structure type to store camera related information */
typedef struct {
    uint32_t u32_num;                   /**< number of active camera streams */
    int afd_socks[L_U8_MAX_CAMERA_NUM]; /**< array of camera streaming sockets */
} l_s_camera_t;

/** structure type to store information about a broadcast discovered device */
typedef struct {
    uint8_t u8_hw_ver_major;            /**< major version number of the hardware */
    uint8_t u8_hw_ver_minor;            /**< minor version number of the hardware */
    uint8_t u8_sw_ver_major;            /**< major version number of the software */
    uint8_t u8_sw_ver_minor;            /**< minor version number of the software */
    char st_name[MSG_MAX_NAME_LEN+1];   /**< name of the WRC device */
    char st_serial[MSG_MAX_SERIAL_LEN+1]; /**< serial number of the WRC device */
    char st_ip[MSG_MAX_IP_LEN+1];       /**< IP address of the serial device */
} l_s_wrc_dev_t;

/** structure type to store a list of discovered devices */
typedef struct {
    l_s_wrc_dev_t* sp_list;             /**< array of wrc device descriptors */
    uint32_t u32_len;                   /**< number of elements in the array */
    uint32_t u32_max_len;               /**< maximal length of the array (allocated memory) */
} l_s_wrc_dev_list_t;

/*================================[ INTERNAL FUNCTION PROTOTYPES ]===========*/

/* connect to TCP port of the remote WRC */
static int l_ctrl_connect_f(const char* p_st_ip, uint16_t p_u16_port);
/* disconnect TCP channel */
static int l_ctrl_disconnect_f(void);
/* perform psd start command */
static int l_psd_start_f(uint16_t p_u16_port);
/* perform psd stop command */
static int l_psd_stop_f(void);
/* perform pcd start command */
static int l_pcd_start_f(const char* p_st_ip, uint16_t p_u16_port);
/* perform pcd stop command */
static int l_pcd_stop_f(void);
/* control message receiver thread */
static void* l_tcp_recv_thread_f(void* p_vp_arg);
/* periodic status data message receiver thread */
static void* l_psd_recv_thread_f(void* p_vp_arg);
/* periodic control data message sender thread */
static void* l_pcd_send_thread_f(void* p_vp_arg);
/* start control message receiver thread */
static int l_start_tcp_receiver_f(int p_fd_socket);
/* stop control message receiver thread */
static int l_stop_tcp_receiver_f(int p_fd_socket);
/* start PSD message receiver thread */
static int l_start_psd_receiver_f(int p_fd_socket);
/* stop PSD message receiver thread */
static int l_stop_psd_receiver_f(int p_fd_socket);
/* start PCD message receiver thread */
static int l_start_pcd_sender_f(int p_fd_socket);
/* stop PCD message receiver thread */
static int l_stop_pcd_sender_f(int p_fd_socket);
/* parse ctrl command */
static void l_ctrl_cmd_f(char* p_st_arg);
/* parse pcd command */
static void l_pcd_cmd_f(char* p_st_arg);
/* parse psd command */
static void l_psd_cmd_f(char* p_st_arg);
/* parse bcsd command */
static void l_bcsd_cmd_f(char* p_st_arg);
/* parse Upper Layer commands */
static int l_upper_cmd_f(char* p_st_cmd, char* p_st_arg);
/* perform upper layer init command */
static int l_upper_init_f(const char* p_st_ip, uint8_t p_u8_prio);
/* set PCD value for a channel */
static int l_upper_channel_f(uint8_t p_u8_ch, uint16_t p_u16_value);
/* start or stop camera streaming */
static int l_upper_camera_f(uint8_t p_u8_id, int p_b_start);
/* perform camera starting */
static int l_camera_start_f(uint8_t p_u8_id);
/* perform camera stopping */
static int l_camera_stop_f(uint8_t p_u8_id);
/* start camera streamer thread */
static int l_start_camera_streaming_f(int p_fd_socket);
/* stop camera streamer thread */
static int l_stop_camera_streaming_f(int p_fd_socket);
/* camera stream receiver thread */
static void* l_camera_recv_thread_f(void* p_vp_arg);
/* print help info about the commands */
static void l_help_f(const char* p_st_cmd);
/* add a new element to the list of discovered WRC devices */
static int l_add_to_devlist_f(const msg_s_bcsa_t* p_sp_bcsa, 
                               const msg_s_bcsa_peer_t* p_sp_peer);
/* get an element for device list */
static int l_get_from_devlist_f(uint32_t p_u32_index, l_s_wrc_dev_t* p_sp_dev);
/* clear device list */
static int l_clear_devlist_f(void);
/* free device list */
static void l_free_devlist_f(void);
/* print WRC device info */
static void l_print_wrc_dev_f(const l_s_wrc_dev_t* p_sp_dev);
/* list discovered WRC devices */
static int l_list_devlist_f(void);

/*================================[ INTERNAL GLOBALS ]=======================*/

/** stores actual PCD values */
static l_s_pcd_t l_s_pcd;
/** mutex to protect l_s_pcd */
static pthread_mutex_t l_mutex_pcd = PTHREAD_MUTEX_INITIALIZER;
/** stores connection info about the remote WRC device */
static l_s_wrc_t l_s_wrc = { 
    .fd_ctrl    = -1,
    .fd_psd     = -1,
    .fd_pcd     = -1,
    .u8_tr_id   = 0,
    .u8_cam_num = 0,
    .u16_psd_port = 0,
    .u16_pcd_port = 0,
    .b_ctrl_right = 0,
    .st_ip      = { 0, },
};
/** mutex to protect l_s_wrc */
static pthread_mutex_t l_mutex_wrc = PTHREAD_MUTEX_INITIALIZER;
/** stores the last received control message */
static msg_s_message_t l_s_ctrl_msg;
/** mutex to protect l_s_ctrl_msg */
static pthread_mutex_t l_mutex_ctrl = PTHREAD_MUTEX_INITIALIZER;
/** stores the last received control message */
static msg_s_message_t l_s_psd_msg;
/** mutex to protect l_s_psd_msg */
static pthread_mutex_t l_mutex_psd = PTHREAD_MUTEX_INITIALIZER;
/** stores camera streaming sockets */
static l_s_camera_t l_s_camera;
/** mutex to protect l_s_camera */
static pthread_mutex_t l_mutex_cam = PTHREAD_MUTEX_INITIALIZER;
/** list of discovered WRC devices */
static l_s_wrc_dev_list_t l_s_wrc_dev_list;
/** mutex to protect l_s_wrc_dev_list */
static pthread_mutex_t l_mutex_dev_list = PTHREAD_MUTEX_INITIALIZER;

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ INTERNAL FUNCTION DEFINITIONS ]==========*/

/** Internal function to connect to the remote WRC via TCP
 *
 *  @param  p_st_ip     IP of the remote peer in dot separated string format 
 *  @param  p_u16_port  TCP port number of the remote server
 *  @return zero in case of success.
 *
 *  The function connects to a remote TCP port and 
 *  sets socket in the connection structure.
 */
static int l_ctrl_connect_f(const char* p_st_ip, uint16_t p_u16_port)
{
    int fd_socket;
    int res;
    struct sockaddr_in s_peer;

    fd_socket = socket(PF_INET, SOCK_STREAM, 0);
    if(fd_socket < 0) {
        LOG_ERR("Can not create socket %d : %s", fd_socket, strerror(errno));
        return fd_socket;
    }

    /* initialize sockadd struct */
    memset(&s_peer, 0, sizeof(s_peer));
    s_peer.sin_family = AF_INET;
    s_peer.sin_port = htons(p_u16_port);
    s_peer.sin_addr.s_addr = inet_addr(p_st_ip);

    /* connect to remote server */
    res = connect(fd_socket, (struct sockaddr*) &s_peer, sizeof(s_peer));
    if(res) {
        LOG_ERR("Can not connect to remote server: %s:%d (%d : %s)", 
                p_st_ip, p_u16_port, res, (res < 0) ? strerror(errno) : "");
        (void)close(fd_socket);
        return -1;
    }

    /* set socket in the connection struct */
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    l_s_wrc.fd_ctrl = fd_socket;
    strncpy(l_s_wrc.st_ip, p_st_ip, sizeof(l_s_wrc.st_ip));
    l_s_wrc.st_ip[sizeof(l_s_wrc.st_ip) - 1] = '\0';
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    if(l_start_tcp_receiver_f(fd_socket)) {
        LOG_ERR("Can not start TCP receiver");
        return -1;
    }
    return 0;
}

/** Internal function to disconnect from TCP peer
 *
 *  @return zero in case of success
 *
 *  The function modifies the connections struct.
 */
static int l_ctrl_disconnect_f(void)
{
    int res;
    int fd_ctrl_socket;

    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    fd_ctrl_socket = l_s_wrc.fd_ctrl;
    l_s_wrc.fd_ctrl = -1;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock WRC mutex");
        return -1;
    }
    res = l_stop_tcp_receiver_f(fd_ctrl_socket);
    if(res) {
        LOG_ERR("Can not close TCP socket %d - %s", res, strerror(errno));
        return res;
    }

    return 0;
}

/** Internal function to initialize PSD receiving
 *
 *  @param  p_u16_port  UDP port number in the local machine to bind
 *  @return zero in case of success.
 *
 *  The function creates a socket, binds, starts a thread and
 *  sets socket in the connection struct.
 */
static int l_psd_start_f(uint16_t p_u16_port)
{
    struct sockaddr_in s_psd_peer;
    int fd_psd_socket;
    int res;
    struct sockaddr_in s_udp_sock;
    socklen_t len_sock;
    uint16_t u16_psd_port;

    /* create UDP socket */
    memset(&s_psd_peer, 0, sizeof(s_psd_peer));
    s_psd_peer.sin_family = AF_INET;
    s_psd_peer.sin_port = htons(p_u16_port);
    s_psd_peer.sin_addr.s_addr = htonl(INADDR_ANY);
    fd_psd_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if(fd_psd_socket < 0) {
        LOG_ERR("Can not create socket %d : %s", fd_psd_socket, strerror(errno));
        return -1;
    }
    res = bind(fd_psd_socket, (struct sockaddr*)&s_psd_peer, sizeof(s_psd_peer));
    if(res < 0) {
        LOG_ERR("Can not bind to port [%d] - %d : %s", p_u16_port, res, strerror(errno));
        (void)close(fd_psd_socket);
        return -1;
    }
    res = l_start_psd_receiver_f(fd_psd_socket);
    if(res) {
        LOG_ERR("Can not start PSD receiver (%d)", res);
        (void)close(fd_psd_socket);
        return -1;
    }
    memset(&s_udp_sock, 0, sizeof(s_udp_sock));
    s_udp_sock.sin_family = AF_INET;
    len_sock = sizeof(s_udp_sock);
    res = getsockname(fd_psd_socket, (struct sockaddr*)&s_udp_sock, &len_sock);
    if(res) {
        LOG_ERR("Can not get PSD socket options %d - %s", res, strerror(errno));
        return -1;
    }
    if(len_sock != sizeof(s_udp_sock)) {
        LOG_ERR("Invalid socket length returned %d instead of %d", 
                len_sock, sizeof(s_udp_sock));
        return -1;
    }
    u16_psd_port = ntohs(s_udp_sock.sin_port);
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        (void)close(fd_psd_socket);
        return -1;
    }
    l_s_wrc.fd_psd = fd_psd_socket;
    l_s_wrc.u16_psd_port = u16_psd_port;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock WRC mutex");
        return -1;
    }
    
    return 0;
}

/** Internal function to stop PSD receiving
 *
 *  @return zero in case of success
 *
 *  The function modifies the connection struct.
 */
static int l_psd_stop_f(void)
{
    int res;
    int fd_psd_socket;

    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    fd_psd_socket = l_s_wrc.fd_psd;
    l_s_wrc.fd_psd = -1;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock WRC mutex");
        return -1;
    }
    res = l_stop_psd_receiver_f(fd_psd_socket);
    fd_psd_socket = -1;
    if(res < 0) {
        LOG_ERR("Can not close PSD socket %d : %s", res, strerror(errno));
        return -1;
    }
    
    return 0;
}

/** Internal function to initialize PCD transmission
 *
 *  @param  p_u16_port  UDP port number in the remote server 
 *  @return zero in case of success.
 *
 *  The function creates a socket, starts a transmission thread and
 *  sets socket in the connection struct.
 *  The IP address is read from the connection struct.
 */
static int l_pcd_start_f(const char* p_st_ip, uint16_t p_u16_port)
{
    struct sockaddr_in s_pcd_peer;
    int fd_pcd_socket;
    int res;

    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock wrc mutex");
        return -1;
    }
    /* create UDP socket */
    s_pcd_peer.sin_family = AF_INET;
    s_pcd_peer.sin_port = htons(p_u16_port);
    if(NULL == p_st_ip)
        s_pcd_peer.sin_addr.s_addr = inet_addr(l_s_wrc.st_ip);
    else
        s_pcd_peer.sin_addr.s_addr = inet_addr(p_st_ip);
    fd_pcd_socket = socket(PF_INET, SOCK_DGRAM, 0);
    l_s_wrc.fd_pcd = fd_pcd_socket;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock wrc mutex");
        return -1;
    }
    if(fd_pcd_socket < 0) {
        LOG_ERR("Can not create socket %d : %s", fd_pcd_socket, strerror(errno));
        return -1;
    }
    res = connect(fd_pcd_socket, (struct sockaddr*)&s_pcd_peer, sizeof(s_pcd_peer));
    if(res < 0) {
        LOG_ERR("Can not connect to peer %d : %s", res, strerror(errno));
        return -1;
    }
    res = l_start_pcd_sender_f(fd_pcd_socket);
    if(res) {
        LOG_ERR("Can not start PCD sender (%d)", res);
        return -1;
    }
    return 0;
}

/** Internal function to stop PCD transmission
 *
 *  @return zero in case of success
 *
 *  The function modifies the connection struct.
 */
static int l_pcd_stop_f(void)
{
    int res;
    int fd_pcd_socket;

    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock wrc mutex");
        return -1;
    }
    fd_pcd_socket = l_s_wrc.fd_pcd;
    l_s_wrc.fd_pcd = -1;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock wrc mutex");
        return -1;
    }
    res = l_stop_pcd_sender_f(fd_pcd_socket);
    fd_pcd_socket = -1;
    if(res < 0) {
        LOG_ERR("Can not close PCD socket %d : %s", res, strerror(errno));
        return -1;
    }

    return 0;
}

/** Internal thread to receive messages from the TCP control socket
 *
 *  @param  p_vp_arg    reference to the socket shall be read
 *  @return always NULL
 *
 *  The thread receives and parses messages from the TCP control connection.
 */
static void* l_tcp_recv_thread_f(void* p_vp_arg)
{
    int fd_socket = -1;
    int res;
    msg_s_message_t s_msg;
    
    LOG_INFO("start of tcp receiver thread");

    if(p_vp_arg)
        fd_socket = *((int*)p_vp_arg);

    while(0 <= (res = msg_recv_message_f(fd_socket, &s_msg))) {
        if(pthread_mutex_lock(&l_mutex_ctrl)) {
            LOG_ERR("Can not lock CTRL msg mutex");
            break;
        }
        /* store received message */
        memcpy(&l_s_ctrl_msg, &s_msg, sizeof(l_s_ctrl_msg));
        if(pthread_mutex_unlock(&l_mutex_ctrl)) {
            LOG_ERR("Can not unlock CTRL msg mutex");
            break;
        }
        /* filter for messages */
        if((MSG_CMD_WST == s_msg.u8_cmd) ||
           (MSG_CMD_AGR == s_msg.u8_cmd)) {
            /* WST and AGR messages influences the connection struct */
            if(pthread_mutex_lock(&l_mutex_wrc)) {
                LOG_ERR("Can not lock WRC mutex");
                break;
            }
            if(MSG_CMD_WST == s_msg.u8_cmd) {
                msg_s_wst_t* sp_wst;
                sp_wst = (msg_s_wst_t*)s_msg.au8_body;
                /* store transmitter ID number of cameras and PCD port */
                l_s_wrc.u8_tr_id = sp_wst->u8_id;
                l_s_wrc.u8_cam_num = sp_wst->u8_cn;
                l_s_wrc.u16_pcd_port = sp_wst->u16_pcd_port;
            } else if(MSG_CMD_AGR == s_msg.u8_cmd) {
                msg_s_agr_t* sp_agr;
                sp_agr = (msg_s_agr_t*)s_msg.au8_body;
                /* check AGR notif byte */
                switch(sp_agr->u8_notif) {
                case MSG_U8_NOTIF_GRANTED:
                    /* access granted - enable PCD sending */
                    l_s_wrc.b_ctrl_right = 1;
                    break;
                case MSG_U8_NOTIF_LOST:
                    /* access lost - disable PCD sending */
                    l_s_wrc.b_ctrl_right = 0;
                    break;
                }
            }
            if(pthread_mutex_unlock(&l_mutex_wrc)) {
                LOG_ERR("Can not unlock WRC mutex");
                break;
            }
        }
    }

    LOG_INFO("end of tcp receiver thread");

    pthread_exit(NULL);
    return NULL;
}

/** Internal thread to receive periodic status data (PSD) messages
 *
 *  @param  p_vp_arg    reference to the socket shall be read
 *  @return always NULL
 *
 *  The thread receives receives and parses PSD messages from an UDP socket.
 */
static void* l_psd_recv_thread_f(void* p_vp_arg)
{
    int fd_socket = -1;
    int res;
    msg_s_message_t s_msg;
    
    LOG_INFO("start of PSD receiver thread");

    if(p_vp_arg)
        fd_socket = *((int*)p_vp_arg);

    while(0 <= (res = msg_recv_message_f(fd_socket, &s_msg))) {
        if(pthread_mutex_lock(&l_mutex_psd)) {
            LOG_ERR("Can not lock PSD msg mutex");
            break;
        }
        /* store received message */
        memcpy(&l_s_psd_msg, &s_msg, sizeof(l_s_psd_msg));
        if(pthread_mutex_unlock(&l_mutex_psd)) {
            LOG_ERR("Can not unlock PSD msg mutex");
            break;
        }
    }

    LOG_INFO("end of PSD receiver thread");

    pthread_exit(NULL);
    return NULL;
}

/** Internal thread to send Periodic Channel Data (PCD) messages 
 *
 *  @param  p_vp_arg    reference to the socket shall be written
 *  @return always NULL
 *
 *  The function sends PCD messages with a defined period time.
 */
static void* l_pcd_send_thread_f(void* p_vp_arg)
{
    int fd_socket = -1;
    int res;
    msg_s_pcd_t s_pcd;
    uint32_t u32_delay;
    uint32_t u32_index;
    int b_ctrl_right;
    
    LOG_INFO("start of PCD transmitter thread");

    if(p_vp_arg)
        fd_socket = *((int*)p_vp_arg);

    while(1) {
        /* get control right */
        if(pthread_mutex_lock(&l_mutex_wrc)) {
            LOG_ERR("Can not lock WRC mutex");
            break;
        }
        b_ctrl_right = l_s_wrc.b_ctrl_right;
        if(pthread_mutex_unlock(&l_mutex_wrc)) {
            LOG_ERR("Can not unlock WRC mutex");
            break;
        }
        /* get PCD values */
        if(pthread_mutex_lock(&l_mutex_pcd)) {
            LOG_ERR("Can not lock PCD mutex");
            break;
        }
        u32_delay = l_s_pcd.u32_period_us;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            s_pcd.au16_ch_v[u32_index] = l_s_pcd.au16_ch_v[u32_index];
        }
        if(pthread_mutex_unlock(&l_mutex_pcd)) {
            LOG_ERR("Can not unlock PCD mutex");
            break;
        }
        /* check access control right */
        if(b_ctrl_right) {
            res = msg_send_pcd_f(fd_socket, &s_pcd);
            if(res < 0) {
                LOG_ERR("Can not send PCD");
                break;
            }
        }
        (void)usleep(u32_delay);
    }

    LOG_INFO("end of PCD transmitter thread");

    pthread_exit(NULL);
    return NULL;
}

/** Internal function to start TCP receiver thread
 *
 *  @param  p_fd_socket     socket shall be used in the connection
 *  @return zero in case of success
 *
 *  The function starts TCP control message receiver thread 
 *  using the specified socket.
 */
static int l_start_tcp_receiver_f(int p_fd_socket)
{
    pthread_t thread;
    pthread_attr_t attr;
    static int l_fd_socket;

    l_fd_socket = p_fd_socket;
    if(pthread_attr_init(&attr)) {
        LOG_ERR("pthread attr init error");
        return -1;
    }
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        LOG_ERR("pthread attr set detached error");
        return -1;
    }
    if(pthread_create(&thread, &attr, l_tcp_recv_thread_f, &l_fd_socket)) {
        LOG_ERR("pthread create error");
        return -1;
    }
    return 0;
}

/** Internal function to stop TCP receiver thread 
 *
 *  @param  p_fd_socket     socket used for TCP control messages
 *  return zero in case of success
 *
 *  The function closes the socket that triggers receiver thread to stop.
 */
static int l_stop_tcp_receiver_f(int p_fd_socket) 
{
    int res;
    res = shutdown(p_fd_socket, SHUT_RDWR);
    if(res) {
        LOG_ERR("Can not shutdown TCP socket (%d) %d - %s", 
                p_fd_socket, res, strerror(errno));
        return res;
    }
    return close(p_fd_socket);
}


/** Internal function to start PSD receiver thread
 *
 *  @param  p_fd_socket     socket shall be used in the connection
 *  @return zero in case of success
 *
 *  The function starts Periodic Status Data (PSD) message receiver thread 
 *  using the specified socket.
 */
static int l_start_psd_receiver_f(int p_fd_socket) 
{
    pthread_t thread;
    pthread_attr_t attr;
    static int l_fd_socket;

    l_fd_socket = p_fd_socket;
    if(pthread_attr_init(&attr)) {
        LOG_ERR("pthread attr init error");
        return -1;
    }
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        LOG_ERR("pthread attr set detached error");
        return -1;
    }
    if(pthread_create(&thread, &attr, l_psd_recv_thread_f, &l_fd_socket)) {
        LOG_ERR("pthread create error");
        return -1;
    }
    return 0;
}

/** Internal function to stop PSD receiver thread 
 *
 *  @param  p_fd_socket     socket used for PSD messages
 *  return zero in case of success
 *
 *  The function closes the socket that triggers receiver thread to stop.
 */
static int l_stop_psd_receiver_f(int p_fd_socket) 
{
    return close(p_fd_socket);
}

/** Internal function to start PCD sender thread
 *
 *  @param  p_fd_socket     socket shall be used in the connection
 *  @return zero in case of success
 *
 *  The function starts Periodic Channel Data (PCD) message sender thread 
 *  using the specified socket.
 */
static int l_start_pcd_sender_f(int p_fd_socket) 
{
    pthread_t thread;
    pthread_attr_t attr;
    static int l_fd_socket;

    l_fd_socket = p_fd_socket;
    if(pthread_attr_init(&attr)) {
        LOG_ERR("pthread attr init error");
        return -1;
    }
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        LOG_ERR("pthread attr set detached error");
        return -1;
    }
    if(pthread_create(&thread, &attr, l_pcd_send_thread_f, &l_fd_socket)) {
        LOG_ERR("pthread create error");
        return -1;
    }
    return 0;
}

/** Internal function to stop PCD sender thread 
 *
 *  @param  p_fd_socket     socket used for PSD messages
 *  return zero in case of success
 *
 *  The function closes the socket that triggers receiver thread to stop.
 */
static int l_stop_pcd_sender_f(int p_fd_socket) 
{
    return close(p_fd_socket);
}

/** Internal function to parse a ctrl command
 *
 *  @param  p_st_arg        ctrl command arguments
 *
 *  The function parses the arguments of a ctrl command,
 *  constructs the message and sends this message using the network socket.
 */
static void l_ctrl_cmd_f(char* p_st_arg)
{
    char* cp_cmd;
    char* cp_err;
    int res;
    uint32_t u32_index;
    int fd_ctrl_socket;

    if(NULL == p_st_arg) {
        LOG_ERR("argument is missing from ctrl send command");
        return;
    }
    cp_cmd = strtok(p_st_arg, L_ST_DELIM);
    if(NULL == cp_cmd) {
        LOG_ERR("argument is missing from ctrl send command");
        return;
    }
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return;
    }
    fd_ctrl_socket = l_s_wrc.fd_ctrl;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return;
    }
    if(0 == strcmp(cp_cmd, "TL")) {
        char* cp_sys  = NULL;
        char* cp_version = NULL;
        char* cp_prio = NULL;
        char* cp_name = NULL;
        char* cp_port = NULL;
        char* cp_ver_minor;
        char* cp_ver_major;
        msg_s_tl_t s_tl;

        cp_sys = strtok(NULL, L_ST_DELIM);
        if(cp_sys)
            cp_version = strtok(NULL, L_ST_DELIM);
        if(cp_version)
            cp_prio = strtok(NULL, L_ST_DELIM);
        if(cp_prio) 
            cp_name = strtok(NULL, "\"");
        if(cp_name)
            cp_port = strtok(NULL, L_ST_DELIM);
        
        if(NULL == cp_port) {
            l_help_f("ctrl TL");
            return;
        }
        s_tl.u8_sys = (uint8_t)strtoul(cp_sys, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_sys);
            return;
        }
        cp_ver_major = cp_version;
        cp_ver_minor = strchr(cp_version, '.');
        if(NULL == cp_ver_minor) {
            LOG_ERR("Invalid version format %s", cp_version);
            return;
        }
        *(cp_ver_minor++) = '\0';
        s_tl.au8_version[0] = (uint8_t)strtoul(cp_ver_major, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_ver_major);
            return;
        }
        s_tl.au8_version[1] = (uint8_t)strtoul(cp_ver_minor, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_ver_minor);
            return;
        }
        s_tl.u8_prio = (uint8_t)strtoul(cp_prio, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_prio);
            return;
        }
        memset(s_tl.au8_tr_name, 0, MSG_MAX_NAME_LEN);
        strncpy((char*)s_tl.au8_tr_name, cp_name, MSG_MAX_NAME_LEN);
        s_tl.u16_psd_port = (uint16_t)strtoul(cp_port, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_port);
            return;
        }
        res = msg_send_tl_f(fd_ctrl_socket, &s_tl);
        if(res) {
            LOG_ERR("Can not send TL message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "DCFG")) {
        char* cp_name = NULL;
        char* cp_cam_off = NULL;
        char* cp_wrc_off = NULL;
        msg_s_dcfg_t s_dcfg;

        cp_name = strtok(NULL, "\"");
        if(cp_name) 
            cp_cam_off = strtok(NULL, L_ST_DELIM);
        if(cp_cam_off)
            cp_wrc_off = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_wrc_off) {
            l_help_f("ctrl DCFG");
            return;
        }
        
        memset(s_dcfg.au8_wrc_name, 0, MSG_MAX_NAME_LEN);
        strncpy((char*)s_dcfg.au8_wrc_name, cp_name, MSG_MAX_NAME_LEN);

        s_dcfg.u16_cam_off = (uint16_t)strtoul(cp_cam_off, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_cam_off);
            return;
        }
        s_dcfg.u16_wrc_off = (uint16_t)strtoul(cp_wrc_off, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_wrc_off);
            return;
        }
        res = msg_send_dcfg_f(fd_ctrl_socket, &s_dcfg);
        if(res) {
            LOG_ERR("Can not send DCFG message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "CCFG")) {
        msg_s_ccfg_t s_ccfg;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            char* cp_time;
            
            cp_time = strtok(NULL, L_ST_DELIM);
            if(NULL == cp_time) 
                break;
            s_ccfg.au16_ch_t[u32_index] = (uint16_t)strtoul(cp_time, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_time);
                break;
            }
        }
        if(MSG_NUM_CH != u32_index) {
            l_help_f("ctrl CCFG");
            return;
        }
        res = msg_send_ccfg_f(fd_ctrl_socket, &s_ccfg);
        if(res) {
            LOG_ERR("Can not send CCFG message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "FCFG")) {
        msg_s_fcfg_t s_fcfg;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            char* cp_value;
            
            cp_value = strtok(NULL, L_ST_DELIM);
            if(NULL == cp_value) 
                break;
            s_fcfg.au16_ch_v[u32_index] = (uint16_t)strtoul(cp_value, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_value);
                break;
            }
        }
        if(MSG_NUM_CH != u32_index) {
            l_help_f("ctrl FCFG");
            return;
        }
        res = msg_send_fcfg_f(fd_ctrl_socket, &s_fcfg);
        if(res) {
            LOG_ERR("Can not send FCFG message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "TLR")) {
        res = msg_send_tlr_f(fd_ctrl_socket, NULL);
        if(res) {
            LOG_ERR("Can not send TLR message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "STST")) {
        char* cp_id = NULL;
        char* cp_port = NULL;
        msg_s_stst_t s_stst;

        cp_id = strtok(NULL, L_ST_DELIM);
        if(cp_id)
            cp_port = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_port) {
            l_help_f("ctrl STST");
            return;
        }

        s_stst.u8_id = (uint8_t)strtoul(cp_id, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_id);
            return;
        }
        s_stst.u16_port = (uint16_t)strtoul(cp_port, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_port);
            return;
        }
        res = msg_send_stst_f(fd_ctrl_socket, &s_stst);
        if(res) {
            LOG_ERR("Can not send STST message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "EST")) {
        char* cp_id = NULL;
        msg_s_est_t s_est;

        cp_id = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_id) {
            l_help_f("ctrl EST");
            return;
        }

        s_est.u8_id = (uint8_t)strtoul(cp_id, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_id);
            return;
        }
        res = msg_send_est_f(fd_ctrl_socket, &s_est);
        if(res) {
            LOG_ERR("Can not send EST message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "EXTOUT")) {
        char* cp_dst = NULL;
        msg_s_extout_t s_extout;
        char* cp_data;

        cp_dst = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_dst) {
            l_help_f("ctrl EXTOUT");
            return;
        }

        s_extout.u8_dst = (uint8_t)strtoul(cp_dst, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_dst);
            return;
        }
        
        for(u32_index = 0u; u32_index < MSG_MAX_EXTDATA_LEN; ++u32_index) {
            cp_data = strtok(NULL, L_ST_DELIM);
            if(NULL == cp_data)
                break;
            s_extout.au8_data[u32_index] = (uint8_t)strtoul(cp_data, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_data);
                u32_index--;
                break;
            }
        }
        res = msg_send_extout_f(fd_ctrl_socket, &s_extout, u32_index+1);
        if(res) {
            LOG_ERR("Can not send EXTOUT message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "WCFG")) {
        char* cp_ssid  = NULL;
        char* cp_pass = NULL;
        char* cp_ap_mode = NULL;
        char* cp_security = NULL;
        char* cp_ap_channel = NULL;
        char* cp_ccode = NULL;
        char* cp_dummy = NULL;
        msg_s_wcfg_t s_wcfg;

        cp_ssid = strtok(NULL, "\"");
        if(cp_ssid)
            cp_dummy = strtok(NULL, "\"");
        if(cp_dummy)
            cp_pass = strtok(NULL, "\"");
        if(cp_pass)
            cp_ap_mode = strtok(NULL, L_ST_DELIM);
        if(cp_ap_mode) 
            cp_security = strtok(NULL, L_ST_DELIM);
        if(cp_security)
            cp_ap_channel = strtok(NULL, L_ST_DELIM);
        if(cp_ap_channel)
            cp_ccode = strtok(NULL, "\"");
        
        if(NULL == cp_ccode) {
            l_help_f("ctrl WCFG");
            return;
        }
        memset(s_wcfg.au8_ssid, 0, MSG_MAX_SSID_LEN);
        strncpy((char*)s_wcfg.au8_ssid, cp_ssid, MSG_MAX_SSID_LEN);
        memset(s_wcfg.au8_pass, 0, MSG_MAX_PASS_LEN);
        strncpy((char*)s_wcfg.au8_pass, cp_pass, MSG_MAX_PASS_LEN);
        if((0 == strcasecmp(cp_ap_mode, "ap")) ||
           (0 == strcasecmp(cp_ap_mode, "1"))) {
            s_wcfg.u8_ap_mode = 1u;
        } else if((0 == strcasecmp(cp_ap_mode, "sta")) || 
                  (0 == strcasecmp(cp_ap_mode, "0"))) {
            s_wcfg.u8_ap_mode = 0u;
        } else {
            LOG_ERR("Unknown AP mode %s (it can be ap or sta)", cp_ap_mode);
            return;
        }
        if((0 == strcasecmp(cp_security, "open")) ||
           (0 == strcasecmp(cp_security, "0"))) {
            s_wcfg.u8_security = 0u;
        } else if((0 == strcasecmp(cp_security, "wpa2")) ||
                  (0 == strcasecmp(cp_security, "1"))) {
            s_wcfg.u8_security = 1u;
        } else {
            LOG_ERR("Unknown security mode %s (it can be open or wpa2)", cp_security);
            return;
        }
        s_wcfg.u8_channel = (uint8_t)strtoul(cp_ap_channel, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_ap_channel);
            return;
        }
        memset(s_wcfg.au8_country, 0, MSG_MAX_CCODE_LEN);
        strncpy((char*)s_wcfg.au8_country, cp_ccode, MSG_MAX_CCODE_LEN);
        res = msg_send_wcfg_f(fd_ctrl_socket, &s_wcfg);
        if(res) {
            LOG_ERR("Can not send WCFG message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "AREQ")) {
        char* cp_id = NULL;
        msg_s_areq_t s_areq;

        cp_id = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_id) {
            l_help_f("ctrl AREQ");
            return;
        }

        s_areq.u8_id = (uint8_t)strtoul(cp_id, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_id);
            return;
        }
        res = msg_send_areq_f(fd_ctrl_socket, &s_areq);
        if(res) {
            LOG_ERR("Can not send AREQ message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "FWUP")) {
        char* cp_md5 = NULL;
        msg_s_fwup_t s_fwup;

        cp_md5 = strtok(NULL, "\"");
        if(NULL == cp_md5) {
            l_help_f("ctrl FWUP");
            return;
        }
        
        for(u32_index = 0u; u32_index < MSG_MAX_MD5_LEN; ++u32_index) {
            uint32_t u32_chr_pos = u32_index * 2u;
            char st_byte[3];
            
            st_byte[0] = cp_md5[u32_chr_pos];
            if('\0' == st_byte[0]) 
                break;
            st_byte[1] = cp_md5[u32_chr_pos+1];
            if('\0' == st_byte[1]) 
                break;
            st_byte[2] = '\0';

            s_fwup.au8_md5[u32_index] = (uint8_t)strtoul(st_byte, &cp_err, 16);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", st_byte);
                return;
            }
        }

        if(MSG_MAX_MD5_LEN != u32_index) {
            l_help_f("ctrl FWUP");
            return;
        }

        res = msg_send_fwup_f(fd_ctrl_socket, &s_fwup);
        if(res) {
            LOG_ERR("Can not send FWUP message %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "connect")) {
        char* cp_ip;
        
        cp_ip = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_ip) {
            l_help_f("ctrl connect");
            return;
        }

        res = l_ctrl_connect_f(cp_ip, L_U16_TCP_PORT);
        if(res) {
            LOG_ERR("can not connect to %s:%d (%d - %s)", 
                    cp_ip, L_U16_TCP_PORT, res, strerror(errno));
            return;
        }
    } else if(0 == strcmp(cp_cmd, "close")) {
        res = l_ctrl_disconnect_f();
        if(res) {
            LOG_ERR("Can not disconnect from TCP peer %d", res);
            return;
        }
    } else {
        LOG_ERR("Unknown control command %s", cp_cmd);
        return;
    }
}

/** Internal function to parse a pcd command
 *
 *  @param  p_st_arg        pcd command arguments
 *
 *  The function parses the arguments of a pcd command.
 *  It can start, stop the PCD transmitter thread, or
 *  set the channel values sent in the PCD messages.
 */
static void l_pcd_cmd_f(char* p_st_arg)
{
    char* cp_cmd;
    char* cp_err;
    int res;
    uint32_t u32_index;

    if(NULL == p_st_arg) {
        LOG_ERR("argument is missing from pcd command");
        return;
    }
    cp_cmd = strtok(p_st_arg, L_ST_DELIM);
    if(NULL == cp_cmd) {
        LOG_ERR("argument is missing from pcd command");
        return;
    }
    if(0 == strcmp(cp_cmd, "start")) {
        char* cp_port = NULL;
        char* cp_ip = NULL;
        uint16_t u16_port;

        cp_ip = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_ip) {
            l_help_f("pcd start");
            return;
        }
        cp_port = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_port) {
            cp_port = cp_ip;
            cp_ip = NULL;
        }
        u16_port = (uint16_t)strtoul(cp_port, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_port);
            return;
        }
        res = l_pcd_start_f(cp_ip, u16_port);
        if(res) {
            LOG_ERR("Can not start PCD transmitter %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "stop")) {
        res = l_pcd_stop_f();
        if(res) {
            LOG_ERR("Can not stop PCD transmitter %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "set")) {
        uint16_t au16_ch[MSG_NUM_CH];

        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            char* cp_value;
            
            cp_value = strtok(NULL, L_ST_DELIM);
            if(NULL == cp_value) 
                break;
            au16_ch[u32_index] = (uint16_t)strtoul(cp_value, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_value);
                break;
            }
        }
        if(MSG_NUM_CH != u32_index) {
            l_help_f("pcd set");
            return;
        }
        if(pthread_mutex_lock(&l_mutex_pcd)) {
            LOG_ERR("Can not lock PCD mutex");
            return;
        }
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            l_s_pcd.au16_ch_v[u32_index] = au16_ch[u32_index];
        }
        if(pthread_mutex_unlock(&l_mutex_pcd)) {
            LOG_ERR("Can not unlock PCD mutex");
            return;
        }
    } else if(0 == strcmp(cp_cmd, "period")) {
        char* cp_period = NULL;
        uint32_t u32_period;

        cp_period = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_period) {
            l_help_f("pcd period");
            return;
        }
        u32_period = (uint32_t)strtoul(cp_period, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_period);
            return;
        }
        if(pthread_mutex_lock(&l_mutex_pcd)) {
            LOG_ERR("Can not lock PCD mutex");
            return;
        }
        l_s_pcd.u32_period_us = u32_period;
        if(pthread_mutex_unlock(&l_mutex_pcd)) {
            LOG_ERR("Can not unlock PCD mutex");
            return;
        }
    } else if(0 == strcmp(cp_cmd, "control")) {
        char* cp_right = NULL;
        int b_right;

        cp_right = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_right) {
            l_help_f("pcd control");
            return;
        }
        if(0 == strcmp(cp_right, "enable")) {
            b_right = 1;
        } else if(0 == strcmp(cp_right, "disable")) {
            b_right = 0;
        } else {
            l_help_f("pcd control");
            return;
        }

        if(pthread_mutex_lock(&l_mutex_wrc)) {
            LOG_ERR("Can not lock WRC mutex");
            return;
        }
        l_s_wrc.b_ctrl_right = b_right;
        if(pthread_mutex_unlock(&l_mutex_wrc)) {
            LOG_ERR("Can not unlock WRC mutex");
            return;
        }
    } else {
        LOG_ERR("Unknown pcd command %s", cp_cmd);
        return;
    }
}

/** Internal function to parse a psd command
 *
 *  @param  p_st_arg        psd command arguments
 *
 *  The function parses the arguments of a psd command.
 *  It can start, stop the PSD receiver thread.
 */
static void l_psd_cmd_f(char* p_st_arg)
{
    char* cp_cmd;
    char* cp_err;
    int res;

    if(NULL == p_st_arg) {
        LOG_ERR("argument is missing from psd command");
        return;
    }
    cp_cmd = strtok(p_st_arg, L_ST_DELIM);
    if(NULL == cp_cmd) {
        LOG_ERR("argument is missing from psd command");
        return;
    }
    if(0 == strcmp(cp_cmd, "start")) {
        char* cp_port = NULL;
        uint16_t u16_port;

        cp_port = strtok(NULL, L_ST_DELIM);
        if(NULL == cp_port) {
            l_help_f("psd start");
            return;
        }
        u16_port = (uint16_t)strtoul(cp_port, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_port);
            return;
        }

        res = l_psd_start_f(u16_port);
        if(res) {
            LOG_ERR("Can not start PSD receiving %d", res);
            return;
        }
    } else if(0 == strcmp(cp_cmd, "stop")) {
        res = l_psd_stop_f();
        if(res) {
            LOG_ERR("Can not stop PSD %d", res);
            return;
        }
    } else {
        LOG_ERR("Unknown psd command %s", cp_cmd);
        return;
    }
}

/** Internal function to handle a bcsd command
 *
 *  @param  p_st_arg        psd command arguments
 *
 *  The function sends a Broadcast Service Discovery message
 *  and parses the responses.
 */
static void l_bcsd_cmd_f(/*@unused@*/ char* p_st_arg)
{
    int res;
    int res_recv;
    msg_s_bcsd_t s_bcsd;
    msg_s_bcsa_t s_bcsa;
    msg_s_bcsa_peer_t s_peer;
    int fd_socket;
    struct timeval s_start_time;
    struct timeval s_act_time;
    double f32_diff_time;
    int b_broadcast;
    
    /* clear wrc device list */
    res = l_clear_devlist_f();
    if(res) {
        LOG_ERR("Can not clear WRC device list %d", res);
        return;
    }

    /* create UDP socket for discovery */
    fd_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if(fd_socket < 0) {
        LOG_ERR("Can not create UDP socket for BCSD messages: %d - %s",
                res, strerror(errno));
        return;
    }
    /* set broadcast flag */
    b_broadcast = 1;
    res = setsockopt(fd_socket, SOL_SOCKET, SO_BROADCAST, 
                    (char*)&b_broadcast, sizeof(b_broadcast));
    if(res) {
        LOG_ERR("Can not set broadcast flag: %d - %s", res, strerror(errno));
        (void)close(fd_socket);
        return;
    }

    /* create BCSD message */
    s_bcsd.u8_sys = L_U8_SYS_PC;
    s_bcsd.au8_version[0] = L_U8_VERSION_MAJOR;
    s_bcsd.au8_version[1] = L_U8_VERSION_MINOR;

    /* send BCSD */
    res = msg_broadcast_bcsd_f(fd_socket, &s_bcsd);
    if(res) {
        LOG_ERR("Can not send broadcast BCSD message %d", res);
        (void)close(fd_socket);
        return;
    }
    
    if(gettimeofday(&s_start_time, NULL)) {
        LOG_ERR("Can not get time");
        (void)close(fd_socket);
        return;
    }
    do {
        res_recv = msg_recv_bcsa_f(fd_socket, &s_bcsa, &s_peer);
        if(0 == res_recv) {
            /* BCSA has been received */
            LOG_INFO("WRC: %s - %s", s_bcsa.au8_wrc_name, s_peer.st_ip);
            res = l_add_to_devlist_f(&s_bcsa, &s_peer);
            if(res) {
                LOG_ERR("Can not add device %s - %s to dev list %d",
                        s_bcsa.au8_wrc_name, s_peer.st_ip, res);
            }
        } else {
            /* check timeout */
            if(gettimeofday(&s_act_time, NULL)) {
                LOG_ERR("Can not get time");
                break;
            }
            f32_diff_time = s_act_time.tv_sec + (s_act_time.tv_usec * 1e-6);
            f32_diff_time -= (s_start_time.tv_sec + (s_start_time.tv_usec * 1e-6));
            if(f32_diff_time > L_F32_BCSA_TIMEOUT_SEC)
                break;
        }
    } while(res_recv >= 0);
    if(res_recv < 0) {
        LOG_ERR("Can not receive BCSA message %d", res_recv);
    }
    /* close UDP socket */
    res = close(fd_socket);
    if(res) {
        LOG_ERR("Can not close BCSD socket: %d - %s", res, strerror(errno));
    }
}

/** Internal function to parse a Upper Layer command
 *
 *  @param  p_st_arg        upper layer command
 *  @param  p_st_arg        upper layer command arguments
 *  @return zero in case of success
 *
 *  The function parses an upper layer command.
 *  Upper layer command performs a sequence of individual message transmissions.
 */
static int l_upper_cmd_f(char* p_st_cmd, char* p_st_arg)
{
    int res;
    char* cp_err;

    if(NULL == p_st_cmd) {
        LOG_ERR("upper layer command is missing");
        return 1;
    }

    if(0 == strcmp(p_st_cmd, "init")) {
        char* cp_ip = NULL;
        char* cp_prio = NULL;
        uint8_t u8_prio = 0u;
        
        if(p_st_arg) 
            cp_ip = strtok(p_st_arg, L_ST_DELIM);
        if(cp_ip)
            cp_prio = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_ip) {
            l_help_f("init");
            return -1;
        }
        if(cp_prio) {
            u8_prio = (uint8_t)strtoul(cp_prio, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_prio);
                return -1;
            }
        }

        res = l_upper_init_f(cp_ip, u8_prio);
        if(res) {
            LOG_ERR("Can not connect to %s %d", cp_ip, res);
            return -1;
        }
    } else if(0 == strcmp(p_st_cmd, "describe")) {
        char* cp_index = NULL;
        l_s_wrc_dev_t s_wrc;
        uint32_t u32_index;

        if(p_st_arg)
            cp_index = strtok(p_st_arg, L_ST_DELIM);
        if(NULL == cp_index) {
            l_help_f("describe");
            res = l_list_devlist_f();
            if(res) {
                LOG_ERR("Can not list discovered WRC devices");
                return -1;
            }
            return 0;
        }

        u32_index = (uint32_t)strtoul(cp_index, &cp_err, 10);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_index);
            return -1;
        }

        res = l_get_from_devlist_f(u32_index, &s_wrc);
        if(res) {
            LOG_ERR("Can not get device with index %d from the list (%d)", 
                    u32_index, res);
            return -1;
        }
        
        /* print device info */
        l_print_wrc_dev_f(&s_wrc);
    } else if(0 == strcmp(p_st_cmd, "connect")) {
        char* cp_index = NULL;
        char* cp_prio = NULL;
        uint8_t u8_prio = 0u;
        l_s_wrc_dev_t s_wrc;
        uint32_t u32_index;

        if(p_st_arg)
            cp_index = strtok(p_st_arg, L_ST_DELIM);
        if(cp_index)
            cp_prio = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_index) {
            l_help_f("connect");
            res = l_list_devlist_f();
            if(res) {
                LOG_ERR("Can not list discovered WRC devices");
                return -1;
            }
            return 0;
        }

        u32_index = (uint32_t)strtoul(cp_index, &cp_err, 10);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_index);
            return -1;
        }
        if(cp_prio) {
            u8_prio = (uint8_t)strtoul(cp_prio, &cp_err, 0);
            if(cp_err && *cp_err) {
                LOG_ERR("%s is not a number", cp_prio);
                return -1;
            }
        }

        res = l_get_from_devlist_f(u32_index, &s_wrc);
        if(res) {
            LOG_ERR("Can not get device with index %d from the list (%d)", 
                    u32_index, res);
            return -1;
        }
        
        res = l_upper_init_f(s_wrc.st_ip, u8_prio);
        if(res) {
            LOG_ERR("Can not connect to %s %d", s_wrc.st_ip, res);
            return -1;
        }
    } else if(0 == strcmp(p_st_cmd, "disconnect")) {
        (void)l_ctrl_disconnect_f();
        (void)l_psd_stop_f();
        (void)l_pcd_stop_f();
    } else if(0 == strcmp(p_st_cmd, "channel")) {
        char* cp_ch = NULL;
        char* cp_value = NULL;
        uint8_t u8_ch;
        uint16_t u16_value;

        if(p_st_arg)
            cp_ch = strtok(p_st_arg, L_ST_DELIM);
        if(cp_ch)
            cp_value = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_value) {
            l_help_f("channel");
            return -1;
        }
        u8_ch = (uint8_t)strtoul(cp_ch, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_ch);
            return -1;
        }
        u16_value = (uint16_t)strtoul(cp_value, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_value);
            return -1;
        }
        
        res = l_upper_channel_f(u8_ch, u16_value);
        if(res) {
            LOG_ERR("Can not set channel %d to %d (%d)", 
                    u8_ch, u16_value, res);
            return -1;
        }
    } else if(0 == strcmp(p_st_cmd, "camera")) {
        char* cp_id = NULL;
        char* cp_start_stop = NULL;
        uint8_t u8_id;
        int b_start;

        if(p_st_arg) 
            cp_id = strtok(p_st_arg, L_ST_DELIM);
        if(cp_id)
            cp_start_stop = strtok(NULL, L_ST_DELIM);

        if(NULL == cp_start_stop) {
            l_help_f("camera");
            return -1;
        }
        u8_id = (uint8_t)strtoul(cp_id, &cp_err, 0);
        if(cp_err && *cp_err) {
            LOG_ERR("%s is not a number", cp_id);
            return -1;
        }
        if(0 == strcasecmp(cp_start_stop, "start")) {
            b_start = 1;
        } else if(0 == strcasecmp(cp_start_stop, "stop")) {
            b_start = 0;
        } else {
            l_help_f("camera");
            return -1;
        }
        res = l_upper_camera_f(u8_id, b_start);
        if(res) {
            LOG_ERR("Can not %s camera %d (%d)", 
                    cp_start_stop, u8_id, res);
            return -1;
        }
    } else {
        return 1;
    }
    return 0;
}

/** Internal function to initialize connection with a WRC 
 *
 *  @param  p_st_ip     IP of the WRC device in dot separated format
 *  @param  p_u8_prio   priority of the transmitter
 *  @return zero in case of success
 *
 *  The function connects to the WRC, logins, starts PSD and PCD threads.
 */
static int l_upper_init_f(const char* p_st_ip, uint8_t p_u8_prio)
{
    int res;
    uint16_t u16_psd_port;
    uint16_t u16_pcd_port;
    int fd_ctrl_socket;
    int fd_psd_socket;
    msg_s_tl_t s_tl;
    msg_s_dcfg_t s_dcfg;
    msg_s_ccfg_t s_ccfg;
    msg_s_fcfg_t s_fcfg;
    uint32_t u32_index;
    uint32_t u32_timeout_us;

    /* start psd thread */
    res = l_psd_start_f(0);
    if(res) {
        LOG_ERR("Can not start PSD %d", res);
        return res;
    }

    /* connect */
    res = l_ctrl_connect_f(p_st_ip, L_U16_TCP_PORT);
    if(res) {
        LOG_ERR("Can not connect to WRC control %s:%d (%d)", 
                p_st_ip, L_U16_TCP_PORT, res);
        return res;
    }

    /* get WRC connection info */
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    fd_ctrl_socket = l_s_wrc.fd_ctrl;
    fd_psd_socket = l_s_wrc.fd_psd;
    u16_psd_port = l_s_wrc.u16_psd_port;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock WRC mutex");
        return -1;
    }
    if(fd_psd_socket < 0) {
        LOG_ERR("Socket not created");
        return -1;
    }
        
    /* send TL use port number from PSD thread */
    s_tl.u8_sys = L_U8_SYS_PC;
    s_tl.au8_version[0] = L_U8_VERSION_MAJOR;
    s_tl.au8_version[1] = L_U8_VERSION_MINOR;
    s_tl.u8_prio = p_u8_prio;
    memset(s_tl.au8_tr_name, 0, MSG_MAX_NAME_LEN);
    strncpy((char*)s_tl.au8_tr_name, L_ST_TR_NAME, MSG_MAX_NAME_LEN);
    s_tl.u16_psd_port = u16_psd_port;
    res = msg_send_tl_f(fd_ctrl_socket, &s_tl);
    if(res) {
        LOG_ERR("Can not send TL %d", res);
        return res;
    }

    /* send DCFG */
    memset(s_dcfg.au8_wrc_name, 0, MSG_MAX_NAME_LEN);
    strncpy((char*)s_dcfg.au8_wrc_name, L_ST_WRC_NAME, MSG_MAX_NAME_LEN);
    s_dcfg.u16_cam_off = L_U16_CAM_OFF;
    s_dcfg.u16_wrc_off = L_U16_WRC_OFF;
/* DCFG message is disabled during initialize process */
#if 0
    res = msg_send_dcfg_f(fd_ctrl_socket, &s_dcfg);
    if(res) {
        LOG_ERR("Can not send DCFG %d", res);
        return res;
    }
#endif

    /* send CCFG */
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        s_ccfg.au16_ch_t[u32_index] = L_U16_CH_PERIOD;
    }
    res = msg_send_ccfg_f(fd_ctrl_socket, &s_ccfg);
    if(res) {
        LOG_ERR("Can not send CCFG %d", res);
        return res;
    }

    /* initialize WST waiting */
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    l_s_wrc.u16_pcd_port = 0;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not unlock WRC mutex");
        return -1;
    }

    /* send FCFG */
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        s_fcfg.au16_ch_v[u32_index] = L_U16_CH_FAILSAFE;
    }
    res = msg_send_fcfg_f(fd_ctrl_socket, &s_fcfg);
    if(res) {
        LOG_ERR("Can not send FCFG %d", res);
        return res;
    }

    /* wait for WST */
    u16_pcd_port = 0u;
    u32_timeout_us = 0u;
    do {
        if(pthread_mutex_lock(&l_mutex_wrc)) {
            l_help_f("pcd start");
            LOG_ERR("Can not lock WRC mutex");
            return -1;
        }
        u16_pcd_port = l_s_wrc.u16_pcd_port;
        if(pthread_mutex_unlock(&l_mutex_wrc)) {
            LOG_ERR("Can not unlock WRC mutex");
            return -1;
        }
        (void)usleep(L_U32_WST_SLEEP_US);
        u32_timeout_us += L_U32_WST_SLEEP_US;
    } while((0 == u16_pcd_port) && (u32_timeout_us < L_U32_WST_TIMEOUT_US));
    if(0 == u16_pcd_port) {
        LOG_ERR("WST wait timeout has been expired (%d sec)", (L_U32_WST_TIMEOUT_US / 1000000));
        return -1;
    }

    /* set default channel states */
    if(pthread_mutex_lock(&l_mutex_pcd)) {
        LOG_ERR("Can not lock PCD mutex");
        return -1;
    }
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        l_s_pcd.au16_ch_v[u32_index] = L_U16_CH_FAILSAFE;
    }
    l_s_pcd.u32_period_us = L_U32_PCD_PERIOD_US;
    if(pthread_mutex_unlock(&l_mutex_pcd)) {
        LOG_ERR("Can not unlock PCD mutex");
        return -1;
    }

    /* start pcd thread using port from WST */
    res = l_pcd_start_f(NULL, u16_pcd_port);
    if(res) {
        LOG_ERR("Can not start PCD thread");
        return -1;
    }

    return 0;
}

/** Internal function to set PCD value of a channel
 *
 *  @param  p_u8_ch     ID of the channel to be set (1..12)
 *  @param  p_u16_value Value to be set to the specified channel
 *  @return zero in case of success
 *
 *  The function checks the range of the channel input.
 *  The function sets the element of the PCD value array to the specified value.
 */
static int l_upper_channel_f(uint8_t p_u8_ch, uint16_t p_u16_value)
{
    uint32_t u32_index;

    if((0u == p_u8_ch) || (p_u8_ch > MSG_NUM_CH)) {
        LOG_ERR("channel identifier (%d) is out of range (1..%d)", 
                p_u8_ch, MSG_NUM_CH);
        return -1;
    }
    u32_index = p_u8_ch - 1u;
    if(pthread_mutex_lock(&l_mutex_pcd)) {
        LOG_ERR("Can not lock PCD mutex");
        return -1;
    }
    l_s_pcd.au16_ch_v[u32_index] = p_u16_value;
    if(pthread_mutex_unlock(&l_mutex_pcd)) {
        LOG_ERR("Can not unlock PCD mutex");
        return -1;
    }

    return 0;
}

/** Internal function to start or stop camera 
 *
 *  @param  p_u8_id     camera ID
 *  @param  p_b_start   0: stop, non-zero: start
 *  @return zero in case of success
 *
 *  The function to start or stop camera streaming.
 */
static int l_upper_camera_f(uint8_t p_u8_id, int p_b_start)
{
    msg_s_stst_t s_stst;
    msg_s_est_t s_est;
    int res;
    int fd_ctrl_socket = -1;
    int fd_cam_socket = -1;
    struct sockaddr_in s_cam_sock;
    socklen_t len_sock;
    uint16_t u16_cam_port;

    /* get control socket */
    if(pthread_mutex_lock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    fd_ctrl_socket = l_s_wrc.fd_ctrl;
    if(pthread_mutex_unlock(&l_mutex_wrc)) {
        LOG_ERR("Can not lock WRC mutex");
        return -1;
    }
    /* execute start or stop */
    if(p_b_start) {
        /* start camera stream receiver */
        res = l_camera_start_f(p_u8_id);
        if(res) {
            LOG_ERR("Can not start camera streaming for %d (%d)", 
                    p_u8_id, res);
            return -1;
        }
        /* get camera socket */
        if(pthread_mutex_lock(&l_mutex_cam)) {
            LOG_ERR("Can not lock Camera mutex");
            return -1;
        }
        if(p_u8_id < L_U8_MAX_CAMERA_NUM) {
            fd_cam_socket = l_s_camera.afd_socks[p_u8_id];
        }
        if(pthread_mutex_unlock(&l_mutex_cam)) {
            LOG_ERR("Can not unlock Camera mutex");
            return -1;
        }
        /* get streaming port */
        memset(&s_cam_sock, 0, sizeof(s_cam_sock));
        s_cam_sock.sin_family = AF_INET;
        len_sock = sizeof(s_cam_sock);
        res = getsockname(fd_cam_socket, (struct sockaddr*)&s_cam_sock, &len_sock);
        if(res) {
            LOG_ERR("Can not get socket info for socket %d: %d - %s", 
                    fd_cam_socket, res, strerror(errno));
            return -1;
        }
        if(len_sock != sizeof(s_cam_sock)) {
            LOG_ERR("Invalid socket length returned %d instead of %d", 
                    len_sock, sizeof(s_cam_sock));
            return -1;
        }
        u16_cam_port = ntohs(s_cam_sock.sin_port);

        /* send STST message */
        s_stst.u8_id = p_u8_id;
        s_stst.u16_port = u16_cam_port;
        res = msg_send_stst_f(fd_ctrl_socket, &s_stst);
        if(res) {
            LOG_ERR("Can not send STST %d", res);
            return -1;
        }
    } else {    /* stop */
        res = l_camera_stop_f(p_u8_id);
        if(res) {
            LOG_ERR("Can not stop camera streaming");
            return -1;
        }
        /* send EST */
        s_est.u8_id = p_u8_id;
        res = msg_send_est_f(fd_ctrl_socket, &s_est);
        if(res) {
            LOG_ERR("Can not send EST message (%d)", res);
            return -1;
        }
    }
    return 0;
}

/** Internal function to handle camera start command 
 *
 *  @param  p_u8_id     ID if the camera shall be started
 *  @return zero in case of success
 *
 *  The function creates an UDP socket starts camera thread and
 *  adds the new socket to the camera socket list.
 */
static int l_camera_start_f(uint8_t p_u8_id)
{
    int res;
    struct sockaddr_in s_cam_sock;
    int fd_cam_socket;

    if(p_u8_id >= L_U8_MAX_CAMERA_NUM) {
        LOG_ERR("Camera ID %d is out of maximal camera number range %d", 
                p_u8_id, L_U8_MAX_CAMERA_NUM);
        return -1;
    }
    /* initialize camera socket */
    memset(&s_cam_sock, 0, sizeof(s_cam_sock));
    s_cam_sock.sin_family = AF_INET;
    s_cam_sock.sin_port = 0;    /* automatic port allocation */
    s_cam_sock.sin_addr.s_addr = htonl(INADDR_ANY);
    fd_cam_socket = socket(PF_INET, SOCK_DGRAM, 0);
    if(fd_cam_socket < 0) {
        LOG_ERR("Can not create camera socket %d - %s", 
                fd_cam_socket, strerror(errno));
        return -1;
    }
    /* bind to the socket */
    res = bind(fd_cam_socket, (struct sockaddr*)&s_cam_sock, sizeof(s_cam_sock));
    if(res) {
        LOG_ERR("Can not bind to camera port %d - %s", res, strerror(errno));
        (void)close(fd_cam_socket);
        return -1;
    }
    /* start camera streaming thread */
    res = l_start_camera_streaming_f(fd_cam_socket);
    if(res) {
        LOG_ERR("Can not start camera stream receiver thread %d", res);
        (void)close(fd_cam_socket);
        return -1;
    }
    /* add socket to the camera socket list */
    if(pthread_mutex_lock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        (void)close(fd_cam_socket);
        return -1;
    }
    if(p_u8_id < L_U8_MAX_CAMERA_NUM) {
        l_s_camera.afd_socks[p_u8_id] = fd_cam_socket;
    }
    if(pthread_mutex_unlock(&l_mutex_cam)) {
        LOG_ERR("Can not unlock Camera mutex");
        (void)close(fd_cam_socket);
        return -1;
    }
    return 0;
}

/** Internal function to stop camera streaming
 *
 *  @param  p_u8_id     ID of the camera shall be stopped
 *  @return zero in case of success
 *
 *  The function stops camera streaming.
 */
static int l_camera_stop_f(uint8_t p_u8_id)
{
    int res;
    int fd_cam_socket = -1;

    if(p_u8_id >= L_U8_MAX_CAMERA_NUM) {
        LOG_ERR("Camera ID %d is out of maximal camera number range %d", 
                p_u8_id, L_U8_MAX_CAMERA_NUM);
        return -1;
    }
    /* add socket to the camera socket list */
    if(pthread_mutex_lock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        (void)close(fd_cam_socket);
        return -1;
    }
    if(p_u8_id < L_U8_MAX_CAMERA_NUM) {
        fd_cam_socket = l_s_camera.afd_socks[p_u8_id];
        l_s_camera.afd_socks[p_u8_id] = -1;
    }
    if(pthread_mutex_unlock(&l_mutex_cam)) {
        LOG_ERR("Can not unlock Camera mutex");
        (void)close(fd_cam_socket);
        return -1;
    }
    res = l_stop_camera_streaming_f(fd_cam_socket);
    if(res) {
        LOG_ERR("Can not stop %d. camera streaming %d", p_u8_id, res);
        return -1;
    }
    return 0;
}

/** Internal function to start Camera stream receiver thread
 *
 *  @param  p_fd_socket     socket shall be used in the connection
 *  @return zero in case of success
 *
 *  The function starts Camera stream receiver thread 
 *  using the specified socket.
 */
static int l_start_camera_streaming_f(int p_fd_socket) 
{
    pthread_t thread;
    pthread_attr_t attr;
    static int l_fd_socket;

    l_fd_socket = p_fd_socket;
    if(pthread_attr_init(&attr)) {
        LOG_ERR("pthread attr init error");
        return -1;
    }
    if(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
        LOG_ERR("pthread attr set detached error");
        return -1;
    }
    if(pthread_create(&thread, &attr, l_camera_recv_thread_f, &l_fd_socket)) {
        LOG_ERR("pthread create error");
        return -1;
    }
    return 0;
}

/** Internal function to stop Camera stream receiver thread 
 *
 *  @param  p_fd_socket     socket used for camera stream receiving
 *  return zero in case of success
 *
 *  The function closes the socket that triggers receiver thread to stop.
 */
static int l_stop_camera_streaming_f(int p_fd_socket) 
{
    uint32_t u32_num;
    int return_value;

    return_value = close(p_fd_socket);
    if(pthread_mutex_lock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        pthread_exit(NULL);
        return -1;
    }
    if(l_s_camera.u32_num > 0u)
       u32_num = l_s_camera.u32_num--;
    if(pthread_mutex_unlock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        pthread_exit(NULL);
        return -1;
    }
    if(1u == u32_num)
        cam_stop_f();

    return return_value;
}

/** Internal thread to receive Camera stream
 *
 *  @param  p_vp_arg    reference to the socket shall be read
 *  @return always NULL
 *
 *  The thread receives and processes Camera stream
 */
static void* l_camera_recv_thread_f(void* p_vp_arg)
{
    int fd_socket = -1;
    int res;
    cam_s_handler_t s_cam_handler;
    
    LOG_INFO("start of Camera receiver thread");

    if(p_vp_arg)
        fd_socket = *((int*)p_vp_arg);

    res = cam_stream_init_f(&s_cam_handler);
    if(res) {
        LOG_ERR("Can not initialize camera streaming %d", res);
        pthread_exit(NULL);
        return NULL;
    }
    if(pthread_mutex_lock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        pthread_exit(NULL);
        return NULL;
    }
    l_s_camera.u32_num++;
    if(pthread_mutex_unlock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        pthread_exit(NULL);
        return NULL;
    }

    while(0 <= (res = cam_stream_recv_f(fd_socket, &s_cam_handler))) {
    }

    LOG_INFO("end of Camera receiver thread");

    pthread_exit(NULL);
    return NULL;
}

/** Internal function to print help information about a command
 *
 *  @param  p_st_cmd    command shall be described or NULL
 *
 *  The function prints help message about the specified command, 
 *  or about all commands if the p_st_cmd argument is NULL.
 */
static void l_help_f(const char* p_st_cmd) 
{
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl TL")))
        fprintf(stderr, "ctrl TL sys major.minor prio \"transmitter name\" psd_port\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl DCFG")))
        fprintf(stderr, "ctrl DCFG \"wrc name\" cam_off_V wrc_off_V\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl CCFG")))
        fprintf(stderr, "ctrl CCFG Ch1T Ch2T Ch3T Ch4T Ch5T Ch6T Ch7T Ch8T Ch9T Ch10T Ch11T Ch12T\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl FCFG")))
        fprintf(stderr, "ctrl FCFG Ch1V Ch2V Ch3V Ch4V Ch5V Ch6V Ch7V Ch8V Ch9V Ch10V Ch11V Ch12V\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl WCFG")))
        fprintf(stderr, "ctrl WCFG \"SSID\" \"Pass\" <ap|sta> <open|wpa2> ap_channel \"CountryCode\"\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl TLR")))
        fprintf(stderr, "ctrl TLR\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl AREQ")))
        fprintf(stderr, "ctrl AREQ transmitter_id\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl FWUP")))
        fprintf(stderr, "ctrl FWUP \"md5sum\"\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl STST")))
        fprintf(stderr, "ctrl STST camera_id UDP_port\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl EXTOUT")))
        fprintf(stderr, "ctrl EXTOUT dest_id <max 255 separated byte value>\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl EST")))
        fprintf(stderr, "ctrl EST camera_id\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl connect")))
        fprintf(stderr, "ctrl connect IP\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "ctrl close")))
        fprintf(stderr, "ctrl close\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "pcd start")))
        fprintf(stderr, "pcd start UDP_port\nOR\npcd start IP UDP_port\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "pcd stop")))
        fprintf(stderr, "pcd stop\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "pcd control")))
        fprintf(stderr, "pcd control <enable|disable>\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "pcd set")))
        fprintf(stderr, "pcd set Ch1V Ch2V Ch3V Ch4V Ch5V Ch6V Ch7V Ch8V Ch9V Ch10V Ch11V Ch12V\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "pcd period")))
        fprintf(stderr, "pcd period value_us\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "psd start")))
        fprintf(stderr, "psd start UDP_port\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "psd stop")))
        fprintf(stderr, "psd stop\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "init")))
        fprintf(stderr, "init IP [prio=0]\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "disconnect")))
        fprintf(stderr, "disconnect\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "channel")))
        fprintf(stderr, "channel ch value\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "camera")))
        fprintf(stderr, "camera id <start|stop>\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "connect")))
        fprintf(stderr, "connect [WRC_index] [prio=0]\n");
    if((NULL == p_st_cmd) || 
       (0 == strcasecmp(p_st_cmd, "describe")))
        fprintf(stderr, "describe [WRC_index]\n");
}

/** Internal function to add a new element to the list of discovered WRC devices
 *
 *  @param  p_sp_bcsa   BCSA message body that describes the new WRC device
 *  @param  p_sp_peer   network address of the new WRC device
 *  @return 0 in case of success
 *
 *  The function extends the list of discovered WRC devices with a new element.
 */
static int l_add_to_devlist_f(const msg_s_bcsa_t* p_sp_bcsa, 
                               const msg_s_bcsa_peer_t* p_sp_peer)
{
    int return_value = 0;

    /* check input argument */
    if((NULL == p_sp_bcsa) || (NULL == p_sp_peer)) {
        LOG_ERR("NULL argument for add to dev list");
        return -1;
    }

    /* lock device list mutex */
    if(pthread_mutex_lock(&l_mutex_dev_list)) {
        LOG_ERR("Can not lock device list mutex");
        return -1;
    }

    /* check if new element shall be allocated */
    if(l_s_wrc_dev_list.u32_max_len <= l_s_wrc_dev_list.u32_len) {
        uint32_t u32_new_len;
        l_s_wrc_dev_t* sp_new_list;
        
        u32_new_len = l_s_wrc_dev_list.u32_len + L_U32_BLOCK_WRC_DEV;
        sp_new_list = realloc(l_s_wrc_dev_list.sp_list, 
                              (u32_new_len * sizeof(l_s_wrc_dev_t)));
        if(sp_new_list) {
            l_s_wrc_dev_list.sp_list = sp_new_list;
            l_s_wrc_dev_list.u32_max_len = u32_new_len;
        } else {
            LOG_ERR("Can not allocate new dev list (size: %d)", u32_new_len);
            return_value = -1;
        }
    }

    /* check list size */
    if(l_s_wrc_dev_list.u32_max_len > l_s_wrc_dev_list.u32_len) {
        l_s_wrc_dev_t* sp_new;
        uint32_t u32_index = l_s_wrc_dev_list.u32_len;

        /* set length */
        sp_new = &l_s_wrc_dev_list.sp_list[u32_index];
        l_s_wrc_dev_list.u32_len = u32_index + 1;
        
        /* set values in the new list element */
        sp_new->u8_hw_ver_major = p_sp_bcsa->au8_hw_ver[0];
        sp_new->u8_hw_ver_minor = p_sp_bcsa->au8_hw_ver[1];
        sp_new->u8_sw_ver_major = p_sp_bcsa->au8_sw_ver[0];
        sp_new->u8_sw_ver_minor = p_sp_bcsa->au8_sw_ver[1];
        strncpy(sp_new->st_name, (char*)p_sp_bcsa->au8_wrc_name, MSG_MAX_NAME_LEN);
        sp_new->st_name[MSG_MAX_NAME_LEN] = '\0';
        strncpy(sp_new->st_serial, (char*)p_sp_bcsa->au8_serial, MSG_MAX_SERIAL_LEN);
        sp_new->st_serial[MSG_MAX_SERIAL_LEN] = '\0';
        strncpy(sp_new->st_ip, p_sp_peer->st_ip, MSG_MAX_IP_LEN);
        sp_new->st_ip[MSG_MAX_IP_LEN] = '\0';
    } else {
        LOG_ERR("Can not add new element to device list");
        return_value = -1;
    }
    
    /* unlock device list mutex */
    if(pthread_mutex_unlock(&l_mutex_dev_list)) {
        LOG_ERR("Can not unlock device list mutex");
        return -1;
    }

    return return_value;
}

/** Internal function to get an element from the device list 
 *
 *  @param  p_u32_index index of the device
 *  @param  p_sp_dev    output argument: device descriptor will be stored here
 *  @return 0 in case of success
 *
 *  The function returns a device descriptor in its output argument.
 */
static int l_get_from_devlist_f(uint32_t p_u32_index, l_s_wrc_dev_t* p_sp_dev)
{
    int return_value = 0;

    /* check arguments */
    if(NULL == p_sp_dev) {
        LOG_ERR("NULL argument for get from devlist function");
        return -1;
    }

    /* lock device list mutex */
    if(pthread_mutex_lock(&l_mutex_dev_list)) {
        LOG_ERR("Can not lock device list mutex");
        return -1;
    }

    /* range check */
    if(p_u32_index < l_s_wrc_dev_list.u32_len) {
        memcpy(p_sp_dev, &l_s_wrc_dev_list.sp_list[p_u32_index], sizeof(l_s_wrc_dev_t));
    } else {
        LOG_ERR("Device index %d is out of range %d", 
                p_u32_index, l_s_wrc_dev_list.u32_len);
        return_value = -1;
    }
      
    /* unlock device list mutex */
    if(pthread_mutex_unlock(&l_mutex_dev_list)) {
        LOG_ERR("Can not unlock device list mutex");
        return -1;
    }

    return return_value;
}

/** Internal function to clear elements from the device list 
 *
 *  @return 0 in case of success
 *
 *  The function only clears the item counter of the list, 
 *  but does not free any allocated memory.
 */
static int l_clear_devlist_f(void)
{
    /* lock device list mutex */
    if(pthread_mutex_lock(&l_mutex_dev_list)) {
        LOG_ERR("Can not lock device list mutex");
        return -1;
    }

    l_s_wrc_dev_list.u32_len = 0u;

    /* unlock device list mutex */
    if(pthread_mutex_unlock(&l_mutex_dev_list)) {
        LOG_ERR("Can not unlock device list mutex");
        return -1;
    }
    return 0;
}

/** Internal function to free device list
 *
 *  The function frees allocated memory belongs to the device list.
 */
static void l_free_devlist_f(void)
{
    /* lock device list mutex */
    if(pthread_mutex_lock(&l_mutex_dev_list)) {
        LOG_ERR("Can not lock device list mutex");
        return;
    }

    l_s_wrc_dev_list.u32_len = 0u;
    l_s_wrc_dev_list.u32_max_len = 0u;
    free(l_s_wrc_dev_list.sp_list);
    l_s_wrc_dev_list.sp_list = NULL;

    /* unlock device list mutex */
    if(pthread_mutex_unlock(&l_mutex_dev_list)) {
        LOG_ERR("Can not unlock device list mutex");
        return;
    }
}

/** Internal function to print info about a WRC device
 *
 *  @param  p_sp_dev    descriptor of the WRC device to describe
 *  
 *  The function prints information stored of a WRC device.
 */
static void l_print_wrc_dev_f(const l_s_wrc_dev_t* p_sp_dev)
{
    if(p_sp_dev) {
        fprintf(stderr, "WRC %s:\n", p_sp_dev->st_name);
        fprintf(stderr, "IP: %s\n", p_sp_dev->st_ip);
        fprintf(stderr, "Serial: %s\n", p_sp_dev->st_serial);
        fprintf(stderr, "HW: %d.%d\n", p_sp_dev->u8_hw_ver_major, p_sp_dev->u8_hw_ver_minor);
        fprintf(stderr, "SW: %d.%d\n", p_sp_dev->u8_sw_ver_major, p_sp_dev->u8_sw_ver_minor);
    }
}

/** Internal function to print a list of discovered WRC devices 
 *
 *  @return 0 in case of success
 *
 *  The function prints a list of the WRC devices discovered by 
 *  Broadcast Service Discovery.
 */
static int l_list_devlist_f(void)
{
    uint32_t u32_index;

    /* lock device list mutex */
    if(pthread_mutex_lock(&l_mutex_dev_list)) {
        LOG_ERR("Can not lock device list mutex");
        return -1;
    }
    fprintf(stderr, "[Idx]\tWRC Name  -  Serial\n");
    fprintf(stderr, "---------------------------\n");
    for(u32_index = 0u; u32_index < l_s_wrc_dev_list.u32_len; ++u32_index) {
        l_s_wrc_dev_t* sp_dev = &l_s_wrc_dev_list.sp_list[u32_index];
        fprintf(stderr, "[%d]\t%s - %s\n", u32_index, sp_dev->st_name, sp_dev->st_serial);
    }
    /* unlock device list mutex */
    if(pthread_mutex_unlock(&l_mutex_dev_list)) {
        LOG_ERR("Can not unlock device list mutex");
        return -1;
    }
    return 0;
}

/*================================[ EXPORTED FUNCTION DEFINITIONS ]==========*/

int main(int argc, char* argv[]) 
{
    char st_line[512];
    char* cp_logfile = NULL;
    char* cp_ip = NULL;
    uint32_t u32_index;

    if (isatty(1))
      {
        fprintf(stderr, "Usage:\n%s ... | dvsource-alien ... -\n", argv[0]);
        fprintf(stderr, "> connect 0\n");
	fprintf(stderr, "> camera 0 start\n");
	fprintf(stderr, "\nHINT: the wirc device often fails to initialize its camera.\n");
	fprintf(stderr, "When we connect, the camera light must switch on. If not, \n");
	fprintf(stderr, "disconnect power, and immediatly reconnect.\n");
	return 0;
      }

    fprintf(stderr, "HINT: at the '>' prompt, type the following commands:\n");
    fprintf(stderr, "> connect 0\n");
    fprintf(stderr, "> camera 0 start\n\n\n");

    fprintf(stderr, "WRC client, version: %d.%d\n", 
            L_U8_VERSION_MAJOR, L_U8_VERSION_MINOR);

    if(argc > 1) {
        cp_ip = argv[1];
    } 
    if(argc > 2) {
        cp_logfile = argv[2];
    }

    log_init_f(cp_logfile);
   
    /* initialize camera sockets */
    if(pthread_mutex_lock(&l_mutex_cam)) {
        LOG_ERR("Can not lock Camera mutex");
        return -1;
    }
    for(u32_index = 0u; u32_index < L_U8_MAX_CAMERA_NUM; ++u32_index)
        l_s_camera.afd_socks[u32_index] = -1;
    if(pthread_mutex_unlock(&l_mutex_cam)) {
        LOG_ERR("Can not unlock Camera mutex");
        return -1;
    }

    if(NULL != cp_ip) {
        (void)l_upper_cmd_f("init", cp_ip);
    } else {
        l_bcsd_cmd_f(NULL);
    }

    fprintf(stderr, "> ");
    while(NULL != fgets(st_line, sizeof(st_line), stdin)) {
        char* cp_cmd;
        char* cp_arg;

        cp_cmd = strtok(st_line, " \t\n");
        if(NULL == cp_cmd) {
            fprintf(stderr, "> ");
            continue;
        }
        cp_arg = strtok(NULL, "\n");

        if(0 == strcmp(cp_cmd, "ctrl")) {
            l_ctrl_cmd_f(cp_arg);
        } else if(0 == strcmp(cp_cmd, "pcd")) {
            l_pcd_cmd_f(cp_arg);
        } else if(0 == strcmp(cp_cmd, "psd")) {
            l_psd_cmd_f(cp_arg);
        } else if(0 == strcmp(cp_cmd, "bcsd")) {
            l_bcsd_cmd_f(cp_arg);
        } else if(0 == strcmp(cp_cmd, "quit")) {
            break;
        } else if(0 == strcmp(cp_cmd, "help")) {
            l_help_f(NULL);
        } else {
            /* try upper layer commands */
            if(l_upper_cmd_f(cp_cmd, cp_arg) > 0) {
                fprintf(stderr, "Unknown command: %s\n", cp_cmd);
            }
        }

        fprintf(stderr, "> ");
    }

    fprintf(stderr, "\n");

    l_free_devlist_f();

    return 0;
}

/**@}*/
