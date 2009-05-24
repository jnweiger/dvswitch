// Copyright 2007 Ben Hutchings.
// See the file "COPYING" for licence details.

#ifndef DVSWITCH_SERVER_HPP
#define DVSWITCH_SERVER_HPP

#include <memory>
#include <string>

#include <boost/thread.hpp>

#include "auto_fd.hpp"
#include "auto_pipe.hpp"
#include "mixer.hpp"

class server
{
public:
    server(const std::string & host, const std::string & port, mixer & mixer);
    ~server();

private:
    class connection;
    class unknown_connection;
    class source_connection;
    class sink_connection;

    void serve();

    mixer & mixer_;
    auto_fd listen_socket_;
    auto_pipe message_pipe_;
    std::auto_ptr<boost::thread> server_thread_;
};

#endif // !defined(DVSWITCH_SERVER_HPP)
