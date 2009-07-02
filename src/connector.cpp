// Copyright 2009 Ben Hutchings.
// See the file "COPYING" for licence details.

// Network connector.  We act as an RTP/RTSP client to sources.   With
// sinks the client/server roles will be somewhat blurred.

#include <iostream>
#include <new>
#include <stdexcept>

#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

#include <BasicUsageEnvironment.hh>
#include <RTSPClient.hh>

#include "connector.hpp"

class connector::source_connection : public mixer::source
{
public:
    source_connection(mixer & mixer, BasicUsageEnvironment * env,
		      const char * session_desc);
    ~source_connection();

private:
    virtual void set_active(mixer::source_activation);

    static void handle_frame(void * opaque, unsigned frame_size,
			     unsigned trunc_size,  timeval pts,
			     unsigned duration);
    static void handle_close(void * opaque);

    mixer & mixer_;
    MediaSession * session_;
    MediaSubsession * subsession_;
    mixer::source_id id_;
    dv_frame_ptr frame_;
};

connector::source_connection::source_connection(
    mixer & mixer, BasicUsageEnvironment * env, const char * session_desc)
    : mixer_(mixer)
{
    session_ = MediaSession::createNew(*env, session_desc);
    if (!session_ ||
	!session_->initiateByMediaType("video/dv", subsession_))
	throw std::runtime_error(env->getResultMsg());

    id_ = mixer_.add_source(this);

    // Read first frame
    frame_ = allocate_dv_frame();
    subsession_->readSource()->getNextFrame(
	frame_->buffer, DIF_MAX_FRAME_SIZE,
	handle_frame, this, handle_close, this);
}

connector::source_connection::~source_connection()
{
    mixer_.remove_source(id_);
    if (subsession_)
	subsession_->readSource()->stopGettingFrames();
    if (session_)
	Medium::close(session_);
}

void connector::source_connection::set_active(mixer::source_activation)
{
    // TODO
}

void connector::source_connection::handle_frame(
    void * opaque, unsigned frame_size, unsigned trunc_size,
    timeval /*pts*/, unsigned /*duration*/)
{
    source_connection & conn = *static_cast<source_connection *>(opaque);

    if (trunc_size == 0 &&
	frame_size == dv_frame_system(conn.frame_.get())->size)
    {
	conn.mixer_.put_frame(conn.id_, conn.frame_);
	conn.frame_.reset();
	conn.frame_ = allocate_dv_frame();
    }
    else
    {
	std::cerr << "WARN: Size mismatch in frame from source " << conn.id_
		  << ":  expected " << dv_frame_system(conn.frame_.get())->size
		  << " bytes and got " << frame_size + trunc_size << " bytes\n";
    }

    conn.subsession_->readSource()->getNextFrame(
	conn.frame_->buffer, DIF_MAX_FRAME_SIZE,
	handle_frame, &conn, handle_close, &conn);    
}

void connector::source_connection::handle_close(void * opaque)
{
    delete static_cast<source_connection *>(opaque);
}

connector::connector(mixer & mixer)
    : mixer_(mixer),
      poll_env_(0),
      poll_exit_flag_(0)
{
    poll_thread_.reset(
	new boost::thread(boost::bind(&connector::run_event_loop, this)));

    BasicTaskScheduler * sched = BasicTaskScheduler::createNew();
    resolve_env_ = BasicUsageEnvironment::createNew(*sched);
}

connector::~connector()
{
    resolve_env_->reclaim();

    poll_exit_flag_ = 1;
    write(poll_pipe_.writer.get(), &poll_exit_flag_, 1);
    poll_thread_->join();
}

void connector::add_source(const std::string & uri)
{
    // Get source description from the source URI
    boost::shared_ptr<RTSPClient> client(
	RTSPClient::createNew(*resolve_env_, 0, "DVswitch"),
	static_cast<void (*)(Medium *)>(&Medium::close));
    if (!client)
	throw std::bad_alloc();
    boost::scoped_array<char> desc(client->describeURL(uri.c_str()));
    if (!desc)
	throw std::runtime_error(resolve_env_->getResultMsg());

    // Pass it over to the thread running the liveMedia event loop
    boost::mutex::scoped_lock lock(add_mutex_);
    swap(add_desc_, desc);
    write(poll_pipe_.writer.get(), &poll_exit_flag_, 1);
    add_done_.wait(lock);
}

void connector::run_event_loop()
{
    BasicTaskScheduler * sched = BasicTaskScheduler::createNew();
    poll_env_ = BasicUsageEnvironment::createNew(*sched);

    poll_env_->taskScheduler().turnOnBackgroundReadHandling(
	poll_pipe_.reader.get(), &connector::handle_request, this);
    sched->doEventLoop(&poll_exit_flag_);
    poll_env_->reclaim();
}

void connector::handle_request(void * opaque, int)
{
    connector & connr = *static_cast<connector *>(opaque);
    char dummy;
    if (read(connr.poll_pipe_.reader.get(), &dummy, 1) != 1)
	return;

    // Get source description from the UI thread
    boost::scoped_array<char> desc;
    boost::mutex::scoped_lock lock(connr.add_mutex_);
    swap(desc, connr.add_desc_);
    lock.unlock();
    connr.add_done_.notify_one();

    try
    {
	new source_connection(connr.mixer_, connr.poll_env_, desc.get());
    }
    catch (std::exception & e)
    {
	std::cerr << "ERROR: " << e.what() << "\n";
    }
}
