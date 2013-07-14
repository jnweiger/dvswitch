/**
 * @file log_logger.h
 * @brief logger module interface
 *
 * $Revision: $
 * $Date: $
 *
 * Logger functions
 *
 * Author: Balint Viragh <bviragh@dension.com>
 * Copyright (C) 2011 Dension Audio Systems Kft.
 */

/**
 * @addtogroup WRC
 * @{
 */

/*================================[ HEADER INIT ]============================*/
#ifndef LOG_LOGGER_H
#define LOG_LOGGER_H

/*================================[ EXPORTED MACROS ]========================*/

/* the following macro functions cause splint parse error */
#ifndef S_SPLINT_S
/** log an error message */
#define LOG_ERR(p_st_fmt, ...) fprintf(stderr, "ERR %s:%u " p_st_fmt "\n", \
                __FILE__, __LINE__, ##__VA_ARGS__)
/** log an info message */
#define LOG_INFO(p_st_fmt, ...) fprintf(stderr, "INF %s:%u " p_st_fmt "\n", \
                __FILE__, __LINE__, ##__VA_ARGS__)
#endif

/*================================[ EXPORTED TYPEDEFS ]======================*/

/** Direction types of a log message */
typedef enum {
    LOG_RAW_IN,         /**< raw dump of a received packet */
    LOG_PACKET_IN,      /**< parsed packet dump of a received packet */
    LOG_RAW_OUT,        /**< raw dump of a sent packet */
    LOG_PACKET_OUT,     /**< parsed packet dump of a sent packet */ 
} log_e_dir_t;

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ EXPORTED FUNCTIONS ]=====================*/

/** Function to initialize logging
 *
 *  @param  p_st_periodic_log   logfile name for periodic messages
 *  
 *  The function initialize logging.
 *  If p_st_periodic_log is NULL then PCD and PSD messages are not logged.
 */
void log_init_f(const char* p_st_periodic_log);

/** Function to print a packet dump
 *
 *  @param  p_e_dir     direction of the dump log
 *  @param  p_u8_type   packet type
 *  @param  p_st_dump   dump string
 *
 *  The function prints the dump line into the appropriate log file/stdout.
 *  Filtering is performed using p_e_dir and p_u8_type arguments.
 *  Timestamp and direction info is printed before the dump string.
 */
void log_dump_f(log_e_dir_t p_e_dir, uint8_t p_u8_type, const char* p_st_dump);

#endif /* LOG_LOGGER_H */

/**@}*/
