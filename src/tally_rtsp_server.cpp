/*
 * Copyright 2011 Wouter Verhelst
 * See the file "COPYING" for license details.
 */
#include "tally_rtsp_server.hpp"

#include <string>

#include <time.h>

/*
 * The dateHeader() function was copied shamelessly from the liveMedia
 * sources (modulo winCE #ifdefs), which is Copyright 2010 Live Networks, Inc.
 * For some unfathomable reason, this function is not exported.
 */
static char const* dateHeader() {
  static char buf[200];
  time_t tt = time(NULL);
  strftime(buf, sizeof buf, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&tt));
  return buf;
}

tally_rtsp_server::tally_rtsp_server(int pipefd, bool verbose, UsageEnvironment& env, int ourSocket, Port ourPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
    : RTSPServer(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds),
      pipefd_(pipefd), tally_state_(TALLY_OFF), verbose_(verbose)
{
    if (verbose)
    	printf("INFO: initializing tally to off\n");
    if (write(pipefd_, "TALLY: off\n", 11) < 0 && verbose)
    {
        perror("write to pipe");
    }
}

tally_rtsp_server::~tally_rtsp_server()
{
}

tally_rtsp_server* tally_rtsp_server::createNew(int pipefd, bool verbose, UsageEnvironment& env, Port ourPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
{
    int ourSocket = -1;
    do
    {
        ourSocket = setUpOurSocket(env, ourPort);
	if (ourSocket == -1) break;
        return new tally_rtsp_server(pipefd, verbose, env, ourSocket, ourPort, authDatabase, reclamationTestSeconds);
    } while (0);

    if (ourSocket != -1) ::closeSocket(ourSocket);
    return NULL;
}

RTSPServer::RTSPClientConnection*
tally_rtsp_server::createNewClientConnection(int clientSocket, struct sockaddr_in clientAddr)
{
    return new RTSPClientConnection(*this, clientSocket, clientAddr);
}

tally_rtsp_server::RTSPClientConnection::RTSPClientConnection(tally_rtsp_server& ourServer, int clientSocket, struct sockaddr_in clientAddr)
    : RTSPServer::RTSPClientConnection(ourServer, clientSocket, clientAddr), server_(ourServer)
{
}

tally_rtsp_server::RTSPClientConnection::~RTSPClientConnection()
{
}

void
tally_rtsp_server::RTSPClientConnection::handleCmd_SET_PARAMETER(ServerMediaSubsession* subsession __attribute__((unused)), char const* cseq, char const* fullRequestStr)
{
    enum tally newstate;
    const char* tallyloc;

    tallyloc = strstr(fullRequestStr, "TALLY:");
    if (!tallyloc) {
    	setRTSPResponse("RTSP/1.0 200 OK");
    }
    char* ptr;
    if ((ptr = (char*)strchr(tallyloc, '\r')))
    {
        *ptr = '\0';
    }
    if (strstr(tallyloc, "on"))
    {
        newstate = TALLY_ON;
	if (server_.verbose_)
	    printf("Enabling tally light\n");
    }
    else if (strstr(tallyloc, "off"))
    {
        newstate = TALLY_OFF;
	if (server_.verbose_)
	    printf("Disabling tally light\n");
    }
    else if (strstr(tallyloc, "cue"))
    {
        newstate = TALLY_CUE;
	if (server_.verbose_)
	    printf("Enabling cue light\n");
    }
    else
    {
	// invalid
	setRTSPResponse("RTSP/1.0 400 Bad Request");
	return;
    }
    if (newstate != server_.tally_state_)
    {
        std::string tmp(tallyloc);
	tmp += "\n";
        server_.tally_state_ = newstate;
	if(write(server_.pipefd_, tmp.c_str(), tmp.length()) < 0 && server_.verbose_)
	{
	    perror("write to pipe");
	}
    }
    setRTSPResponse("RTSP/1.0 200 OK");
}
