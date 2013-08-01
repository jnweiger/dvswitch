#! /bin/sh
##
## FIXME: -deinterlace is deprecated, use -filter:v yadif instead

version=0.2
startsecs=0.0
duration=


usage () 
{
echo "      
      dv2webm Version $version

      Usage: $0 [OPTIONS] INFILE.dv [... INFILE.dv]
      
      Options:
        -s,--startsecs N.N
        -s,--startsecs HH:MM:SS[.XXX]
	        Skip number of seconds from the start.
                Default: '$startsecs' .
        -t,--duration N.N
        -t,--duration HH:MM:SS[.XXX]
	        Stop after N.N seconds of video were produced.
		Default: stop when input ends.
        -o,--output  FILE.webm
	        Define the output file name. Default: Derive from 
		input file name (first input file name, if multiple).
        -h,--help
		Display this help.

      INFILE.dv ...
        One or multiple input files in dv format. The files will be concatenated
	in the given order, then a startsecs skip will be applied if any.
"
}


ARGS=`getopt -o "s:o:t:h" -l "start:,output:,duration:,help" -n "dv2webm V$version" -- "$@"`
eval set -- "$ARGS"
while true;
do
    case "$1" in
	-h|--help)
	    usage
	    exit 0;;
	-s|--startsecs)
	    startsecs="$2"
	    shift 2;;
	-o|--output)
	    outfile="$2"
	    shift 2;;
	-t|--duration)
	    duration="-t $2"
	    shift 2;;
	--)
	    shift
	    break;;
    esac
done


infile=$1
shift
if [[ "$outfile" == "" ]]; then
  outfile=${infile%.dv}
  # check if a timestamp YYYY-MM-DD is present, if not add one.
  if ! [[ "$outfile" =~ 20[0-9]{2} ]]; then
    outfile="$outfile-$(date +'%Y-%m-%d')"
  fi
  outfile="$outfile.webm"
  echo "writing output to $outfile"
fi

if [ ! -f "$infile" ]; then
  echo "input .dv file '$infile' not found"
  usage
  exit
fi

need_concat=0
for i in $*; do
 need_concat=1
 infile="$infile|$i"
done;

if [[ $need_concat ]]; then
  infile="concat:$infile"
fi

echo ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto $duration "$outfile"
sleep 3
ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto $duration "$outfile"

