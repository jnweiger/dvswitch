/**
 * @file log_logger.c
 * @brief logging module
 *
 * $Revision: $
 * $Date: $
 *
 * Filter and print log messages...
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
#include <sys/time.h>
#include <errno.h>

/* external library headers */   

/* Interface of other modules */
#include "msg_messages.h"

/* Interface header of this module */
#include "log_logger.h"

/*================================[ INTERNAL MACROS ]========================*/

/*================================[ INTERNAL TYPEDEFS ]======================*/

/** structure to describe a log output */
typedef struct {
    FILE*       sp_file;        /**< file descriptor to write */
    uint32_t    u32_num;        /**< line number counter */
    struct timeval s_start;     /**< start time of the logging */
} l_s_log_t;

/*================================[ INTERNAL FUNCTION PROTOTYPES ]===========*/

/* print a log message */
static void l_print_log_f(l_s_log_t* p_sp_log, log_e_dir_t p_e_dir, const char* p_st_str);

/*================================[ INTERNAL GLOBALS ]=======================*/

/** stderr log descriptor */
static l_s_log_t l_s_log_stderr;
/** log descriptor for periodic messages */
static l_s_log_t l_s_log_periodic;

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ INTERNAL FUNCTION DEFINITIONS ]==========*/

/** Internal function to print a log message 
 *
 *  @param  p_sp_out    handler of the written log file 
 *  @param  p_e_dir     direction to be marked in the log message
 *  @param  p_st_str    log message
 *
 *  The function prints line number, timestamp, direction and log message.
 */
static void l_print_log_f(l_s_log_t* p_sp_log, log_e_dir_t p_e_dir, const char* p_st_str)
{
    double f32_currtime = 0.0; /* in seconds */
    struct timeval s_currtime;
    const char* cp_dir;

    if(NULL == p_sp_log->sp_file)
        return;

    if(gettimeofday(&s_currtime, NULL)) {
        LOG_ERR("Can not get current time %s", strerror(errno));
    } else {
        f32_currtime = s_currtime.tv_sec;
        f32_currtime += (double)(s_currtime.tv_usec) / 1e6;
        f32_currtime -= p_sp_log->s_start.tv_sec;
        f32_currtime -= (double)(p_sp_log->s_start.tv_usec) / 1e6;
    }
    if((LOG_RAW_IN == p_e_dir) || (LOG_PACKET_IN == p_e_dir)) {
        cp_dir = "<<<";
    } else {
        cp_dir = ">>>";
    }
    fprintf(p_sp_log->sp_file, "\r%8d %11.5f %s %s\n", 
            p_sp_log->u32_num, f32_currtime, cp_dir, p_st_str);

    p_sp_log->u32_num++;
}

/*================================[ EXPORTED FUNCTION DEFINITIONS ]==========*/

void log_init_f(const char* p_st_periodic_log) 
{
    l_s_log_stderr.sp_file = stderr;
    l_s_log_stderr.u32_num = 0u;
    (void)gettimeofday(&l_s_log_stderr.s_start, NULL);
    setlinebuf(l_s_log_stderr.sp_file);

    if(p_st_periodic_log) {
        l_s_log_periodic.sp_file = fopen(p_st_periodic_log, "w");
        l_s_log_periodic.u32_num = 0u;
        (void)gettimeofday(&l_s_log_periodic.s_start, NULL);
        setlinebuf(l_s_log_periodic.sp_file);
    }
}

void log_dump_f(log_e_dir_t p_e_dir, uint8_t p_u8_type, const char* p_st_dump) 
{
    l_s_log_t* sp_log = &l_s_log_stderr;

    /* filter message */
    if((MSG_CMD_PCD == p_u8_type) ||
       (MSG_CMD_PSD == p_u8_type)) {
        sp_log = &l_s_log_periodic;
    } else {
        sp_log = &l_s_log_stderr;
    }
    
    l_print_log_f(sp_log, p_e_dir, p_st_dump);
}

/**@}*/
