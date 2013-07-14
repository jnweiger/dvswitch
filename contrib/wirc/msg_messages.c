/**
 * @file msg_messages.c 
 * @brief WRC message handling
 *
 * $Revision: $
 * $Date: $
 *
 * Functions to send and receive WRC messages.
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
#include <errno.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* external library headers */   

/* Interface of other modules */
#include "log_logger.h"

/* Interface header of this module */
#include "msg_messages.h"

/*================================[ INTERNAL MACROS ]========================*/

/** Expected value of the first frame start byte */
#define L_U8_FRAME_START_1  0xAA
/** Expected value of the second frame start byte */
#define L_U8_FRAME_START_2  0xBB

/** function to calculate the total size of a message frame in bytes */
#define L_U32_MSG_LEN(p_sp_packet)  (uint32_t)(((p_sp_packet)->u8_len) + \
                                     (sizeof(msg_s_message_t)) - 256)

/** maximal size of an UDP message packet */
#define L_U32_MAX_PACKET_SIZE   (1<<16)

/** Broadcast service remote UDP port */
#define L_U16_BCS_UDP_PORT      1984

/** maximal length of a dump string */
#define L_U32_MAX_DUMP_LEN  1023

/*================================[ INTERNAL TYPEDEFS ]======================*/

/*================================[ INTERNAL FUNCTION PROTOTYPES ]===========*/

/* calculate message CRC */
static uint16_t l_calc_msg_crc_f(const msg_s_message_t* p_sp_msg);
/* CRC-CCITT calculator */
static inline uint16_t l_crc_ccitt_f(uint16_t p_u16_init, uint8_t p_u8_byte);
/* send message */
static int l_send_message_f(int p_fd_file, msg_s_message_t* p_sp_in);
/* convert byte order of the fields in message body from network to host */
static int l_ntoh_msg_f(msg_s_message_t* p_sp_msg);
/* log packet in raw format */
static void l_dump_raw_packet_f(const msg_s_message_t* p_sp_msg, char* p_st_str, uint32_t p_u32_size);
/* log a packet */
static void l_dump_packet_f(const msg_s_message_t* p_sp_msg, char* p_st_str, uint32_t p_u32_size);
/* read(2) wrapper */
static ssize_t l_safe_read_f(int p_fd, void* p_vp_buf, size_t p_count);

/*================================[ INTERNAL GLOBALS ]=======================*/

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ INTERNAL FUNCTION DEFINITIONS ]==========*/

/** Internal function to calculate CRC for a message 
 *
 *  @param  p_sp_msg    the message the CRC shall be calculated for
 *  @return the calculated CRC16 value
 *
 *  The function calculates CRC-CCITT from the following fields of the message:
 *      - command code
 *      - length
 *      - message body
 */
static uint16_t l_calc_msg_crc_f(const msg_s_message_t* p_sp_msg) 
{
    uint16_t u16_crc = 0u;
    uint8_t u8_index;
    uint8_t u8_length;

    /* add command code to CRC */
    u16_crc = l_crc_ccitt_f(u16_crc, p_sp_msg->u8_cmd);
    /* add length to CRC */
    u16_crc = l_crc_ccitt_f(u16_crc, p_sp_msg->u8_len);
    /* add message body to the calculation */
    for(u8_index = 0u, u8_length = p_sp_msg->u8_len; 
        u8_index < u8_length; ++u8_index) {
        u16_crc = l_crc_ccitt_f(u16_crc, p_sp_msg->au8_body[u8_index]);
    }

    return u16_crc;
}

/** Internal function to calculate CRC-CCIT value
 *
 *  @param  p_u16_init  initial value of the CRC calculator
 *  @param  p_u8_byte   new byte shall be added to the CRC calculation
 *  @return calculated CRC value
 *
 *  The function implements CRC-CCITT calculation:
 *  The CRC polynomial: X^16 + X^12 + X^5 + 1
 */
static inline uint16_t l_crc_ccitt_f(uint16_t p_u16_init, uint8_t p_u8_byte)
{
    static const uint16_t l_au16_ccitt_crc16_table_c[256] = {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
        0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
        0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
        0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
        0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
        0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
        0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
        0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
        0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
        0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
        0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
        0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
        0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
        0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
        0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
        0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
        0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
        0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
        0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
        0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
        0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
        0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
        0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
    };
    uint16_t u16_crc;
    uint8_t u8_idx;
    
    u8_idx = (p_u16_init >> 8) ^ p_u8_byte;
    u16_crc = l_au16_ccitt_crc16_table_c[u8_idx] ^ (p_u16_init << 8);

    return u16_crc;
}

/** Internal function to send a message 
 *
 *  @param  p_fd_file   file descriptor to write
 *  @param  p_sp_in     message to be sent, modified!
 *  @return zero in case of success
 *
 *  The constructs the message and calculates the CRC.
 *  Note that the function modifies the structure referenced by p_sp_in.
 *  The sent message is constructed in p_sp_in message structure.
 */
static int l_send_message_f(int p_fd_file, msg_s_message_t* p_sp_in)
{
    uint16_t u16_crc;
    uint16_t* u16p_crc_pos;
    int res;
    uint32_t u32_msg_len;
    char st_dump[L_U32_MAX_DUMP_LEN+1];

    /* calculate message CRC */
    u16_crc = l_calc_msg_crc_f(p_sp_in);
    u16_crc = htons(u16_crc);
    
    u16p_crc_pos = (uint16_t*)&(p_sp_in->au8_body[p_sp_in->u8_len]);
    *u16p_crc_pos = u16_crc;
    u32_msg_len = L_U32_MSG_LEN(p_sp_in);
    p_sp_in->u16_crc = u16_crc;

    l_dump_raw_packet_f(p_sp_in, st_dump, sizeof(st_dump));
    log_dump_f(LOG_RAW_OUT, p_sp_in->u8_cmd, st_dump);

    res = write(p_fd_file, p_sp_in, u32_msg_len);
    if(res != u32_msg_len) {
        LOG_ERR("writing message (size: %d) returns %d : %s", 
                u32_msg_len, res, (res < 0) ? strerror(errno) : "NULL");
        return (res < 0) ? -1 : -2;
    }
    return 0;
}

/** Internal function to convert message body fields from network to host byte order
 *
 *  @param  p_sp_msg    input-output argument: reference to the message structure to be converted
 *  @return zero in case of success
 *
 *  The function convert appropriate message body fields form network byte order to
 *  host byte order. The conversion type depends on the command code field in the message.
 *  The function checks the command code and message body size.
 */
static int l_ntoh_msg_f(msg_s_message_t* p_sp_msg)
{
    uint32_t u32_index;

    switch(p_sp_msg->u8_cmd) {
    case MSG_CMD_BCSD:
        /* message body length check */
        if(MSG_LEN_BCSD != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_BCSD);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_BCSA:
        /* message body length check */
        if(MSG_LEN_BCSA != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_BCSA);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_TL:
        {
        msg_s_tl_t* sp_tl;
        /* message body length check */
        if(MSG_LEN_TL != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_TL);
            return -2;
        }
        /* convert status port field */
        sp_tl = (msg_s_tl_t*)p_sp_msg->au8_body;
        sp_tl->u16_psd_port = ntohs(sp_tl->u16_psd_port);
        break;
        }
    case MSG_CMD_DCFG:
        {
        msg_s_dcfg_t* sp_dcfg;
        /* message body length check */
        if(MSG_LEN_DCFG != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_DCFG);
            return -2;
        }
        /* convert camera switch off and wrc device switch off voltage limits */
        sp_dcfg = (msg_s_dcfg_t*)p_sp_msg->au8_body;
        sp_dcfg->u16_cam_off = ntohs(sp_dcfg->u16_cam_off);
        sp_dcfg->u16_wrc_off = ntohs(sp_dcfg->u16_wrc_off);
        break;
        }
    case MSG_CMD_CCFG:
        {
        msg_s_ccfg_t* sp_ccfg;
        /* message body length check */
        if(MSG_LEN_CCFG != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_CCFG);
            return -2;
        }
        /* convert channel period time values */
        sp_ccfg = (msg_s_ccfg_t*)p_sp_msg->au8_body;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            sp_ccfg->au16_ch_t[u32_index] = ntohs(sp_ccfg->au16_ch_t[u32_index]);
        }
        break;
        }
    case MSG_CMD_FCFG:
        {
        msg_s_fcfg_t* sp_fcfg;
        /* message body length check */
        if(MSG_LEN_FCFG != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_FCFG);
            return -2;
        }
        /* convert channel failsafe values */
        sp_fcfg = (msg_s_fcfg_t*)p_sp_msg->au8_body;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            sp_fcfg->au16_ch_v[u32_index] = ntohs(sp_fcfg->au16_ch_v[u32_index]);
        }
        break;
        }
    case MSG_CMD_WST:
        {
        msg_s_wst_t* sp_wst;
        /* message body length check */
        if(MSG_LEN_WST != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_WST);
            return -2;
        }
        /* convert control port */
        sp_wst = (msg_s_wst_t*)p_sp_msg->au8_body;
        sp_wst->u16_pcd_port = ntohs(sp_wst->u16_pcd_port);
        break;
        }
    case MSG_CMD_PCD:
        {
        msg_s_pcd_t* sp_pcd;
        /* message body length check */
        if(MSG_LEN_PCD != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_PCD);
            return -2;
        }
        /* convert channel values */
        sp_pcd = (msg_s_pcd_t*)p_sp_msg->au8_body;
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            sp_pcd->au16_ch_v[u32_index] = ntohs(sp_pcd->au16_ch_v[u32_index]);
        }
        break;
        }
    case MSG_CMD_PSD:
        {
        msg_s_psd_t* sp_psd;
        /* message body length check */
        if(MSG_LEN_PSD != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_PSD);
            return -2;
        }
        /* convert battery values */
        sp_psd = (msg_s_psd_t*)p_sp_msg->au8_body;
        for(u32_index = 0u; u32_index < MSG_NUM_BATT; ++u32_index) {
            sp_psd->au16_batt[u32_index] = ntohs(sp_psd->au16_batt[u32_index]);
        }
        break;
        }
    case MSG_CMD_WCFG:
        /* message body length check */
        if(MSG_LEN_WCFG != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_WCFG);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_TLR:
        /* message body length check */
        if(MSG_LEN_TLR != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_TLR);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_TLST:
        /* message body length check */
        if(MSG_LEN_TLST != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_TLST);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_TLEND:
        /* message body length check */
        if(MSG_LEN_TLEND != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_TLEND);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_AREQ:
        /* message body length check */
        if(MSG_LEN_AREQ != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_AREQ);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_AGR:
        /* message body length check */
        if(MSG_LEN_AGR != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_AGR);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_FWUP:
        /* message body length check */
        if(MSG_LEN_FWUP != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_FWUP);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_STST:
        {
        msg_s_stst_t* sp_stst;
        /* message body length check */
        if(MSG_LEN_STST != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_STST);
            return -2;
        }
        /* convert streaming port */
        sp_stst = (msg_s_stst_t*)p_sp_msg->au8_body;
        sp_stst->u16_port = ntohs(sp_stst->u16_port);
        break;
        }
    case MSG_CMD_EST:
        /* message body length check */
        if(MSG_LEN_EST != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_EST);
            return -2;
        }
        /* no conversion */
        break;
    case MSG_CMD_EXTOUT:
        /* no message body length check (EXTOUT is dynamic) */
        /* no conversion */
        break;
    case MSG_CMD_ERR:
        {
        msg_s_err_t* sp_err;
        /* message body length check */
        if(MSG_LEN_ERR != p_sp_msg->u8_len) {
            LOG_ERR("length of msg 0x%.2x is incorrect 0x%.2x (expected: 0x%.2x)", 
                    p_sp_msg->u8_cmd, p_sp_msg->u8_len, MSG_LEN_ERR);
            return -2;
        }
        /* convert error code */
        sp_err = (msg_s_err_t*)p_sp_msg->au8_body;
        sp_err->u16_err_code = ntohs(sp_err->u16_err_code);
        break;
        }
    default:
        LOG_ERR("Unknown command code: %.2x", p_sp_msg->u8_cmd);
        return -1;
    }
    return 0;
}

/** Internal function to print a raw packet to a string
 *
 *  @param  p_sp_msg    message to be printed
 *  @param  p_st_str    output argument: string where the result is stored
 *  @param  p_u32_size  size of the output argument
 *
 *  The function prints a packet as a raw message to a string.
 */
static void l_dump_raw_packet_f(const msg_s_message_t* p_sp_msg, char* p_st_str, uint32_t p_u32_size) 
{
    char* cp_curr;
    uint32_t u32_len;
    uint32_t u32_index;
    uint8_t* u8p_crc16;

    /* print frame header: frame start bytes + command code + body length */
    (void)snprintf(p_st_str, p_u32_size, "%.2X %.2X %.2X %.2X", 
             p_sp_msg->au8_frame[0], p_sp_msg->au8_frame[1], p_sp_msg->u8_cmd, p_sp_msg->u8_len);
    /* print msg body */
    for(u32_index = 0u; u32_index < p_sp_msg->u8_len; ++u32_index) {
        u32_len = strlen(p_st_str);
        cp_curr = p_st_str + u32_len;
        (void)snprintf(cp_curr, p_u32_size - u32_len, " %.2X", p_sp_msg->au8_body[u32_index]);
    }
    /* print CRC */
    u32_len = strlen(p_st_str);
    cp_curr = p_st_str + u32_len;
    u8p_crc16 = (uint8_t*)&(p_sp_msg->u16_crc);
    (void)snprintf(cp_curr, p_u32_size - u32_len, " %.2X %.2X", u8p_crc16[0], u8p_crc16[1]);
}

/** Internal function to print a packet to a string
 *
 *  @param  p_sp_msg    message to be printed
 *  @param  p_st_str    output argument: string where the result is stored
 *  @param  p_u32_size  size of the output argument
 *
 *  The function prints a packet in parsed format.
 */
static void l_dump_packet_f(const msg_s_message_t* p_sp_msg, char* p_st_str, uint32_t p_u32_size) 
{
    uint32_t u32_index;
    char* cp_curr;
    uint32_t u32_len;

    switch(p_sp_msg->u8_cmd) {
    case MSG_CMD_BCSD: 
        {
        msg_s_bcsd_t* sp_bcsd = (msg_s_bcsd_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size,  "BCSD [0x%.2X] [%d.%d]", 
                 sp_bcsd->u8_sys, sp_bcsd->au8_version[0], sp_bcsd->au8_version[1]);
        }
        break;
    case MSG_CMD_BCSA:
        {
        msg_s_bcsa_t* sp_bcsa = (msg_s_bcsa_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "BCSA [%d.%d] [%d.%d] [%.*s] [%.*s]", 
                 sp_bcsa->au8_hw_ver[0], sp_bcsa->au8_hw_ver[1],
                 sp_bcsa->au8_sw_ver[0], sp_bcsa->au8_sw_ver[1],
                 MSG_MAX_NAME_LEN, sp_bcsa->au8_wrc_name,
                 MSG_MAX_SERIAL_LEN, sp_bcsa->au8_serial);
        }
        break;
    case MSG_CMD_TL:
        {
        msg_s_tl_t* sp_tl = (msg_s_tl_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "TL [0x%.2X] [%d.%d] [0x%.2X] [%.*s] [%d]", 
                 sp_tl->u8_sys, sp_tl->au8_version[0], sp_tl->au8_version[1], sp_tl->u8_prio,
                 MSG_MAX_NAME_LEN, sp_tl->au8_tr_name, sp_tl->u16_psd_port);
        }
        break;
    case MSG_CMD_DCFG:
        {
        msg_s_dcfg_t* sp_dcfg = (msg_s_dcfg_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "DCFG [%.*s] [%.4X] [%.4X]", 
                 MSG_MAX_NAME_LEN, sp_dcfg->au8_wrc_name, sp_dcfg->u16_cam_off, sp_dcfg->u16_wrc_off);
        }
        break;
    case MSG_CMD_CCFG:
        {
        msg_s_ccfg_t* sp_ccfg = (msg_s_ccfg_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "CCFG");
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.4X]", sp_ccfg->au16_ch_t[u32_index]);
        }
        }
        break;
    case MSG_CMD_FCFG:
        {
        msg_s_fcfg_t* sp_fcfg = (msg_s_fcfg_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "FCFG");
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.4X]", sp_fcfg->au16_ch_v[u32_index]);
        }
        }
        break;
    case MSG_CMD_WST:
        {
        msg_s_wst_t* sp_wst = (msg_s_wst_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "WST [%d] [%d] [%d]", 
                 sp_wst->u8_id, sp_wst->u8_cn, sp_wst->u16_pcd_port);
        }
        break;
    case MSG_CMD_PCD:
        {
        msg_s_pcd_t* sp_pcd = (msg_s_pcd_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "PCD");
        for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.4X]", sp_pcd->au16_ch_v[u32_index]);
        }
        }
        break;
    case MSG_CMD_PSD:
        {
        msg_s_psd_t* sp_psd = (msg_s_psd_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "PSD");
        for(u32_index = 0u; u32_index < MSG_NUM_BATT; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.4X]", sp_psd->au16_batt[u32_index]);
        }
        for(u32_index = 0u; u32_index < MSG_NUM_INPUT; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.2X]", sp_psd->au8_input[u32_index]);
        }
        }
        break;
    case MSG_CMD_WCFG:
        {
        msg_s_wcfg_t* sp_wcfg = (msg_s_wcfg_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "WCFG [%.*s] [%.*s] [%s] [%s] [%d] [%.*s]",
                 MSG_MAX_SSID_LEN, sp_wcfg->au8_ssid, MSG_MAX_PASS_LEN, sp_wcfg->au8_pass,
                 sp_wcfg->u8_ap_mode ? "AP" : "STA", sp_wcfg->u8_security ? "WPA2" : "OPEN",
                 sp_wcfg->u8_channel, MSG_MAX_CCODE_LEN, sp_wcfg->au8_country);
        }
        break;
    case MSG_CMD_TLR:
        (void)snprintf(p_st_str, p_u32_size, "TLR");
        break;
    case MSG_CMD_TLST:
        {
        msg_s_tlst_t* sp_tlst = (msg_s_tlst_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "TLST [%d] [0x%.2X] [%.*s]",
                 sp_tlst->u8_id, sp_tlst->u8_prio,
                 MSG_MAX_NAME_LEN, sp_tlst->au8_tr_name);
        }
        break;
    case MSG_CMD_TLEND:
        (void)snprintf(p_st_str, p_u32_size, "TLEND");
        break;
    case MSG_CMD_AREQ:
        {
        msg_s_areq_t* sp_areq = (msg_s_areq_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "AREQ [%d]", sp_areq->u8_id);
        }
        break;
    case MSG_CMD_AGR:
        {
        msg_s_agr_t* sp_agr = (msg_s_agr_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "AGR [%d] [0x%.2X] [%.*s] [0x%.2x]",
                 sp_agr->u8_id, sp_agr->u8_prio,
                 MSG_MAX_NAME_LEN, sp_agr->au8_tr_name, sp_agr->u8_notif);
        }
        break;
    case MSG_CMD_FWUP:
        {
        msg_s_fwup_t* sp_fwup = (msg_s_fwup_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "FWUP");
        for(u32_index = 0u; u32_index < MSG_MAX_MD5_LEN; ++u32_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, " [%.2X]", sp_fwup->au8_md5[u32_index]);
        }
        }
        break;
    case MSG_CMD_STST:
        {
        msg_s_stst_t* sp_stst = (msg_s_stst_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "STST [%d] [%d]",
                 sp_stst->u8_id, sp_stst->u16_port);
        }
        break;
    case MSG_CMD_EST:
        {
        msg_s_est_t* sp_est = (msg_s_est_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "EST [%d]", sp_est->u8_id);
        }
        break;
    case MSG_CMD_EXTOUT:
        {
        msg_s_extout_t* sp_extout = (msg_s_extout_t*)p_sp_msg->au8_body;
        uint8_t u8_index;
        (void)snprintf(p_st_str, p_u32_size, "EXTOUT [%d] [", sp_extout->u8_dst);
        for(u8_index = 0; u8_index < (p_sp_msg->u8_len - 1); ++u8_index) {
            u32_len = strlen(p_st_str);
            cp_curr = p_st_str + u32_len;
            if((p_u32_size - u32_len) > 0)
                (void)snprintf(cp_curr, p_u32_size - u32_len, "%s%.2X", 
                    (0 == u8_index) ? "" : " ", sp_extout->au8_data[u8_index]);
        }
        u32_len = strlen(p_st_str);
        cp_curr = p_st_str + u32_len;
        if((p_u32_size - u32_len) > 0)
            (void)snprintf(cp_curr, p_u32_size - u32_len, "]");
        }
        break;
    case MSG_CMD_ERR:
        {
        msg_s_err_t* sp_err = (msg_s_err_t*)p_sp_msg->au8_body;
        (void)snprintf(p_st_str, p_u32_size, "ERR [0x%.2x] [%d]", 
                 sp_err->u8_cmd, sp_err->u16_err_code);
        }
        break;
    default:
        LOG_ERR("Unknown command code: %.2x", p_sp_msg->u8_cmd);
    }
}

/** Internal function to read given number of bytes from a socket
 *
 *  @param  p_fd        file descriptor to read
 *  @param  p_vp_buf    output buffer to write
 *  @param p_count      number of bytes shall be read
 *  return number of read bytes or negative error code
 *
 *  The function wraps read(2).
 *  It blocks until the given number of bytes have been read or an error occurs.
 *  The function uses an internal buffer to buffer the incoming requests.
 *  The buffering is required by UDP packets.
 */
static ssize_t l_safe_read_f(int p_fd, void* p_vp_buf, size_t p_count)
{
    static uint8_t l_au8_buffer[L_U32_MAX_PACKET_SIZE];
    static uint32_t l_u32_buflen = 0u;
    static uint32_t l_u32_offset = 0u;
    uint32_t u32_count;
    uint8_t* u8p_outbuf;
    ssize_t res;

    u8p_outbuf = (uint8_t*)p_vp_buf;
    u32_count = p_count;
    while(u32_count > 0) {
        if(0u == l_u32_buflen) {
            res = read(p_fd, l_au8_buffer, sizeof(l_au8_buffer));
            if(res < 0) {
               return res;
            }
            l_u32_buflen = res;
            l_u32_offset = 0u;
        } else {    /* l_u32_buflen > 0u */
            uint32_t u32_cpy_cnt;
            u32_cpy_cnt = (l_u32_buflen < u32_count) ? l_u32_buflen : u32_count;
            memcpy(u8p_outbuf, l_au8_buffer + l_u32_offset, u32_cpy_cnt);
            u8p_outbuf += u32_cpy_cnt;
            u32_count -= u32_cpy_cnt;
            l_u32_buflen -= u32_cpy_cnt;
            l_u32_offset += u32_cpy_cnt;
        }
    }
    return p_count;
}

/*================================[ EXPORTED FUNCTION DEFINITIONS ]==========*/

/* Function to receive a message (see header for more info) */
int msg_recv_message_f(int p_fd_file, msg_s_message_t* p_sp_out)
{
    uint8_t* u8p_st_1 = NULL;
    uint8_t* u8p_st_2 = NULL;
    int res;
    uint16_t u16_crc_calc;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check arguments */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for msg receiver", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_out) {
        LOG_ERR("NULL argument for msg receiver");
        return -2;
    }

    u8p_st_1 = &(p_sp_out->au8_frame[0]);
    u8p_st_2 = &(p_sp_out->au8_frame[1]);

    /* read frame start bytes */
    res = l_safe_read_f(p_fd_file, u8p_st_1, 1);
    if(1 == res) {
        res = l_safe_read_f(p_fd_file, u8p_st_2, 1);
    }
    /* check frame start bytes */
    while((1 == res) &&
         ((L_U8_FRAME_START_1 != *u8p_st_1) ||
          (L_U8_FRAME_START_2 != *u8p_st_2))) {
        /* wait for correct frame start bytes */
        *u8p_st_1 = *u8p_st_2;
        res = l_safe_read_f(p_fd_file, u8p_st_2, 1);
    }
    /* error handling */
    if(1 != res) {
        LOG_ERR("reading frame start bytes returns %d : %s", res, 
                (res < 0) ? strerror(errno) : "");
        return (res < 0) ? -1 : -2;
    }
    /* read command code */
    res = l_safe_read_f(p_fd_file, &(p_sp_out->u8_cmd), sizeof(p_sp_out->u8_cmd));
    if(1 != res) {
        LOG_ERR("reading command code returns %d : %s", res, 
                (res < 0) ? strerror(errno) : "");
        return (res < 0) ? -1 : -2;
    }
    /* read length */
    res = l_safe_read_f(p_fd_file, &(p_sp_out->u8_len), sizeof(p_sp_out->u8_len));
    if(1 != res) {
        LOG_ERR("reading length returns %d : %s", res, 
                (res < 0) ? strerror(errno) : "");
        return (res < 0) ? -1 : -2;
    }
    /* read message body */
    res = l_safe_read_f(p_fd_file, p_sp_out->au8_body, p_sp_out->u8_len);
    if(p_sp_out->u8_len != res) {
        LOG_ERR("reading message body returns %d : %s", res, 
                (res < 0) ? strerror(errno) : "");
        return (res < 0) ? -1 : -2;
    }
    /* read CRC */
    res = l_safe_read_f(p_fd_file, &(p_sp_out->u16_crc), 2);
    if(2 != res) {
        LOG_ERR("reading CRC returns %d : %s", res, 
                (res < 0) ? strerror(errno) : "");
        return (res < 0) ? -1 : -2;
    }

    l_dump_raw_packet_f(p_sp_out, st_str, sizeof(st_str));
    log_dump_f(LOG_RAW_IN, p_sp_out->u8_cmd, st_str);

    /* convert CRC to host byte order */
    p_sp_out->u16_crc = ntohs(p_sp_out->u16_crc);
    /* calculate current CRC */
    u16_crc_calc = l_calc_msg_crc_f(p_sp_out);
    if(u16_crc_calc != p_sp_out->u16_crc) {
        LOG_ERR("CRC check failed; in packet: %.4X, calculated: %.4X", 
                p_sp_out->u16_crc, u16_crc_calc);
        return 1;
    }
    /* convert message body fields from network to host byte order */
    res = l_ntoh_msg_f(p_sp_out);
    if(res) {
        LOG_ERR("Can not convert network to host byte order %d", res);
        return 2;
    }

    l_dump_packet_f(p_sp_out, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_IN, p_sp_out->u8_cmd, st_str);
    
    return 0;
}

/* Function to receive a BCSA message (see header for more info) */
int msg_recv_bcsa_f(int p_fd_file, msg_s_bcsa_t* p_sp_bcsa, 
                    msg_s_bcsa_peer_t * p_sp_peer)
{
    int res;
    uint16_t u16_crc_calc;
    char st_str[L_U32_MAX_DUMP_LEN + 1];
    msg_s_message_t s_msg;
    struct sockaddr_in s_peer;
    socklen_t len_sock;
    uint16_t *u16p_crc_pos;
    uint16_t u16_crc;
    char* cp_ip;

    /* check arguments */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for msg receiver", p_fd_file);
        return -2;
    }
    if((NULL == p_sp_bcsa) || (NULL == p_sp_peer)) {
        LOG_ERR("NULL argument for msg receiver");
        return -2;
    }

    /* initialize peer struct */
    memset(&s_peer, 0, sizeof(s_peer));
    s_peer.sin_family = AF_INET;
    len_sock = sizeof(struct sockaddr_in);
    
    /* receive a BCSA msg */
    res = recvfrom(p_fd_file, &s_msg, sizeof(s_msg), MSG_DONTWAIT,
                   (struct sockaddr*)&s_peer, &len_sock);
    if(res < 0) {
        if(EAGAIN == errno) {
            return 1;   /* non-blocking */
        } else {
            LOG_ERR("BCSA Recvfrom error: %d - %s", res, strerror(errno));
            return res;
        }
    }

    /* check peer */
    if(sizeof(struct sockaddr_in) != len_sock) {
        LOG_ERR("Socket length error %d expected is %d", 
                len_sock, sizeof(struct sockaddr_in));
        return 1;
    }
    if(AF_INET != s_peer.sin_family) {
        LOG_ERR("Address family is incorrect %d instead of %d", 
                s_peer.sin_family, AF_INET);
        return 1;
    }

    /* check the received packet */
    if((L_U8_FRAME_START_1 != s_msg.au8_frame[0]) ||
       (L_U8_FRAME_START_2 != s_msg.au8_frame[1])) {
        LOG_ERR("Wrong frame header bytes: %.2x%.2x",
                s_msg.au8_frame[0], s_msg.au8_frame[1]);
        return 1;
    }
    if(L_U32_MSG_LEN(&s_msg) != res) {
        LOG_ERR("Received packet length is %d instead of %d", 
                res, L_U32_MSG_LEN(&s_msg));
        return 1;
    }

    /* convert CRC to host byte order */
    u16p_crc_pos = (uint16_t*)&(s_msg.au8_body[s_msg.u8_len]);
    s_msg.u16_crc = *u16p_crc_pos;

    l_dump_raw_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_RAW_IN, s_msg.u8_cmd, st_str);

    /* filter for BCSA packets */
    if(MSG_CMD_BCSA != s_msg.u8_cmd) {
        LOG_ERR("This is not a BCSA message (%.2x) but %.2x", 
                MSG_CMD_BCSA, s_msg.u8_cmd);
        return 1;
    }
    if(MSG_LEN_BCSA != s_msg.u8_len) {
        LOG_ERR("Invalid message length field %d instead of %d", 
                s_msg.u8_len, MSG_LEN_BCSA);
        return 1;
    }
        
    u16_crc = ntohs(*u16p_crc_pos);
    /* calculate current CRC */
    u16_crc_calc = l_calc_msg_crc_f(&s_msg);
    if(u16_crc_calc != u16_crc) {
        LOG_ERR("CRC check failed; in packet: %.4X, calculated: %.4X", 
                u16_crc, u16_crc_calc);
        return 1;
    }
    /* convert message body fields from network to host byte order */
    res = l_ntoh_msg_f(&s_msg);
    if(res) {
        LOG_ERR("Can not convert network to host byte order %d", res);
        return 2;
    }

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_IN, s_msg.u8_cmd, st_str);

    /* copy to output argument */
    memcpy(p_sp_bcsa, s_msg.au8_body, sizeof(msg_s_bcsa_t));
    cp_ip = inet_ntoa(s_peer.sin_addr);
    if(cp_ip) {
        strncpy(p_sp_peer->st_ip, cp_ip, MSG_MAX_IP_LEN);
        p_sp_peer->st_ip[MSG_MAX_IP_LEN] = '\0';
    } else {
        LOG_ERR("Can not convert peer address to IP string");
        return 3;
    }
    
    return 0;
}

/* Function to send a BCSD (Broadcast Service Discovery) message */
int msg_broadcast_bcsd_f(int p_fd_file, const msg_s_bcsd_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];
    struct sockaddr_in s_broadcast;
    uint16_t u16_crc;
    uint16_t* u16p_crc_pos;
    uint32_t u32_msg_len;

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for BCSD msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for BCSD msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_BCSD;
    s_msg.u8_len = MSG_LEN_BCSD;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_BCSD);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    /* version field has two independent bytes... */

    /* calculate message CRC */
    u16_crc = l_calc_msg_crc_f(&s_msg);
    u16_crc = htons(u16_crc);
    
    u16p_crc_pos = (uint16_t*)&(s_msg.au8_body[s_msg.u8_len]);
    *u16p_crc_pos = u16_crc;
    u32_msg_len = L_U32_MSG_LEN(&s_msg);
    s_msg.u16_crc = u16_crc;

    l_dump_raw_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_RAW_OUT, s_msg.u8_cmd, st_str);
    
    /* initialize broadcast descriptor */
    memset(&s_broadcast, 0, sizeof(s_broadcast));
    s_broadcast.sin_family = AF_INET;
    s_broadcast.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    s_broadcast.sin_port = htons(L_U16_BCS_UDP_PORT);
    res = sendto(p_fd_file, &s_msg, u32_msg_len, 0,
                 (struct sockaddr*)&s_broadcast, sizeof(s_broadcast));
    if(res != u32_msg_len) {
        LOG_ERR("sending message (size: %d) returns %d : %s", 
                u32_msg_len, res, (res < 0) ? strerror(errno) : "NULL");
        return (res < 0) ? -1 : -2;
    }

    return 0;
}

/* Function to send a TL (Transmitter Login) message (see header for more info) */
int msg_send_tl_f(int p_fd_file, const msg_s_tl_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_tl_t* sp_tl;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for TL msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for TL msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_TL;
    s_msg.u8_len = MSG_LEN_TL;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_TL);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    /* version field has two independent bytes... */
    sp_tl = (msg_s_tl_t*)s_msg.au8_body;
    sp_tl->u16_psd_port = htons(p_sp_arg->u16_psd_port);

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a DCFG (Device Configuration) message (see header for more info) */
int msg_send_dcfg_f(int p_fd_file, const msg_s_dcfg_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_dcfg_t* sp_dcfg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for DCFG msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for DCFG msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_DCFG;
    s_msg.u8_len = MSG_LEN_DCFG;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_DCFG);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    sp_dcfg = (msg_s_dcfg_t*)s_msg.au8_body;
    sp_dcfg->u16_cam_off = htons(p_sp_arg->u16_cam_off);
    sp_dcfg->u16_wrc_off = htons(p_sp_arg->u16_wrc_off);

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a CCFG (Channel Configuration) message (see header for more info) */
int msg_send_ccfg_f(int p_fd_file, const msg_s_ccfg_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_ccfg_t* sp_ccfg;
    uint32_t u32_index;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for CCFG msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for CCFG msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_CCFG;
    s_msg.u8_len = MSG_LEN_CCFG;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_CCFG);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    sp_ccfg = (msg_s_ccfg_t*)s_msg.au8_body;
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        sp_ccfg->au16_ch_t[u32_index] = htons(p_sp_arg->au16_ch_t[u32_index]);
    }

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a FCFG (Failsafe Configuration) message (see header for more info) */
int msg_send_fcfg_f(int p_fd_file, const msg_s_fcfg_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_fcfg_t* sp_fcfg;
    uint32_t u32_index;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for FCFG msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for FCFG msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_FCFG;
    s_msg.u8_len = MSG_LEN_FCFG;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_FCFG);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    sp_fcfg = (msg_s_fcfg_t*)s_msg.au8_body;
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        sp_fcfg->au16_ch_v[u32_index] = htons(p_sp_arg->au16_ch_v[u32_index]);
    }

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a PCD (Periodic Channel Data) message (see header for more info) */
int msg_send_pcd_f(int p_fd_file, const msg_s_pcd_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_pcd_t* sp_pcd;
    uint32_t u32_index;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for PCD msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for PCD msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_PCD;
    s_msg.u8_len = MSG_LEN_PCD;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_PCD);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    sp_pcd = (msg_s_pcd_t*)s_msg.au8_body;
    for(u32_index = 0u; u32_index < MSG_NUM_CH; ++u32_index) {
        sp_pcd->au16_ch_v[u32_index] = htons(p_sp_arg->au16_ch_v[u32_index]);
    }

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a WCFG (WiFi Configuration) message (see header for more info) */
int msg_send_wcfg_f(int p_fd_file, const msg_s_wcfg_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for WCFG msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for WCFG msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_WCFG;
    s_msg.u8_len = MSG_LEN_WCFG;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_WCFG);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a TLR (Transmitter List Request) message (see header for more info) */
int msg_send_tlr_f(int p_fd_file, /*@unused@*/ const void* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for TLR msg sender", p_fd_file);
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_TLR;
    s_msg.u8_len = MSG_LEN_TLR;

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a AREQ (Access Request) message (see header for more info) */
int msg_send_areq_f(int p_fd_file, const msg_s_areq_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for AREQ msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for AREQ msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_AREQ;
    s_msg.u8_len = MSG_LEN_AREQ;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_AREQ);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a FWUP (Firmware Update) message (see header for more info) */
int msg_send_fwup_f(int p_fd_file, const msg_s_fwup_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for FWUP msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for FWUP msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_FWUP;
    s_msg.u8_len = MSG_LEN_FWUP;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_FWUP);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a STST (Start Stream) message (see header for more info) */
int msg_send_stst_f(int p_fd_file, const msg_s_stst_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    msg_s_stst_t* sp_stst;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for STST msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for STST msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_STST;
    s_msg.u8_len = MSG_LEN_STST;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_STST);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */
    sp_stst = (msg_s_stst_t*)s_msg.au8_body;
    sp_stst->u16_port = htons(p_sp_arg->u16_port);
    
    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a EST (End Stream) message (see header for more info) */
int msg_send_est_f(int p_fd_file, const msg_s_est_t* p_sp_arg)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for EST msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for EST msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_EST;
    s_msg.u8_len = MSG_LEN_EST;
    memcpy(s_msg.au8_body, p_sp_arg, MSG_LEN_EST);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}

/* Function to send a EXTOUT (Extrenal Out) message (see header for more info) */
int msg_send_extout_f(int p_fd_file, const msg_s_extout_t* p_sp_arg, uint8_t p_u8_len)
{
    int res;
    msg_s_message_t s_msg;
    char st_str[L_U32_MAX_DUMP_LEN + 1];

    /* check input argument */
    if(p_fd_file < 0) {
        LOG_ERR("invalid file handler %d for EXTOUT msg sender", p_fd_file);
        return -2;
    }
    if(NULL == p_sp_arg) {
        LOG_ERR("NULL argument for EXTOUT msg sender");
        return -2;
    }

    /* fill message fields */
    s_msg.au8_frame[0] = L_U8_FRAME_START_1;
    s_msg.au8_frame[1] = L_U8_FRAME_START_2;
    s_msg.u8_cmd = MSG_CMD_EXTOUT;
    s_msg.u8_len = p_u8_len + 1;
    memcpy(s_msg.au8_body, p_sp_arg, s_msg.u8_len + 1);

    l_dump_packet_f(&s_msg, st_str, sizeof(st_str));
    log_dump_f(LOG_PACKET_OUT, s_msg.u8_cmd, st_str);

    /* convert to message body to network byte order */

    /* send message */
    res = l_send_message_f(p_fd_file, &s_msg);
    
    return res;
}


#ifdef MSG_TEST_APP
int main(int argc, char* argv[]) {
    int i;
    uint8_t buf[] = { 0x1A, 4, 1, 1, 1, 0};
    uint16_t u16_crc = 0;
    
    for(i=0; i < sizeof(buf); ++i) 
        u16_crc = l_crc_ccitt_f(u16_crc, buf[i]);

    printf("%.4X\n", u16_crc);
    return 0;
}
#endif

/**@}*/
