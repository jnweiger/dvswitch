/* udp_repeater - dump or forward IP/UDP messages
 *
 * Copyright (C) 1999, 2006, 2012 Robin Gareus <robin@gareus.org>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#ifdef HAVE_WINDOWS
#include <windows.h>
#include <winsock.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>  
#include <signal.h>
#include <netdb.h>
#endif


typedef struct {
	int sock;
	struct sockaddr_in addr;
	char *hostname; // debug info
	int port; // debug info
} remoteconnection;


static void usage(char *name, int exitval) {
	printf("repeater_udp - utility to dump, forward and replicate messages\n\n");
	printf("Usage: %s <listen-port> [ [host]:<port> ]*\n\n", name);
	printf(
"UDP message repeater - this program listens on a local UDP port and\n"
"forward messages to one or more [remote] UDP ports.\n"
"If the hostname is not specified, 'localhost' is used.\n"
"When no forwarding address is given, raw UDP messages are printed to stdout\n"
"\n"
"Examples:\n"
"repeater_udp 3333\n"
"  Print UDP message arriving on port 3333 to stdout\n"
"repeater_udp 3333 :3334 :3335 192.168.6.66:666 example.org:3333\n"
"  Forward UDP message arriving on port 3333 to port 3334, 3335 on localhost\n"
"  as well as to port 666 on 192.168.6.66 and 3333 example.org\n"
"\n"
"Report bugs to <robin@gareus.org>.\n"
);
	exit(exitval);
}

static void printversion(char *prog) {
	printf("%s v0.1\n\n", prog);
	printf(
"Copyright (C) 1999, 2006, 2012 Robin Gareus\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
"\n"
);
}

void error(const char *format,...) {
	va_list arglist;
	char text[BUFSIZ];
	va_start(arglist, format);
	vsnprintf(text, BUFSIZ, format, arglist);
	va_end(arglist);
	text[BUFSIZ -1]=0;
	perror(text);
	exit(0);
}

int splithp (char *arg, char **host, int *port) {
	char *tmp = strchr(arg, ':');
	if (!tmp) return (1);

	*tmp=0;
	if (port) *port=atoi(tmp+1);
	if (host) { 
		if (arg == tmp) *host=strdup("localhost");
		else *host=strdup(arg);
	}
	*tmp=':';
	return (0);
}

void open_rc(remoteconnection *rc, char *host, int port) {
	struct hostent *hp;
	rc->sock=socket(AF_INET, SOCK_DGRAM, 0);
	if (rc->sock < 0) error("socket");
	rc->addr.sin_family=AF_INET;
	hp=gethostbyname(host);
	if (!hp) error("Unknown host: '%s'", host);
	memmove((char *)&rc->addr.sin_addr, (char *)hp->h_addr, hp->h_length);
	rc->addr.sin_port=htons(port);
}

void close_rc(remoteconnection *rc) {
	close(rc->sock);
}

int send_rc (remoteconnection *rc, char *buffer, size_t len) {
	int n;
	socklen_t length=sizeof(struct sockaddr_in);
	n=sendto(rc->sock, buffer, len, 0, (struct sockaddr *)&(rc->addr), length);
	if (n < 0)
		error("Sendto (%s:%i)", rc->hostname, rc->port);
	return (0);
}

void setnonblock(int sock, unsigned long l) {
#ifdef HAVE_WINDOWS
  //WSAAsyncSelect(sock, 0, 0, FD_CONNECT|FD_CLOSE|FD_WRITE|FD_READ|FD_OOB|FD_ACCEPT);
  //setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &val,  sizeof(int));
  if(ioctlsocket(sock, FIONBIO, &l)<0)
#else
  if(ioctl(sock, FIONBIO, &l)<0)
#endif
    printf("WARNING: unable to set (non)blocking mode: %s\n", strerror(errno));
}


static int run;

#ifndef HAVE_WINDOWS
void catchsig (int sig) {
	signal(SIGHUP, catchsig);
	signal(SIGINT, catchsig);
	run=0;
}
#endif

int main(int argc, char *argv[]) {
	int want_dump=0;
	int lport;
	int sock, n, i;
	size_t length, fromlen;
	struct sockaddr_in addr;
	struct sockaddr_in from;
	char buf[BUFSIZ];

	remoteconnection *rc = NULL; 

	if (argc == 2) {
		if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
			printversion(argv[0]);
			return (0);
		}
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
				usage (argv[0], 0);
		}
	}

	if (argc < 2)
		usage(argv[0], 1);

	lport = atoi(argv[1]);
	rc=calloc((argc-2), sizeof(remoteconnection));

	if (argc == 2) {
		want_dump = 1;	
		//printf("raw data dump mode!\n");
	}
	for (i=2; i < argc; i++) {
		char *hostname=NULL;
		int port=0;
		if(!splithp(argv[i], &hostname, &port)) {
			open_rc(&(rc[(i-2)]), hostname, port);
			rc[(i-2)].port=port;
			rc[(i-2)].hostname=hostname;
			//printf(" dup to h:%s p:%i\n", hostname, port);
		}
	}

	sock=socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		error("Opening socket");

	setnonblock(sock, 1);

	int val=1;
#ifndef HAVE_WINDOWS
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &val,  sizeof(int));
#else
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (void*) &val,  sizeof(int));
#endif

	length = sizeof(addr);
	bzero(&addr, length);
	addr.sin_family=AF_INET;
	addr.sin_addr.s_addr=INADDR_ANY;
	addr.sin_port=htons(lport);
	if (bind(sock, (struct sockaddr *)&addr, length)<0)
		error("binding to port %i failed.", lport);
	fromlen = sizeof(struct sockaddr_in);

	run=1;
#ifndef HAVE_WINDOWS
	signal (SIGHUP, catchsig);
	signal (SIGINT, catchsig);
#endif

	while (run) {
    fd_set rfds;
    struct timeval tv;
    tv.tv_sec = 1; tv.tv_usec = 0;

    FD_ZERO(&rfds);
    FD_SET(sock, &rfds);
    if((select(sock+1, &rfds, NULL, NULL, &tv))<0) {
      if (errno!=EINTR) {
        break;
			}
			continue;
    }
    if(!FD_ISSET(sock, &rfds)) 
			continue;

		n = recvfrom(sock, buf, BUFSIZ, 0, (struct sockaddr *)&from, &fromlen);
		if (n < 0)
			error("recvfrom");
		if (want_dump) {
			//printf("Received a datagram: ");
			write(1, buf, n);
			//printf("\n"); 
			fsync(1);
		}
		for (i=2; i < argc; i++) {
			send_rc(&(rc[i-2]), buf, n);
		}
	}

	for (i=2; i < argc; i++) {
		close_rc(&(rc[i-2]));
		free(rc[(i-2)].hostname);
	}
#ifndef HAVE_WINDOWS
    close(sock); 
#else
    closesocket(sock);
#endif

	return (0);
}
