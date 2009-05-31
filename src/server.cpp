// Copyright 2007-2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Server for the original network protocol

#include <cstring>
#include <functional>
#include <iostream>
#include <ostream>
#include <stdexcept>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include "frame.h"
#include "mixer.hpp"
#include "os_error.hpp"
#include "protocol.h"
#include "server.hpp"
#include "socket.h"

namespace
{
    // Numbers used in the message pipe
    enum {
	message_quit = -1
    };
}

// connection: base class for client connections

class server::connection
{
public:
    enum send_status {
	send_failed,
	sent_some,
	sent_all
    };

    virtual ~connection() {}
    connection * do_receive();
    virtual send_status do_send() { return send_failed; }

protected:
    struct receive_buffer
    {
	receive_buffer()
	    : pointer(0),
	      size(0)
	{}
	receive_buffer(uint8_t * pointer, std::size_t size)
	    : pointer(pointer),
	      size(size)
	{}
	uint8_t * pointer;
	std::size_t size;
    };

    connection(server & server, auto_fd socket);

    void schedule_send()
    {
	int fd = socket_.get();
	os_check_zero(
	    "write",
	    write(server_.message_pipe_.writer.get(), &fd, sizeof(int))
	    - sizeof(int));
    }

    server & server_;
    auto_fd socket_;

private:
    virtual receive_buffer get_receive_buffer() = 0;
    virtual connection * handle_complete_receive() = 0;
    virtual std::ostream & print_identity(std::ostream &) = 0;

    receive_buffer receive_buffer_;
};

// unknown_connection: connection where client type is unknown as yet

class server::unknown_connection : public connection
{
public:
    unknown_connection(server & server, auto_fd socket);

private:
    virtual receive_buffer get_receive_buffer();
    virtual connection * handle_complete_receive();
    virtual std::ostream & print_identity(std::ostream &);

    uint8_t greeting_[4];
};

// source_connection: connection from source

class server::source_connection : public connection, private mixer::source
{
public:
    source_connection(server & server, auto_fd socket, bool wants_act = false);
    virtual ~source_connection();

private:
    virtual send_status do_send();
    virtual receive_buffer get_receive_buffer();
    virtual connection * handle_complete_receive();
    virtual std::ostream & print_identity(std::ostream &);

    virtual void set_active(mixer::source_activation);

    dv_frame_ptr frame_;
    bool first_sequence_;
    bool wants_act_;		// client wants activation messages
    mixer::source_activation act_flags_;
    char act_message_[ACT_MSG_SIZE];
    std::size_t act_message_pos_;

    mixer::source_id source_id_;
};

// sink_connection: connection from sink

class server::sink_connection : public connection, private mixer::sink
{
public:
    sink_connection(server &, auto_fd socket, bool is_raw, bool will_record);
    virtual ~sink_connection();

private:
    struct queue_elem
    {
	dv_frame_ptr frame;
	bool overflow_before;
    };

    virtual send_status do_send();
    virtual receive_buffer get_receive_buffer();
    virtual connection * handle_complete_receive();
    virtual std::ostream & print_identity(std::ostream &);

    virtual void put_frame(const dv_frame_ptr & frame);

    receive_buffer handle_unexpected_input();

    bool is_raw_;
    bool will_record_;
    bool is_recording_;
    mixer::sink_id sink_id_;
    std::size_t frame_pos_;

    boost::mutex mutex_; // controls access to the following
    ring_buffer<queue_elem, 30> queue_;
    bool overflowed_;
};

// server implementation

server::server(const std::string & host, const std::string & port,
	       mixer & mixer)
    : mixer_(mixer),
      listen_socket_(create_listening_socket(host.c_str(), port.c_str())),
      message_pipe_(O_NONBLOCK, O_NONBLOCK)
{
    server_thread_.reset(new boost::thread(boost::bind(&server::serve, this)));
}

server::~server()
{
    static const int message = message_quit;
    write(message_pipe_.writer.get(), &message, sizeof(int));
    server_thread_->join();
}

void server::serve()
{
    enum {
	poll_index_message,
	poll_index_listen,
	poll_count_fixed,
	poll_index_clients = poll_count_fixed
    };
    std::vector<pollfd> poll_fds(poll_count_fixed);
    std::vector<std::tr1::shared_ptr<connection> > connections;
    poll_fds[poll_index_message].fd = message_pipe_.reader.get();
    poll_fds[poll_index_message].events = POLLIN;
    poll_fds[poll_index_listen].fd = listen_socket_.get();
    poll_fds[poll_index_listen].events = POLLIN;

    for (;;)
    {
	int count = poll(&poll_fds[0], poll_fds.size(), -1);
	if (count < 0)
	{
	    int error = errno;
	    if (error == EAGAIN || error == EINTR)
		continue;
	    std::cerr << "ERROR: poll: " << std::strerror(errno) << "\n";
	    break;
	}

	// Check message pipe
	if (poll_fds[poll_index_message].revents & POLLIN)
	{
	    int messages[1024];
	    ssize_t size = read(message_pipe_.reader.get(),
				messages, sizeof(messages));
	    if (size > 0)
	    {
		assert(size % sizeof(int) == 0);
		for (std::size_t i = 0; i != size / sizeof(int); ++i)
		{
		    if (messages[i] == message_quit)
			return;
		    // otherwise message is the number of a file
		    // descriptor we want to send on
		    for (std::size_t j = poll_index_clients;
			 j != poll_fds.size();
			 ++j)
		    {
			if (poll_fds[j].fd == messages[i])
			{
			    poll_fds[j].events |= POLLOUT;
			    break;
			}
		    }
		}
	    }
	}

	// Check listening socket
	if (poll_fds[poll_index_listen].revents & POLLIN)
	{
	    auto_fd conn_socket(accept(listen_socket_.get(), 0, 0));
	    try
	    {
		os_check_nonneg("accept", conn_socket.get());
		os_check_nonneg("fcntl",
				fcntl(conn_socket.get(), F_SETFL, O_NONBLOCK));
		pollfd new_poll_fd = { conn_socket.get(), POLLIN, 0 };
		poll_fds.reserve(poll_fds.size() + 1);
		connections.push_back(
		    std::tr1::shared_ptr<connection>(
			new unknown_connection(*this, conn_socket)));
		poll_fds.push_back(new_poll_fd);
	    }
	    catch (std::exception & e)
	    {
		std::cerr << "ERROR: " << e.what() << "\n";
	    }
	}

	// Check client connections
	for (std::size_t i = 0; i != connections.size();)
	{
	    short revents = poll_fds[poll_index_clients + i].revents;
	    bool should_drop = false;
	    try
	    {
		if (revents & (POLLHUP | POLLERR))
		{
		    should_drop = true;
		}
		else if (revents & POLLIN)
		{
		    connection * new_connection = connections[i]->do_receive();
		    if (!new_connection)
			should_drop = true;
		    else if (new_connection != connections[i].get())
			connections[i].reset(new_connection);
		}
		else if (revents & POLLOUT)
		{
		    switch (connections[i]->do_send())
		    {
		    case connection::send_failed:
			should_drop = true;
			break;
		    case connection::sent_some:
			break;
		    case connection::sent_all:
			poll_fds[poll_index_clients + i].events &= ~POLLOUT;
		        break;
		    }
		}
	    }
	    catch (std::exception & e)
	    {
		std::cerr << "ERROR: " << e.what() << "\n";
		should_drop = true;
	    }

	    if (should_drop)
	    {
		connections.erase(connections.begin() + i);
		poll_fds.erase(poll_fds.begin() + 2 + i);
	    }
	    else
	    {
		++i;
	    }
	}
    }
}

// connection

server::connection::connection(server & server, auto_fd socket)
    : server_(server),
      socket_(socket)
{}

server::connection * server::connection::do_receive()
{
    connection * result = 0;

    if (receive_buffer_.size == 0)
    {
	receive_buffer_ = get_receive_buffer();
	assert(receive_buffer_.pointer && receive_buffer_.size);
    }

    ssize_t received_size = read(socket_.get(),
				 receive_buffer_.pointer,
				 receive_buffer_.size);
    if (received_size > 0)
    {
	receive_buffer_.pointer += received_size;
	receive_buffer_.size -= received_size;
	if (receive_buffer_.size == 0)
	    result = handle_complete_receive();
	else
	    result = this;
    }
    else if (received_size == -1 && errno == EWOULDBLOCK)
    {
	// This is expected when the socket buffer is empty
	result = this;
    }

    if (!result)
    {
	// XXX We should distinguish several kinds of failure: network
	// problems, normal disconnection, protocol violation, and
	// resource allocation failure.
	std::cerr << "WARN: Dropping connection from ";
	print_identity(std::cerr) << "\n";
    }

    return result;
}

// unknown_connection implementation

server::unknown_connection::unknown_connection(server & server, auto_fd socket)
    : connection(server, socket)
{}

server::connection::receive_buffer
server::unknown_connection::get_receive_buffer()
{
    return receive_buffer(greeting_, sizeof(greeting_));
}

server::connection * server::unknown_connection::handle_complete_receive()
{
    enum {
	client_type_unknown,
	client_type_source,     // source which sends greeting (>= 0.3)
	client_type_act_source, // source which wants activity (tally)
				// notifications
	client_type_sink,       // sink which wants DIF with control headers
	client_type_raw_sink,   // sink which wants raw DIF
	client_type_rec_sink,   // sink which wants DIF with control headers
	                        // and is recording
    } client_type;

    if (std::memcmp(greeting_, GREETING_SOURCE, GREETING_SIZE) == 0)
	client_type = client_type_source;
    else if (std::memcmp(greeting_, GREETING_SINK, GREETING_SIZE)
	     == 0)
	client_type = client_type_sink;
    else if (std::memcmp(greeting_, GREETING_RAW_SINK, GREETING_SIZE)
	     == 0)
	client_type = client_type_raw_sink;
    else if (std::memcmp(greeting_, GREETING_REC_SINK, GREETING_SIZE)
	     == 0)
	client_type = client_type_rec_sink;
    else if (std::memcmp(greeting_, GREETING_ACT_SOURCE, GREETING_SIZE)
    	     == 0)
    	client_type = client_type_act_source;
    else
	client_type = client_type_unknown;

    switch (client_type)
    {
    case client_type_source:
    case client_type_act_source:
	return new source_connection(server_, socket_,
				     client_type == client_type_act_source);
    case client_type_sink:
    case client_type_raw_sink:
    case client_type_rec_sink:
	return new sink_connection(server_, socket_,
				   client_type == client_type_raw_sink,
				   client_type == client_type_rec_sink);
    default:
	return 0;
    }
}

std::ostream & server::unknown_connection::print_identity(std::ostream & os)
{
    return os << "unknown client";
}

// source_connection implementation

server::source_connection::source_connection(server & server, auto_fd socket,
					     bool wants_act)
    : connection(server, socket),
      frame_(allocate_dv_frame()),
      first_sequence_(true),
      wants_act_(wants_act)
{
    source_id_ = server_.mixer_.add_source(this);
}

server::source_connection::~source_connection()
{
    server_.mixer_.remove_source(source_id_);
}

void server::source_connection::set_active(mixer::source_activation flags)
{
    if (wants_act_)
    {
	act_flags_ = flags;
	schedule_send();
    }
}

server::connection::send_status server::source_connection::do_send()
{
    send_status result = send_failed;

    if (act_message_pos_ == 0)
    {
	// Generate message
	memset(act_message_, 0, ACT_MSG_SIZE);
	act_message_[ACT_MSG_VIDEO_POS] =
	    !!(act_flags_ & mixer::source_active_video);
    }

    ssize_t sent_size = write(socket_.get(), act_message_, ACT_MSG_SIZE);
    if (sent_size > 0)
    {
	act_message_pos_ += sent_size;
	if (act_message_pos_ == ACT_MSG_SIZE)
	{
	    // We've finished sending this message, but we must check
	    // whether the activation flags have changed.
	    act_message_pos_ = 0;
	    if (act_message_[ACT_MSG_VIDEO_POS] ==
		!!(act_flags_ & mixer::source_active_video))
		result = sent_all;
	    else
		result = sent_some;
	}
    }
    else if (sent_size == -1 && errno == EWOULDBLOCK)
    {
	result = sent_some;
    }

    if (result == send_failed)
    {
	// XXX We should distinguish several kinds of failure: network
	// problems, normal disconnection, protocol violation, and
	// resource allocation failure.
	std::cerr << "WARN: Dropping connection from source "
		  << 1 + source_id_ << "\n";
    }

    return result;
}

server::connection::receive_buffer
server::source_connection::get_receive_buffer()
{
    if (first_sequence_)
	return receive_buffer(frame_->buffer, DIF_SEQUENCE_SIZE);
    else
	return receive_buffer(frame_->buffer + DIF_SEQUENCE_SIZE,
			      dv_frame_system(frame_.get())->size
			      - DIF_SEQUENCE_SIZE);
}

server::connection * server::source_connection::handle_complete_receive()
{
    if (!first_sequence_)
    {
 	server_.mixer_.put_frame(source_id_, frame_);
 	frame_.reset();
 	frame_ = allocate_dv_frame();
    }

    first_sequence_ = !first_sequence_;
    return this;
}

std::ostream & server::source_connection::print_identity(std::ostream & os)
{
    return os << "source " << 1 + source_id_;
}

// sink_connection implementation

server::sink_connection::sink_connection(server & server, auto_fd socket,
					 bool is_raw, bool will_record)
    : connection(server, socket),
      is_raw_(is_raw),
      will_record_(will_record),
      is_recording_(false),
      frame_pos_(0),
      overflowed_(false)
{
    sink_id_ = server_.mixer_.add_sink(this);
}

server::sink_connection::~sink_connection()
{	  
    server_.mixer_.remove_sink(sink_id_);
}

server::connection::send_status server::sink_connection::do_send()
{
    send_status result = send_failed;
    bool finished_frame = false;

    do
    {
	struct queue_elem elem;
	{
	    boost::mutex::scoped_lock lock(mutex_);
	    if (finished_frame)
	    {
		if (will_record_)
		    is_recording_ = queue_.front().frame->do_record;
		queue_.pop();
		finished_frame = false;
	    }
	    if (queue_.empty())
	    {
		result = sent_all;
		break;
	    }
	    elem = queue_.front();
	}

	if (will_record_ && !is_recording_ && !elem.frame->do_record)
	{
	    finished_frame = true;
	    continue;
	}

	uint8_t frame_header[SINK_FRAME_HEADER_SIZE] = {};
	iovec vector[2];
	int vector_size;
	std::size_t frame_size;

	if (is_raw_)
	{
	    vector_size = 0;
	    frame_size = 0;
	}
	else
	{
	    uint8_t & flag = frame_header[SINK_FRAME_CUT_FLAG_POS];
	    if (is_recording_ && !elem.frame->do_record)
		flag = SINK_FRAME_CUT_STOP;
	    else if (elem.overflow_before)
		flag = SINK_FRAME_CUT_OVERFLOW;
	    else if (elem.frame->cut_before)
		flag = SINK_FRAME_CUT_CUT;
	    else
		flag = 0;
	    // rest of header left as zero for expansion

	    vector[0].iov_base = frame_header;
	    vector[0].iov_len = SINK_FRAME_HEADER_SIZE;
	    vector_size = 1;
	    frame_size = SINK_FRAME_HEADER_SIZE;
	}

	if (!will_record_ || elem.frame->do_record)
	{
	    vector[vector_size].iov_base = elem.frame->buffer;
	    vector[vector_size].iov_len =
		dv_frame_system(elem.frame.get())->size;
	    ++vector_size;
	    frame_size += dv_frame_system(elem.frame.get())->size;
	}

	int vector_pos = 0;
	std::size_t rel_pos = frame_pos_;
	while (rel_pos >= vector[vector_pos].iov_len)
	    rel_pos -= vector[vector_pos++].iov_len;
	vector[vector_pos].iov_base =
	    static_cast<char *>(vector[vector_pos].iov_base) + rel_pos;
	vector[vector_pos].iov_len -= rel_pos;
	  
	ssize_t sent_size = writev(socket_.get(),
				   vector + vector_pos,
				   vector_size - vector_pos);
	if (sent_size > 0)
	{
	    frame_pos_ += sent_size;
	    if (frame_pos_ == frame_size)
	    {
		finished_frame = true;
		frame_pos_ = 0;
	    }
	    result = sent_some;
	}
	else if (sent_size == -1 && errno == EWOULDBLOCK)
	{
	    result = sent_some;
	}
    }
    while (finished_frame);

    if (result == send_failed)
    {
	// XXX We should distinguish several kinds of failure: network
	// problems, normal disconnection, protocol violation, and
	// resource allocation failure.
	std::cerr << "WARN: Dropping connection from sink "
		  << 1 + sink_id_ << "\n";
    }

    return result;
}

server::connection::receive_buffer
server::sink_connection::get_receive_buffer()
{
    static uint8_t dummy;
    return receive_buffer(&dummy, sizeof(dummy));
}

server::connection * server::sink_connection::handle_complete_receive()
{
    return 0;
}

std::ostream & server::sink_connection::print_identity(std::ostream & os)
{
    return os << "sink " << 1 + sink_id_;
}

void server::sink_connection::put_frame(const dv_frame_ptr & frame)
{
    bool was_empty = false;
    {
	boost::mutex::scoped_lock lock(mutex_);
	if (queue_.full())
	{
	    if (!overflowed_)
	    {
		std::cerr << "WARN: ";
		print_identity(std::cerr) << " overflowed\n";
		overflowed_ = true;
	    }
	}
	else
	{
	    struct queue_elem elem = { frame, overflowed_ };
	    if (overflowed_)
	    {
		std::cout << "INFO: ";
		print_identity(std::cout) << " recovered\n";
		overflowed_ = false;
	    }
	    if (queue_.empty())
		was_empty = true;
	    queue_.push(elem);
	}
    }
    if (was_empty)
	schedule_send();
}
