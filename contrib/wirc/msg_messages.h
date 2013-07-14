/**
 * @file msg_messages.h
 * @brief WRC message handling
 *
 * $Revision: $
 * $Date: $
 *
 * Defines the WRC message types and function prototypes.
 *
 * Author: Balint Viragh <bviragh@dension.com>
 * Copyright (C) 2011 Dension Audio Systems Kft.
 */

/**
 * @addtogroup WRC
 * @{
 */

/*================================[ HEADER INIT ]============================*/
#ifndef MSG_MESSAGES_H
#define MSG_MESSAGES_H

/*================================[ EXPORTED MACROS ]========================*/

/* message types: */

/** Broadcast Service Discovery Message (BCSD) */
#define MSG_CMD_BCSD    0x01
/** Broadcast Service Advertisement Message (BCSA) */
#define MSG_CMD_BCSA    0x02

/** Transmitter Login Message (TL) */
#define MSG_CMD_TL      0x11
/** Device Configuration Message (DCFG) */
#define MSG_CMD_DCFG    0x12
/** Channel Configuration Message (CCFG) */
#define MSG_CMD_CCFG    0x13
/** Failsafe Configuration Message (FCFG) */
#define MSG_CMD_FCFG    0x14
/** WRCD Startup Message (WST) */
#define MSG_CMD_WST     0x1A

/** Periodical Channel Data Message (PCD) */
#define MSG_CMD_PCD     0x21
/** Periodical Status Data Message (PSD) */
#define MSG_CMD_PSD     0x22

/** WiFi Configuration Message (WCFG) */
#define MSG_CMD_WCFG    0x31
/** Transmitter List Request Message (TLR) */
#define MSG_CMD_TLR     0x32
/** Transmitter List Message (TLST) */
#define MSG_CMD_TLST    0x33
/** Transmitter List End Message (TLEND) */
#define MSG_CMD_TLEND   0x34
/** Access Request Message (AREQ) */
#define MSG_CMD_AREQ    0x35
/** Access Granted Message (AGR) */
#define MSG_CMD_AGR     0x36
/** Firmware Update Message (FWUP) */
#define MSG_CMD_FWUP    0x37

/** Start Stream Message (STST) */
#define MSG_CMD_STST    0x41
/** End Stream Message (EST) */
#define MSG_CMD_EST     0x42

/** Extrenal Out Message (EXTOUT) */
#define MSG_CMD_EXTOUT  0x50

/** Error Message (ERR) */
#define MSG_CMD_ERR     0xFF

/* message length: */

/** Length of Broadcast Service Discovery Message (BCSD) */
#define MSG_LEN_BCSD    0x03
/** Length of Broadcast Service Advertisement Message (BCSA) */
#define MSG_LEN_BCSA    0x4B

/** Length of Transmitter Login Message (TL) */
#define MSG_LEN_TL      0x46
/** Length of Device Configuration Message (DCFG) */
#define MSG_LEN_DCFG    0x44
/** Length of Channel Configuration Message (CCFG) */
#define MSG_LEN_CCFG    0x18
/** Length of Failsafe Configuration Message (FCFG) */
#define MSG_LEN_FCFG    0x18
/** Length of WRCD Startup Message (WST) */
#define MSG_LEN_WST     0x04

/** Length of Periodical Channel Data Message (PCD) */
#define MSG_LEN_PCD     0x18
/** Length of Periodical Status Data Message (PSD) */
#define MSG_LEN_PSD     0x08

/** Length of WiFi Configuration Message (WCFG) */
#define MSG_LEN_WCFG    0x66
/** Length of Transmitter List Request Message (TLR) */
#define MSG_LEN_TLR     0x00
/** Length of Transmitter List Message (TLST) */
#define MSG_LEN_TLST    0x42
/** Length of Transmitter List End Message (TLEND) */
#define MSG_LEN_TLEND   0x00
/** Length of Access Request Message (AREQ) */
#define MSG_LEN_AREQ    0x01
/** Length of Access Granted Message (AGR) */
#define MSG_LEN_AGR     0x43
/** Length of Firmware Update Message (FWUP) */
#define MSG_LEN_FWUP    0x10

/** Length of Start Stream Message (STST) */
#define MSG_LEN_STST    0x03
/** Length of End Stream Message (EST) */
#define MSG_LEN_EST     0x01

/** Length of Error Message (ERR) */
#define MSG_LEN_ERR     0x03

/** Number of control channels */
#define MSG_NUM_CH      12
/** Number of input channels */
#define MSG_NUM_INPUT   4
/** Number of measured battery values */
#define MSG_NUM_BATT    2

/** Length of a WRC / transmitter name */
#define MSG_MAX_NAME_LEN    64
/** Length of a serial number */
#define MSG_MAX_SERIAL_LEN  7
/** Length of a WiFi SSID */
#define MSG_MAX_SSID_LEN    32
/** Length of a WiFi Password */
#define MSG_MAX_PASS_LEN    64
/** Length of a WiFi Country Code */
#define MSG_MAX_CCODE_LEN   3
/** Length of a binary MD5 value */
#define MSG_MAX_MD5_LEN     16
/** Maximal length of external out message */
#define MSG_MAX_EXTDATA_LEN 255

/** Maximal length of an IP address in dot separated string format */
#define MSG_MAX_IP_LEN      15

/** Access granted to the current transmitter notification */
#define MSG_U8_NOTIF_GRANTED    0x00
/** Access denied to to the current transmitter for reply for request */
#define MSG_U8_NOTIF_DENIED     0x01
/** Current transmitter has lost its access control right */
#define MSG_U8_NOTIF_LOST       0x02
/** Access granted notification message to everybody */
#define MSG_U8_NOTIF_NOTE       0x03

/*================================[ EXPORTED TYPEDEFS ]======================*/

#pragma pack(1)

/** structure describes a message */
typedef struct {
    uint8_t     au8_frame[2];       /**< frame start indicator bytes */
    uint8_t     u8_cmd;             /**< command code - message type */
    uint8_t     u8_len;             /**< length of the message body */
    uint8_t     au8_body[256];      /**< message body, length is maximum 256 */
    uint16_t    u16_crc;            /**< CRC of the cmd, len and body fields */
} msg_s_message_t;

/** BCSD message body structure */
typedef struct {
    uint8_t     u8_sys;             /**< system, transmitter type pc, iphone, android */
    uint8_t     au8_version[2];     /**< version number of the transmitter */
} msg_s_bcsd_t;

/** BCSA message body structure */
typedef struct {
    uint8_t     au8_hw_ver[2];                  /**< HW version */
    uint8_t     au8_sw_ver[2];                  /**< SW version */
    uint8_t     au8_wrc_name[MSG_MAX_NAME_LEN]; /**< WRC name, not terminated */
    uint8_t     au8_serial[MSG_MAX_SERIAL_LEN]; /**< serial number, not terminated */
} msg_s_bcsa_t;

/** TL message body structure */
typedef struct {
    uint8_t     u8_sys;                         /**< system, transmitter type */
    uint8_t     au8_version[2];                 /**< version number of the transmitter */
    uint8_t     u8_prio;                        /**< priority of the transmitter, 0 is the lowest*/
    uint8_t     au8_tr_name[MSG_MAX_NAME_LEN];  /**< transmitter name, not terminated */
    uint16_t    u16_psd_port;                   /**< UDP port for PSD status messages */
} msg_s_tl_t;

/** DCFG message body structure */
typedef struct {
    uint8_t     au8_wrc_name[MSG_MAX_NAME_LEN]; /**< WRC name, not terminated */
    uint16_t    u16_cam_off;                    /**< camera switch off voltage limit */
    uint16_t    u16_wrc_off;                    /**< WRC device switch off voltage limit */
} msg_s_dcfg_t;

/** CCFG message body structure */
typedef struct {
    uint16_t    au16_ch_t[MSG_NUM_CH];          /**< repeat period time values for channels */
} msg_s_ccfg_t;

/** FCFG message body structure */
typedef struct {
    uint16_t    au16_ch_v[MSG_NUM_CH];          /**< failsafe values for channels */
} msg_s_fcfg_t;

/** WST message body structure */
typedef struct {
    uint8_t     u8_id;              /**< ID of the successfully connected transmitter */
    uint8_t     u8_cn;              /**< number of available cameras */
    uint16_t    u16_pcd_port;       /**< UDP port for PCD control messages */
} msg_s_wst_t;

/** PCD message body structure */
typedef struct {
    uint16_t    au16_ch_v[MSG_NUM_CH];          /**< control values for the channels */
} msg_s_pcd_t;

/** PSD message body structure */
typedef struct {
    uint16_t    au16_batt[MSG_NUM_BATT];        /**< battery values */
    uint8_t     au8_input[MSG_NUM_INPUT];       /**< input values */
} msg_s_psd_t;

/** WCFG message body structure */
typedef struct {
    uint8_t     au8_ssid[MSG_MAX_SSID_LEN];     /**< WiFi SSID */
    uint8_t     au8_pass[MSG_MAX_PASS_LEN];     /**< WiFi Password */
    uint8_t     u8_ap_mode;                     /**< 1: AP mode, 0: STA mode */
    uint8_t     u8_security;                    /**< 0: open, 1: secure */
    uint8_t     u8_channel;                     /**< WiFi Channel */
    uint8_t     au8_country[MSG_MAX_CCODE_LEN]; /**< Country Code */
} msg_s_wcfg_t;

/** TLR message body structure - empty */

/** TLST message body structure */
typedef struct {
    uint8_t     u8_id;                          /**< ID of the transmitter */
    uint8_t     u8_prio;                        /**< Priority of the transmitter */
    uint8_t     au8_tr_name[MSG_MAX_NAME_LEN];  /**< Transmitter name, not terminated */
} msg_s_tlst_t;

/** TLEND message body structure - empty */

/** AREQ message body structure */
typedef struct {
    uint8_t     u8_id;              /**< ID of the new controller transmitter */
} msg_s_areq_t;

/** AGR message body structure */
typedef struct {
    uint8_t     u8_id;                          /**< ID of the current transmitter */
    uint8_t     u8_prio;                        /**< Priority of the current transmitter */
    uint8_t     au8_tr_name[MSG_MAX_NAME_LEN];  /**< Current transmitter name, not terminated */
    uint8_t     u8_notif;                       /**< notification byte */
} msg_s_agr_t;

/** FWUP message body structure */
typedef struct {
    uint8_t     au8_md5[MSG_MAX_MD5_LEN];       /**< MD5 sum (binary) of the firmware updater file */
} msg_s_fwup_t;

/** STST message body struct */
typedef struct {
    uint8_t     u8_id;              /**< ID of the camera shall be started */
    uint16_t    u16_port;           /**< UDP port where the camera stream is sent */
} msg_s_stst_t;

/** EST message body struct */
typedef struct {
    uint8_t     u8_id;              /**< ID of the camera shall be stopped */
} msg_s_est_t;

/** EXTOUT message body struct */
typedef struct {
    uint8_t     u8_dst;                         /**< external port destination of the message */
    uint8_t     au8_data[MSG_MAX_EXTDATA_LEN];  /**< data shall be sent to the external port */
} msg_s_extout_t;

/** ERR message body struct */
typedef struct {
    uint8_t     u8_cmd;             /**< command code where the error occured */
    uint16_t    u16_err_code;       /**< error code */
} msg_s_err_t;

/** structure type to store peer address */
typedef struct {
    char st_ip[MSG_MAX_IP_LEN + 1]; /**< IP address of the peer */
} msg_s_bcsa_peer_t;

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ EXPORTED FUNCTIONS ]=====================*/

/** Function to receive a message 
 *
 *  @param  p_fd_file   input file descriptor to read message from
 *  @param  p_sp_out    output argument: to store the received message 
 *  @return zero in case of success, < 0 IO error, > 0 packet error
 *  
 *  The function parses a message by reading the input file descriptor.
 *  This is blocking mode function.
 */
int msg_recv_message_f(int p_fd_file, msg_s_message_t* p_sp_out);

/** Function to receive a BCSA message
 *
 *  @param  p_fd_file   input socket to receive a  message 
 *  @param  p_sp_bcsa   output argument: to store the received BCSA message 
 *  @param  p_sp_peer   output argument: to store the address of the remote peer
 *  @return zero in case of success, < 0 IO error, > 0 packet error, or non-blocking
 *  
 *  The function receives BCSA messages.
 *  The function is non-blocking.
 */
int msg_recv_bcsa_f(int p_fd_file, msg_s_bcsa_t* p_sp_bcsa, 
                     msg_s_bcsa_peer_t * p_sp_peer);

/** Function to send a BCSD (Broadcast Service Discovery) message 
 *
 *  @param  p_fd_file   output socket to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a BCSD message and 
 *  sends it via the specified file descriptor as a broadcast message.
 */
int msg_broadcast_bcsd_f(int p_fd_file, const msg_s_bcsd_t* p_sp_arg);

/** Function to send a TL (Transmitter Login) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_tl_f(int p_fd_file, const msg_s_tl_t* p_sp_arg);

/** Function to send a DCFG (Device Configuration) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_dcfg_f(int p_fd_file, const msg_s_dcfg_t* p_sp_arg);

/** Function to send a CCFG (Channel Configuration) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_ccfg_f(int p_fd_file, const msg_s_ccfg_t* p_sp_arg);

/** Function to send a FCFG (Failsafe Configuration) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_fcfg_f(int p_fd_file, const msg_s_fcfg_t* p_sp_arg);

/** Function to send a PCD (Periodic Channel Data) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_pcd_f(int p_fd_file, const msg_s_pcd_t* p_sp_arg);

/** Function to send a WCFG (WiFi Configuration) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_wcfg_f(int p_fd_file, const msg_s_wcfg_t* p_sp_arg);

/** Function to send a TLR (Transmitter List Request) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_tlr_f(int p_fd_file, const void* p_sp_arg);

/** Function to send a AREQ (Access Request) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_areq_f(int p_fd_file, const msg_s_areq_t* p_sp_arg);

/** Function to send a FWUP (Firmware Update) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_fwup_f(int p_fd_file, const msg_s_fwup_t* p_sp_arg);

/** Function to send a STST (Start Stream) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_stst_f(int p_fd_file, const msg_s_stst_t* p_sp_arg);

/** Function to send a EST (End Stream) message 
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_est_f(int p_fd_file, const msg_s_est_t* p_sp_arg);

/** Function to send an EXTOUT (External Out) message
 *
 *  @param  p_fd_file   output file descriptor to write
 *  @param  p_sp_arg    contains the body of the message to be sent
 *  @param  p_u8_len    length of the extout data
 *  @return zero in case of success, or a negative error code
 *
 *  The function constructs a message and sends it via the specified file descriptor.
 */
int msg_send_extout_f(int p_fd_file, const msg_s_extout_t* p_sp_arg, uint8_t p_u8_len);

#endif /* MSG_MESSAGES_H */

/**@}*/
