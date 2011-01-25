/*
 * Copyright 2011 Wouter Verhelst
 * See the file "COPYING" for license details
 */
#ifndef INC_TALLY_RTSP_SERVER_HPP
#define INC_TALLY_RTSP_SERVER_HPP

#ifndef _RTSP_SERVER_HH
#include "RTSPServer.hh"
#endif

class tally_rtsp_server: public RTSPServer
{
public:
    static tally_rtsp_server* createNew(int pipefd, UsageEnvironment& env, Port ourPort=554, UserAuthenticationDatabase* authDatabase=NULL, unsigned reclamationTestSeconds=65);
protected:
    tally_rtsp_server(int pipefd, UsageEnvironment& env, int ourSocket, Port ourPort, UserAuthenticationDatabase* authDatabase, unsigned reclamationTestSeconds);
    virtual ~tally_rtsp_server();
    virtual RTSPServer::RTSPClientSession* createNewClientSession(unsigned sessionId, int clientSocket, struct sockaddr_in clientAddr);
private:
    int pipefd_;
    enum tally {
        TALLY_ON,
        TALLY_OFF,
        TALLY_CUE
    } tally_state_;
    class RTSPClientSession: public RTSPServer::RTSPClientSession
    {
    public:
        RTSPClientSession(tally_rtsp_server& ourServer, unsigned sessionId, int clientSocket, struct sockaddr_in clientAddr);
	virtual ~RTSPClientSession();
    protected:
	virtual void handleCmd_SET_PARAMETER(ServerMediaSubsession* subsession, char const* cseq, char const* fullRequestStr);
    private:
        tally_rtsp_server& server_;
    };
};

#endif
