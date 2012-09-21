/* osc_send - simple tool to send opensoundcontrol messages
 *
 * Copyright (C) 2007, 2012 Robin Gareus <robin@gareus.org>
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
#include <string.h>
#include <lo/lo.h>

#define DFLT_PROTO "osc.udp"

static void usage(char *prog, int exitval) {
	printf("send_osc - utility to send OSC messages\n\n");
	printf("Usage: %s <dst> <msg> [parameter]*\n\n", prog);
	printf(
"Options:\n"
" <dst>         The destination to send mesasge to. The format is\n"
"               [[PROTOCOL://]HOSTNAME:]PORTNUMBER. ie.\n"
"               'osc.udp://HOST:PORT' or 'osc.tcp://HOST:PORT'.\n"
"               If the protocol prefix is omitted, '"DFLT_PROTO"' is assumed.\n"
"               When only a number is given "DFLT_PROTO"://localhost is used.\n"
" <msg>         The OSC message to send\n"
" [parameters]  OSC parameters to append to the message.\n"
"               Integer, float and string values are recognized.\n"
"\n"
"Examples:\n"
"send_osc osc.udp:192.168.0.1:9999 /my/message\n\n"
"send_osc 5675 /my/message .30 123 0.5 abC \"DEF GH\"\n"
"\n"
"Report bugs to <robin@gareus.org>.\n"
);
	exit(exitval);
}

static void printversion(char *prog) {
	printf("%s v0.2\n\n", prog);
	printf(
"Copyright (C) 2007, 2012 Robin Gareus\n"
"This is free software; see the source for copying conditions.  There is NO\n"
"warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"
"\n"
);
}

int main(int argc, char **argv) {
	if (argc==2) {
		if (!strcmp(argv[1], "-V") || !strcmp(argv[1], "--version")) {
			printversion(argv[0]);
			return (0);
		}
		if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
				usage (argv[0], 0);
		}
	}

  if (argc<2) {
		usage(argv[0], 1);
	}

	char *d = argv[1], *tmp;

	if (!strncmp(d, "osc.udp://", 10) || !strncmp(d, "osc.tcp://", 10)) {
		d=strdup(argv[1]);
	} else if (strstr(d, "://")) {
		fprintf(stderr, "invalid destination: '%s'\n", argv[1]);
		usage(argv[0], 2);
	} else if ((tmp=strchr(d, ':')) && !strchr(++tmp, ':')) {
		d = malloc((strlen(d)+11)*sizeof(char));
		sprintf(d, DFLT_PROTO"://%s", argv[1]);
	} else if (strlen(d) == strspn(d, "0123456789") && atoi(d) < 65536 && atoi(d) > 0) {
		d = malloc(26*sizeof(char));
		sprintf(d, DFLT_PROTO"://localhost:%d", atoi(argv[1]));
	} else {
		fprintf(stderr, "invalid destination: '%s'\n", argv[1]);
		usage(argv[0], 2);
	}

  lo_address t = lo_address_new_from_url(d);
  if (!t) {
		fprintf(stderr, "can not connect to: '%s'\n", d);
    return(3);
  }
  fprintf(stderr, "Sending to %s\n", d);
	free(d);

  if (argc<3) {
    lo_send(t, argv[2], "");
  } else {
		int i;
		lo_message m = lo_message_new();
    for (i = 3; i < argc; ++i) {
			const char *s = argv[i];
			if (strlen(s) == strspn(s, "+-0123456789")) {
				lo_message_add(m, "i", atoi(s));
			} else if (strlen(s) == strspn(s, ".+-0123456789")) {
				lo_message_add(m, "f", atof(s));
			} else {
				lo_message_add(m, "s", s);
			}
		}
		lo_send_message(t, argv[2], m);
		lo_message_free(m);
	}
	lo_address_free(t);
	return 0;
}
