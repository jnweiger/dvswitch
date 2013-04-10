#! /bin/sh
# simple wrapper for simple recording sessions

name=$1
test -z "$name" && name=recording

control_c()
{
  echo "exiting ..."
  killall dvsource-firewire
  killall dvsink-files
}

trap control_c SIGINT

dvsource-firewire -R &
dvsink-files -R $name &
dvswitch
control_c
