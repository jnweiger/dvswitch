// Copyright 2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Network connector.  We act as an RTP/RTSP client to sources.   With
// sinks the client/server roles will be somewhat blurred.

#ifndef DVSWITCH_CONNECTOR_HPP
#define DVSWITCH_CONNECTOR_HPP

#include <memory>
#include <queue>
#include <string>

#include <boost/scoped_array.hpp>
#include <boost/thread.hpp>

#include "auto_pipe.hpp"
#include "mixer.hpp"

class BasicUsageEnvironment;

class connector
{
public:
    explicit connector(mixer &);
    ~connector();
    void add_source(const mixer::source_settings &);

private:
    class source_connection;

    void do_add_source(source_connection *);
    void run_event_loop();
    static void handle_request(void *, int);

    mixer & mixer_;

    // for liveMedia event loop (in BasicUsageEnvironment)
    BasicUsageEnvironment * poll_env_;
    std::auto_ptr<boost::thread> poll_thread_; // thread to run the event loop
    auto_pipe poll_pipe_; // pipe it will poll (along with the sockets)
    char poll_exit_flag_; // exit request flag it will check on wakeup

    // for requesting addition of new connections
    boost::mutex add_mutex_;
    source_connection * add_conn_;
    std::string add_error_;
    boost::condition add_done_;

    // environment for resolving URIs to SDP
    // (this can only be done synchronously, so we do it in the UI thread)
    BasicUsageEnvironment * resolve_env_;
};

#endif // !defined(DVSWITCH_CONNECTOR_HPP)
