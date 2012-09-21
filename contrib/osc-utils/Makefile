CFLAGS+=`pkg-config --cflags liblo` -Wall -g
LOADLIBES+=`pkg-config --libs liblo`
LDFLAGS+=-Wl,--as-needed

all: bin man

bin: send_osc repeater_udp repeater_tcp

man: send_osc.1 repeater_udp.1 repeater_tcp.1

.PHONY: clean all bin man
clean:
	rm -f send_osc repeater_udp repeater_tcp
	rm -f send_osc.1 repeater_udp.1 repeater_tcp.1


send_osc: send_osc.c

repeater_udp: repeater_udp.c

repeater_tcp: repeater_tcp.c

send_osc.1: send_osc
	help2man -N -o send_osc.1 -n "send OpenSoundControl messages" ./send_osc

repeater_udp.1: repeater_udp
	help2man -N -o repeater_udp.1 -n "UDP message repeater" ./repeater_udp

repeater_tcp.1: repeater_tcp
	help2man -N -o repeater_tcp.1 -n "TCP message repeater" ./repeater_tcp
