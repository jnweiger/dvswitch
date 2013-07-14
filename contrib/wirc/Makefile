#################################################################################
# WRC Project
#
# Balint Viragh <bviragh@dension.com>
# Copyright (C) 2011 Dension Audio Systems Kft.
#################################################################################

CC := gcc
LD := ld
STRIP 	:= strip
CFLAGS	:= $(HOST_CFLAGS) -I.

CFLAGS += -Wall
CFLAGS += -ggdb

LDFLAGS += -lpthread

OBJS := dvsource_wirc.o
OBJS += msg_messages.o
OBJS += log_logger.o
OBJS += dvs_camera.o

BINARY := dvsource-wirc

all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $< 

clean:
	rm -f $(OBJS) *.o
	rm -f $(BINARY)

.PHONY: all 

