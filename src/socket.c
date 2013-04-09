/* Copyright 2007-2008 Ben Hutchings.
 * See the file "COPYING" for licence details.
 */
/* Socket utility functions */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

int create_connected_socket(const char * host, const char * port)
{
    struct addrinfo addr_hints = {
	.ai_family =   AF_UNSPEC,
	.ai_socktype = SOCK_STREAM,
	.ai_flags =    AI_ADDRCONFIG
    };
    struct addrinfo * addr;
    int error;
    if ((error = getaddrinfo(host, port, &addr_hints, &addr)))
    {
	fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(error));
	exit(1);
    }

    /* XXX Should we walk the list rather than only trying the first? */
    int sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock < 0)
    {
	perror("ERROR: socket");
	exit(1);
    }
    if (connect(sock, addr->ai_addr, addr->ai_addrlen) != 0)
    {
	perror("ERROR: connect");
	exit(1);
    }

    freeaddrinfo(addr);
    return sock;
}

int create_listening_socket(const char * host, const char * port)
{
    struct addrinfo addr_hints = {
	.ai_family =   AF_UNSPEC,
	.ai_socktype = SOCK_STREAM,
	.ai_flags =    AI_ADDRCONFIG
    };
    struct addrinfo * addr;
    int error;

    if (!strcmp(host, "*"))
      {
        /*
	  * Manual page getaddrinfo(3) says:
	  * If  the  AI_PASSIVE  flag  is  specified in hints.ai_flags, and node
	  * is NULL,  then  the  returned  socket  addresses  will  be  suitable
	  * for bind(2)ing  a  socket  that  will  accept(2) connections.  The
	  * returned socket address will contain the "wildcard address"
	  */
        host = NULL;
	addr_hints.ai_flags |= AI_PASSIVE;
      }
    // fprintf(stderr, "getaddrinfo %s %s\n", host, port);

    if ((error = getaddrinfo(host, port, &addr_hints, &addr)))
    {
	fprintf(stderr, "ERROR: getaddrinfo: %s\n", gai_strerror(error));
	exit(1);
    }

    /* XXX Should we walk the list rather than only trying the first? */
    int sock = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
    if (sock < 0)
    {
	perror("ERROR: socket");
	exit(1);
    }
    static const int one = 1;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0)
    {
	perror("ERROR: setsockopt");
	exit(1);
    }
    if (bind(sock, addr->ai_addr, addr->ai_addrlen) != 0)
    {
	perror("ERROR: bind");
	exit(1);
    }
    if (listen(sock, 10) != 0)
    {
	perror("ERROR: listen");
	exit(1);
    }

    freeaddrinfo(addr);
    return sock;
}
