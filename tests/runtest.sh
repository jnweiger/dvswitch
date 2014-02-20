#!/bin/bash

# Test basic functionality of dvswitch, using OSC commands and xvfb

set -e
set -x

PID=0
BASEDIR=$1

setup() {
	xvfb-run $BASEDIR/build/src/dvswitch -h 127.0.0.1 -p 1234 -o 2345 &
	PID=$!
	sleep 1
	$BASEDIR/build/src/dvsource-file -l -h 127.0.0.1 -p 1234 $BASEDIR/tests/test1.dv &
	$BASEDIR/build/src/dvsource-file -l -h 127.0.0.1 -p 1234 $BASEDIR/tests/test2.dv &
	$BASEDIR/build/src/dvsink-files -h 127.0.0.1 -p 1234 $BASEDIR/build/tests/test-output &
}

cleanup() {
	oscsend localhost 2345 /dvswitch/app/quit
}

case $2 in
	run)
		setup
		cleanup
	;;
	mix)
		setup
		sleep 1
		oscsend localhost 2345 /dvswitch/src/pri i 1
		sleep 1
		cleanup
	;;
	full)
		rm -f $BASEDIR/build/tests/*dv
		setup
		sleep 1
		oscsend localhost 2345 /dvswitch/rec/start
		sleep 1
		oscsend localhost 2345 /dvswitch/rec/cut
		sleep 1
		oscsend localhost 2345 /dvswitch/src/pri i 1
		sleep 1
		oscsend localhost 2345 /dvswitch/rec/stop
		cleanup
		test -f $BASEDIR/build/tests/test-output.dv
		test -f $BASEDIR/build/tests/test-output-1.dv
	;;
	*)
		exit 1
esac
