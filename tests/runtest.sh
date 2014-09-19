#!/bin/bash

# Test basic functionality of dvswitch, using OSC commands

set -e
set -x

# We really want to exit 77 (for "SKIP"), but ctest doesn't support that...
if [ -z "$DISPLAY" ]
then
	exit 0
fi

PIDDVSW=0
PIDSRC1=0
PIDSRC2=0
PIDOUT=0
BASEDIR=$1

setup() {
	$BASEDIR/build/src/dvswitch -h 127.0.0.1 -p 1234 -o 2345 &
	PIDDVSW=$!
	sleep 1
	$BASEDIR/build/src/dvsource-file -l -h 127.0.0.1 -p 1234 $BASEDIR/tests/test1.dv &
	PIDSRC1=$!
	$BASEDIR/build/src/dvsource-file -l -h 127.0.0.1 -p 1234 $BASEDIR/tests/test2.dv &
	PIDSRC2=$!
	$BASEDIR/build/src/dvsink-files -h 127.0.0.1 -p 1234 $BASEDIR/build/tests/test-output &
	PIDOUT=$!
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

wait $PIDDVSW
wait $PIDSRC1
wait $PIDSRC2
wait $PIDOUT
