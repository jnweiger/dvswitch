#! /bin/sh
##
## FIXME: -deinterlace is deprecated, use -filter:v yadif instead
## FIXME: make -ss a command line option.
## FIXME: allow multiple input files here. 

infile=$1
outfile=${infile%.dv}.webm
startsecs=0.0
test -n "$2" && startsecs=$2

if [ ! -f "$infile" ]; then
  echo "input .dv file '$infile' not found"
  echo "Usage: $0 INFILE.dv [START_POSITION_SECS]"
  exit
fi
echo ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto "$outfile"
sleep 3
ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto "$outfile"
