// Copyright 2007 Ben Hutchings and Tore Sinding Bekkedal.
// See the file "COPYING" for licence details.

#ifndef DVSWITCH_FRAME_H
#define DVSWITCH_FRAME_H

#ifndef __cplusplus
#include <stdbool.h>
#endif

#include <sys/types.h>

#include <libdv/dv.h>

#include "dif.h"

struct frame
{
    uint64_t timestamp;           // set by mixer
    unsigned serial_num;          // set by mixer
    bool cut_before;              // set by mixer
    dv_system_t system;           // set by source
    size_t size;                  // set by source
    uint8_t buffer[DIF_MAX_FRAME_SIZE];
};

#endif // !defined(DVSWITCH_FRAME_H)
