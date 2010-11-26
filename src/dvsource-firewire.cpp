// Copyright 2010 Ben Hutchings.
// Copyright 2010 Live Networks, Inc.
// See the file "COPYING" for licence details.
// Source that reads from a Firewire (IEEE 1394) channel

#include <algorithm>
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

#include "DVVideoStreamFramer.hh"
#include <liveMedia.hh>
#include <BasicUsageEnvironment.hh>
#include <GroupsockHelper.hh>

#include <libraw1394/raw1394.h>

#include "auto_handle.hpp"
#include "config.h"
#include "dif.h"
#include "os_error.hpp"
#include "protocol.h"
#include "socket.h"

// IEC 61883 parameters
#define CIF_HEADER_SIZE 8
#define CIF_PACKET_SIZE (6 * DIF_BLOCK_SIZE)

struct auto_raw1394_closer
{
    void operator()(raw1394handle_t handle) const
    {
	if (handle)
	    raw1394_destroy_handle(handle);
    }
};
struct auto_raw1394_factory
{
    raw1394handle_t operator()() const { return 0; }
};
typedef auto_handle<raw1394handle_t, auto_raw1394_closer,
		    auto_raw1394_factory> auto_raw1394;

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

class firewire_source : public FramedSource
{
public:
    firewire_source(UsageEnvironment & env, const std::string & port_name);
    virtual ~firewire_source();

private:
    virtual void doGetNextFrame();

    bool try_open();

    static void raw_poll(void * opaque, int);
    static enum raw1394_iso_disposition
    raw_receive(raw1394handle_t handle,
		unsigned char * data, unsigned int len,
		unsigned char channel, unsigned char tag,
		unsigned char sy, unsigned int cycle,
		unsigned int dropped);
    void receive(unsigned char * data, unsigned int len, unsigned int dropped);

    std::string port_name_;
    auto_raw1394 handle_;

    unsigned long long total_len_;
    unsigned int dropped_packets_;

    unsigned int seq_count_;
    unsigned int next_seq_num_;
    unsigned int next_block_num_;
};

firewire_source::firewire_source(UsageEnvironment & env,
				 const std::string & port_name)
    : FramedSource(env), port_name_(port_name),
      total_len_(0), dropped_packets_(0),
      seq_count_(0), next_seq_num_(0), next_block_num_(0)
{
}

firewire_source::~firewire_source()
{
    if (handle_.get())
    {
	envir().taskScheduler().turnOffBackgroundReadHandling(
	    raw1394_get_fd(handle_.get()));

	// libraw1394 does *not* automatically close the file descriptor
	// used for isochronous I/O on the new (Juju) firewire stack
	raw1394_iso_shutdown(handle_.get());
    }

    if (verbose)
    {
	printf("INFO: Total length received: %llu\n", total_len_);
	printf("INFO: Dropped packets: %u\n", dropped_packets_);
    }
}

bool firewire_source::try_open()
{
    auto_raw1394 handle(raw1394_new_handle());
    if (!handle.get())
	return false;
    raw1394_set_userdata(handle.get(), this);

    int n_ports = raw1394_get_port_info(handle.get(), NULL, 0);
    std::vector<raw1394_portinfo> ports(n_ports);
    if (n_ports > 0)
    {
	int n_ports_again =
	    raw1394_get_port_info(handle.get(), &ports[0], n_ports);
	if (n_ports > n_ports_again)
	{
	    n_ports = n_ports_again;
	    ports.resize(n_ports);
	}
    }
    if (n_ports == 0)
    {
	fprintf(stderr, "ERROR: No Firewire ports accessible\n");
	return false;
    }

    // Try converting name to an integer
    char * end;
    int i = strtoul(port_name_.c_str(), &end, 10);

    // If we didn't convert the whole string, assume it really is a name
    if (*end)
	for (i = 0; i != n_ports; ++i)
	    if (port_name_ == ports[i].name)
		break;

    if (i >= n_ports)
    {
	fprintf(stderr, "ERROR: %s: not found\n", port_name_.c_str());
	return false;
    }

    if (verbose)
	printf("INFO: Reading from Firewire port %s\n", ports[i].name);

    if (raw1394_set_port(handle.get(), i))
    {
	perror("raw1394_set_port");
	return false;
    }

    if (raw1394_iso_recv_init(handle.get(), raw_receive, /*buf_packets=*/ 600,
			      /*max_packet_size=*/
			      CIF_HEADER_SIZE + CIF_PACKET_SIZE + 8,
			      /*channel=*/ 63,
			      /*mode=*/ RAW1394_DMA_DEFAULT,
			      /*irq_interval=*/ 100))
    {
	perror("raw1394_iso_recv_init");
	return false;
    }

    if (raw1394_iso_recv_start(handle.get(), -1, -1, -1))
    {
	perror("raw1394_iso_recv_start");
	raw1394_iso_shutdown(handle.get()); // see comment on destructor
	return false;
    }

    envir().taskScheduler().turnOnBackgroundReadHandling(
	raw1394_get_fd(handle.get()), raw_poll, this);
    handle_ = handle;
    return true;
}

void firewire_source::doGetNextFrame()
{
    if (!handle_.get() && !try_open())
	handleClosure(this);
}

void firewire_source::raw_poll(void * opaque, int)
{
    firewire_source * source = static_cast<firewire_source *>(opaque);
    if (raw1394_loop_iterate(source->handle_.get()) < 0)
	handleClosure(source);
}

enum raw1394_iso_disposition
firewire_source::raw_receive(raw1394handle_t handle,
			     unsigned char * data, unsigned int len,
			     unsigned char /*channel*/, unsigned char /*tag*/,
			     unsigned char /*sy*/, unsigned int /*cycle*/,
			     unsigned int dropped)
{
    firewire_source * source =
	static_cast<firewire_source *>(raw1394_get_userdata(handle));
    source->receive(data, len, dropped);
    return RAW1394_ISO_OK;
}

void firewire_source::receive(unsigned char * data, unsigned int len,
			      unsigned int dropped)
{
    total_len_ += len;
    dropped_packets_ += dropped;

    if (fTo && len == CIF_HEADER_SIZE + CIF_PACKET_SIZE)
    {
	data += CIF_HEADER_SIZE;

	fFrameSize = std::min<unsigned int>(CIF_PACKET_SIZE, fMaxSize);
	fNumTruncatedBytes = CIF_PACKET_SIZE - fFrameSize;
	memcpy(fTo, data, fFrameSize);
	fTo = NULL;

	FramedSource::afterGetting(this);
    }
    else
    {
	++dropped_packets_;
    }
}

class firewire_subsession : public OnDemandServerMediaSubsession
{
public:
    firewire_subsession(UsageEnvironment & env, const std::string & port_name);

private:
    virtual FramedSource * createNewStreamSource(unsigned clientSessionId,
						 unsigned & estBitrate);
    virtual RTPSink * createNewRTPSink(Groupsock * rtpGroupsock,
				       unsigned char rtpPayloadTypeIfDynamic,
				       FramedSource * inputSource);
    virtual const char * getAuxSDPLine(RTPSink * rtpSink,
				       FramedSource * inputSource);

    std::string port_name_;
    std::vector<char> sdp_line_;
};

firewire_subsession::firewire_subsession(UsageEnvironment & env,
					 const std::string & port_name)
    : OnDemandServerMediaSubsession(env, /*reuseFirstSource=*/ True),
      port_name_(port_name)
{
}

FramedSource *
firewire_subsession::createNewStreamSource(unsigned /*clientSessionId*/,
					   unsigned & estBitrate)
{
    firewire_source * source = new firewire_source(envir(), port_name_);
    estBitrate = 29000; // kbps
    return DVVideoStreamFramer2::createNew(envir(), source,
					   /*sourceIsSeekable=*/ False);
}

RTPSink *
firewire_subsession::createNewRTPSink(Groupsock * rtpGroupsock,
				      unsigned char rtpPayloadTypeIfDynamic,
				      FramedSource * /*inputSource*/)
{
    return DVVideoRTPSink::createNew(envir(), rtpGroupsock,
				     rtpPayloadTypeIfDynamic);
}

const char * firewire_subsession::getAuxSDPLine(RTPSink * rtpSink,
						FramedSource * inputSource)
{
    // We should be able to call DVVideoRTPSink::getAuxSDPLine, but
    // that only works with the original DVVideoStreamFramer.  So, we
    // copy that code here.

    DVVideoStreamFramer2 * framer =
	static_cast<DVVideoStreamFramer2 *>(inputSource);
    const char * profile_name = framer->profileName();

    if (!profile_name)
	return NULL;

    static const char * sdp_format = "a=fmtp:%d encode=%s;audio=bundled\r\n";
    unsigned sdp_size = strlen(sdp_format)
	+ 3 // max payload format code length
	+ strlen(profile_name);
    sdp_line_.resize(sdp_size + 1);
    sprintf(sdp_line_.data(), sdp_format, rtpSink->rtpPayloadType(),
	    profile_name);

    return sdp_line_.data();
}

// Should be volatile sig_atomic_t, but liveMedia insists on char...
static char received_sigint;

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

    // Set up liveMedia framework
    BasicTaskScheduler * sched = BasicTaskScheduler::createNew();
    BasicUsageEnvironment * env = BasicUsageEnvironment::createNew(*sched);
    RTSPServer * server = RTSPServer::createNew(*env, 8554, NULL);
    if (server == NULL)
    {
	*env << "Failed to create RTSP server: " << env->getResultMsg() << "\n";
	return 1;
    }
    OutPacketBuffer::maxSize = DIF_MAX_FRAME_SIZE;

    // Set up session
    std::string stream_name("firewire");
    stream_name.append(fw_port_name);
    std::string stream_desc("DV stream from Firewire port ");
    stream_desc.append(fw_port_name);
    ServerMediaSession * sms =
	ServerMediaSession::createNew(*env, stream_name.c_str(),
				      stream_desc.c_str(), stream_desc.c_str());
    sms->addSubsession(new firewire_subsession(*env, fw_port_name));
    server->addServerMediaSession(sms);

    // Loop until SIGINT received
    if (verbose)
	printf("INFO: Serving at rtsp://*:8554/%s\n", stream_name.c_str());
    sched->doEventLoop(&received_sigint);

    env->reclaim();

    return 0;
}
