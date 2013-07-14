/**
 * @file cam_camera.h
 * @brief interface of camera stream handler module
 *
 * $Revision: $
 * $Date: $
 *
 * Camera stream handler module: (receives processes and/or displays).
 *
 * Author: Balint Viragh <bviragh@dension.com>
 * Copyright (C) 2011 Dension Audio Systems Kft.
 */

/**
 * @addtogroup WRC
 * @{
 */

/*================================[ HEADER INIT ]============================*/
#ifndef CAM_CAMERA_H
#define CAM_CAMERA_H

/*================================[ EXPORTED MACROS ]========================*/

/*================================[ EXPORTED TYPEDEFS ]======================*/

/** structure to store a camera streaming handler */
typedef struct {
    uint32_t u32_error_cnt;     /**< error counter */
    uint32_t u32_packet_cnt;    /**< packet counter */
    uint32_t u32_exp_framenum;  /**< expected number of the next frame */
    uint32_t u32_exp_offset;    /**< expected offset of the next data */
    struct timeval s_prev_time; /**< previous FPS measurement time */
    double f32_fps;             /**< current FPS value of the stream */
} cam_s_handler_t;

/*================================[ EXTERNAL GLOBALS ]=======================*/

/*================================[ EXPORTED FUNCTIONS ]=====================*/

/** Function to initialize camera streaming 
 *
 *  @param  output argument: camera streaming handler to be initialized
 *  @return 0 in case of success
 *
 *  The function initializes a camera connection.
 *  Ie: initialize SDL window 
 */
int cam_stream_init_f(cam_s_handler_t* p_sp_handler);

/** Function to read and handle camera stream
 *
 *  @param  p_fd_socket     socket where the camera stream can be read
 *  @param  p_sp_handler    handler of the camera streaming
 *  @return 0: success, > 0 reversible error, < 0 irreversible error
 *
 *  The function receives and processes a camera stream from the specified UDP socket.
 */
int cam_stream_recv_f(int p_fd_socket, cam_s_handler_t* p_sp_handler);

/** Function to stop close SDL window
 *
 */
void cam_stop_f(void);

#endif /* CAM_CAMERA_H */

/**@}*/
