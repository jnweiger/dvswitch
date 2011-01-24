#include "tally_rtsp_server.hpp"

#include <time.h>

/*
 * The dateHeader() function was copied shamelessly from the liveMedia
 * sources (modulo winCE #ifdefs). For some unfathomable reason, this function
 * is not exported.
 */
static char const* dateHeader() {
  static char buf[200];
  time_t tt = time(NULL);
  strftime(buf, sizeof buf, "Date: %a, %b %d %Y %H:%M:%S GMT\r\n", gmtime(&tt));
  return buf;
}

tally_rtsp_server::tally_rtsp_server(int pipefd, UsageEnvironment& env, int ourSocket, Port ourPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
    : RTSPServer(env, ourSocket, ourPort, authDatabase, reclamationTestSeconds),
      pipefd_(pipefd), tally_state_(TALLY_OFF)
{
    write(pipefd_, "TALLY: off\n", 11);
}

tally_rtsp_server::~tally_rtsp_server()
{
}

tally_rtsp_server* tally_rtsp_server::createNew(int pipefd, UsageEnvironment& env, Port ourPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds)
{
    int ourSocket = -1;
    do
    {
        ourSocket = setUpOurSocket(env, ourPort);
	if (ourSocket == -1) break;
        return new tally_rtsp_server(pipefd, env, ourSocket, ourPort, authDatabase, reclamationTestSeconds);
    } while (0);

    if (ourSocket != -1) ::closeSocket(ourSocket);
    return NULL;
}

RTSPServer::RTSPClientSession*
tally_rtsp_server::createNewClientSession(unsigned sessionId, int clientSocket, struct sockaddr_in clientAddr)
{
    return new RTSPClientSession(*this, sessionId, clientSocket, clientAddr);
}

tally_rtsp_server::RTSPClientSession::RTSPClientSession(tally_rtsp_server& ourServer, unsigned sessionId, int clientSocket, struct sockaddr_in clientAddr)
    : RTSPServer::RTSPClientSession(ourServer, sessionId, clientSocket, clientAddr), server_(ourServer)
{
}

tally_rtsp_server::RTSPClientSession::~RTSPClientSession()
{
}

void
tally_rtsp_server::RTSPClientSession::handleCmd_SET_PARAMETER(ServerMediaSubsession* subsession, char const* cseq, char const* fullRequestStr)
{
    enum tally newstate;
    const char* tallyloc;

    tallyloc = strstr(fullRequestStr, "TALLY:");
    if (!tallyloc) {
    	snprintf((char*)fResponseBuffer, sizeof(fResponseBuffer), "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sSession: %08X\r\n\r\n", cseq, dateHeader(), fOurSessionId);
    }
    if (strstr(tallyloc, "on"))
    {
        newstate = TALLY_ON;
    }
    else if (strstr(tallyloc, "off"))
    {
        newstate = TALLY_OFF;
    }
    else if (strstr(tallyloc, "cue"))
    {
        newstate = TALLY_CUE;
    }
    else
    {
	// invalid
	snprintf((char*)fResponseBuffer, sizeof(fResponseBuffer),
	    "RTSP/1.0 400 Bad Request\r\nCSeq: %s\r\n%sSession: %08X\r\n\r\n",
	    cseq, dateHeader(), fOurSessionId);
	return;
    }
    if (newstate != server_.tally_state_)
    {
        char tmp[strlen(tallyloc)+2];
        server_.tally_state_ = newstate;
        sprintf(tmp, "%s\n", tallyloc);
	write(server_.pipefd_, tmp, sizeof(tmp));
    }
    snprintf((char*)fResponseBuffer, sizeof(fResponseBuffer),
        "RTSP/1.0 200 OK\r\nCSeq: %s\r\n%sSession: %08X\r\n\r\n",
        cseq, dateHeader(), fOurSessionId);
}
