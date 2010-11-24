// Copyright 2010 Ben Hutchings.
// See the file "COPYING" for licence details.
// Source that reads from a Firewire (IEEE 1394) channel

#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <vector>

#include <getopt.h>
#include <poll.h>
#include <unistd.h>

#include <libraw1394/raw1394.h>

#include "config.h"
#include "dif.h"
#include "protocol.h"
#include "socket.h"

// IEC 61883 parameters
#define CIF_HEADER_SIZE 8
#define CIF_PACKET_SIZE (6 * DIF_BLOCK_SIZE)

static option options[] = {
    {"card",        1, NULL, 'c'},
    {"listen-host", 1, NULL, 'h'},
    {"listen-port", 1, NULL, 'p'},
    {"verbose",     0, NULL, 'v'},
    {"help",        0, NULL, 'H'},
    {NULL,          0, NULL, 0}
};

static std::string fw_port_name("0");
static std::string listen_host;
static std::string listen_port;
static bool verbose = false;

static unsigned long long total_len;
static unsigned int dropped_packets;
static unsigned int dropped_frames;
static unsigned int complete_frames;

static unsigned int seq_count;
static unsigned int next_seq_num;
static unsigned int next_block_num;
static unsigned char frame_buf[DIF_MAX_FRAME_SIZE];

static void handle_config(const char * name, const char * value)
{
    if (strcmp(name, "FIREWIRE_CARD") == 0 ||
	strcmp(name, "FIREWIRE_DEVICE") == 0)
	fw_port_name = value;
    else if (strcmp(name, "LISTEN_HOST") == 0)
	listen_host = value;
    else if (strcmp(name, "LISTEN_PORT") == 0)
	listen_port = value;
}

static void usage(const char *progname)
{
    fprintf(stderr,
	    "Usage: %s [-t] [-v] [-c CARD-NUMBER | DEVICE] \\\n"
	    "           [--listen-host HOST] [--listen-port PORT]\n",
	    progname);
}

static enum raw1394_iso_disposition
receive(raw1394handle_t /*handle*/,
	unsigned char * data,
	unsigned int len,
	unsigned char /*channel*/,
	unsigned char /*tag*/,
	unsigned char /*sy*/,
	unsigned int /*cycle*/,
	unsigned int dropped)
{
    total_len += len;
    dropped_packets += dropped;

    if (len == CIF_HEADER_SIZE + CIF_PACKET_SIZE)
    {
	unsigned int seq_num, typed_block_num, block_num;

	data += CIF_HEADER_SIZE;

	// Find position of these blocks in the sequence
	seq_num = data[1] >> 4;
	typed_block_num = data[2];
	block_num = -1;
	switch (data[0] >> 5)
	{
	case 0:
	    // Header: position 0
	    if (typed_block_num == 0)
		block_num = 0;
	    break;
	case 3:
	    // Audio: position 6 or 102
	    if (typed_block_num < 9)
		block_num = 6 + typed_block_num * 16;
	    break;
	case 4:
	    // Video: any other position divisible by 6
	    if (typed_block_num < 135)
		block_num = 7 + typed_block_num + typed_block_num / 15;
	    break;
	}

	// Are these the blocks we're expecting?
	if (seq_num == next_seq_num && block_num == next_block_num)
	{
	    // Set sequence count from the first block of the frame
	    if (seq_num == 0 && block_num == 0)
		seq_count = dv_buffer_system(data)->seq_count;

	    // Append blocks to frame
	    memcpy(frame_buf + ((seq_num * DIF_BLOCKS_PER_SEQUENCE + block_num)
				* DIF_BLOCK_SIZE),
		   data, CIF_PACKET_SIZE);

	    // Advance position in sequence
	    next_block_num = block_num + CIF_PACKET_SIZE / DIF_BLOCK_SIZE;
	    if (next_block_num == DIF_BLOCKS_PER_SEQUENCE)
	    {
		// Advance to next sequence
		next_block_num = 0;
		++next_seq_num;
		if (next_seq_num == seq_count)
		{
		    // Finish frame
		    ++complete_frames;
		    next_seq_num = 0;
		}
	    }
	}
	else if (next_seq_num != 0 || next_block_num != 0)
	{
	    ++dropped_frames;
	    next_seq_num = 0;
	    next_block_num = 0;
	}
    }

    return RAW1394_ISO_OK;
}

static int select_fw_port(raw1394handle_t handle, const std::string & name)
{
    int n_ports = raw1394_get_port_info(handle, NULL, 0);
    std::vector<raw1394_portinfo> ports(n_ports);
    if (n_ports > 0)
    {
	int n_ports_again = raw1394_get_port_info(handle, &ports[0], n_ports);
	if (n_ports > n_ports_again)
	{
	    n_ports = n_ports_again;
	    ports.resize(n_ports);
	}
    }
    if (n_ports == 0)
    {
	fprintf(stderr, "ERROR: No Firewire ports accessible\n");
	return -1;
    }

    // Try converting name to an integer
    char * end;
    int i = strtoul(name.c_str(), &end, 10);

    // If we didn't convert the whole string, assume it really is a name
    if (*end)
	for (i = 0; i != n_ports; ++i)
	    if (name == ports[i].name)
		break;

    if (i >= n_ports)
    {
	fprintf(stderr, "ERROR: %s: not found\n", name.c_str());
	return -1;
    }

    printf("INFO: Reading from Firewire port %s\n", ports[i].name);
    return i;
}

static volatile sig_atomic_t received_sigint;

static void handle_sigint(int)
{
    received_sigint = 1;
}

int main(int argc, char ** argv)
{
    // Initialise settings from configuration files.
    dvswitch_read_config(handle_config);

    // Parse arguments.

    int opt;
    while ((opt = getopt_long(argc, argv, "c:v", options, NULL)) != -1)
    {
	switch (opt)
	{
	case 'c':
	    fw_port_name = optarg;
	    break;
	case 'h':
	    listen_host = optarg;
	    break;
	case 'p':
	    listen_port = optarg;
	    break;
	case 'v':
	    verbose = true;
	    break;
	case 'H': // --help
	    usage(argv[0]);
	    return 0;
	default:
	    usage(argv[0]);
	    return 2;
	}
    }

    if (optind != argc)
	fw_port_name = argv[optind++];

    if (optind != argc)
    {
	fprintf(stderr, "%s: excess argument \"%s\"\n",
		argv[0], argv[optind]);
	usage(argv[0]);
	return 2;
    }

    // Catch SIGINT.
    struct sigaction sigint_action;
    sigint_action.sa_handler = handle_sigint;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sigint_action, NULL))
    {
	perror("ERROR: sigaction");
	return 1;
    }

    raw1394handle_t handle = raw1394_new_handle();
    if (!handle)
    {
	perror("raw1394_new_handle");
	return 1;
    }

    int fw_port_index = select_fw_port(handle, fw_port_name);
    if (fw_port_index < 0)
	return 1;

    if (raw1394_set_port(handle, fw_port_index))
    {
	perror("raw1394_set_port");
	return 1;
    }

    if (raw1394_iso_recv_init(handle, receive, /*buf_packets=*/ 600,
			      /*max_packet_size=*/
			      CIF_HEADER_SIZE + CIF_PACKET_SIZE + 8,
			      /*channel=*/ 63,
			      /*mode=*/ RAW1394_DMA_DEFAULT,
			      /*irq_interval=*/ 100))
    {
	perror("raw1394_iso_recv_init");
	return 1;
    }

    if (raw1394_iso_recv_start(handle, -1, -1, -1))
    {
	perror("raw1394_iso_recv_start");
	return 1;
    }

    if (verbose)
	printf("INFO: Running\n");

    // Loop until I/O failure or SIGINT received
    for (;;)
    {
	pollfd poll_fds[] = {
	    { raw1394_get_fd(handle), POLLIN, 0 }
	};

	if (poll(poll_fds, 1, -1) < 0 ||
	    poll_fds[0].revents & (POLLHUP | POLLERR) ||
	    raw1394_loop_iterate(handle) < 0 ||
	    received_sigint)
	    break;
    }

    raw1394_iso_stop(handle);
    raw1394_iso_shutdown(handle);

    if (verbose)
    {
	printf("INFO: Total length received: %llu\n", total_len);
	printf("INFO: Dropped packets: %u\n", dropped_packets);
	printf("INFO: Dropped frames: %u\n", dropped_frames);
	printf("INFO: Complete frames: %u\n", complete_frames);
    }

    return 0;
}
