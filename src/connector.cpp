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

struct auto_medium_closer
{
    void operator()(Medium * medium) const { Medium::close(medium); }
};
struct auto_mediasession_factory
{
    MediaSession * operator()() const { return 0; }
};
struct auto_rtspclient_factory
{
    RTSPClient * operator()() const { return 0; }
};
typedef auto_handle<MediaSession *,
		    auto_medium_closer, auto_mediasession_factory>
auto_mediasession;
typedef auto_handle<RTSPClient *, auto_medium_closer, auto_rtspclient_factory>
auto_rtspclient;

class connector::source_connection : public mixer::source
{
public:
    source_connection(connector & connr,
		      const mixer::source_settings & settings);
    void setup(UsageEnvironment * env);
    ~source_connection();

private:
    virtual void set_active(mixer::source_activation);

    static void handle_frame(void * opaque, unsigned frame_size,
			     unsigned trunc_size,  timeval pts,
			     unsigned duration);
    static void handle_close(void * opaque);

    mixer & mixer_;
    auto_rtspclient client_;
    boost::scoped_array<char> desc_;
    auto_mediasession session_;
    MediaSubsession * subsession_;
    mixer::source_id id_;
    dv_frame_ptr frame_;
};

connector::source_connection::source_connection(
    connector & connr, const mixer::source_settings & settings)
    : mixer_(connr.mixer_),
      // Get source description from the source URI
      client_(RTSPClient::createNew(*connr.resolve_env_, 0, "DVswitch")),
      desc_(client_.get() ? client_.get()->describeURL(settings.url.c_str()) : 0)
{
    if (!desc_)
	throw std::runtime_error(connr.resolve_env_->getResultMsg());

    // Set up the session in the thread running the liveMedia event loop
    // (this will call back to setup()).
    connr.do_add_source(this);

    id_ = mixer_.add_source(this, settings);
    if (!client_.get()->setupMediaSubsession(*subsession_, false, false) ||
	!client_.get()->playMediaSession(*session_.get(), 0.0, -1.0, 1.0))
    {
	mixer_.remove_source(id_);
	throw std::runtime_error(connr.resolve_env_->getResultMsg());
    }
}

void connector::source_connection::setup(UsageEnvironment * env)
{
    session_.reset(MediaSession::createNew(*env, desc_.get()));
    if (!session_.get() ||
	!session_.get()->initiateByMediaType("video/dv", subsession_))
	throw std::runtime_error(env->getResultMsg());

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

void connector::add_source(const mixer::source_settings & settings)
{
    new source_connection(*this, settings);
}

void connector::do_add_source(source_connection * conn)
{
    boost::mutex::scoped_lock lock(add_mutex_);
    add_conn_ = conn;
    write(poll_pipe_.writer.get(), "", 1);
    add_done_.wait(lock);
    if (!add_error_.empty())
	throw std::runtime_error(add_error_);
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
    if (read(connr.poll_pipe_.reader.get(), &dummy, 1) != 1 ||
	connr.poll_exit_flag_)
	return;

    boost::mutex::scoped_lock lock(connr.add_mutex_);
    try
    {
	connr.add_conn_->setup(connr.poll_env_);
	connr.add_error_.clear();
    }
    catch (std::exception & e)
    {
	connr.add_error_ = e.what();
    }
    lock.unlock();
    connr.add_done_.notify_one();
}
