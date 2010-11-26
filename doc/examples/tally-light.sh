#!/bin/bash

# Filter for output of dvsource-firewire -t.
# Requires a serial device with an LED between DTR and GND.

TALLY_DEVICE="${1:-/dev/ttyUSB0}"

while read line; do
    case "$line" in
	'TALLY: on')
	    # Assert DTR
	    exec 3>"$TALLY_DEVICE"
	    ;;
	'TALLY: off')
	    # Deassert DTR
	    exec 3>/dev/null
	    ;;
	*)
	    echo "$line"
	    ;;
    esac
done
