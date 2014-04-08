#! /bin/sh
##
## FIXME: -deinterlace is deprecated, use -filter:v yadif instead
## 2013-08-17, v0.3, jw	-- added --bitrate option.
## 2013-11-19, v0.4, jw	-- added --cwd option to strip the full path.
## 2014-04-08, v0.5, jw -- pumped up the default bitrate according to tests by darix.


version=0.5
startsecs=0.0
bitrate="b:v 500k"
duration=
cwd=


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

usage () 
{
echo "      
      dv2webm Version $version

      Usage: $0 [OPTIONS] INFILE.dv [... INFILE.dv]
      
      Options:
        -b,--bitrate NNNk
		Encode at the specified bitrate. 
		Default: ffmpeg's choice, often 400k.
	-c,--cwd
		Drop output files in current working directory.
		Default: same path where input is.
		This is exclusive to -o .
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


ARGS=`getopt -o "s:o:t:b:hc" -l "start:,output:,duration:,bitrate:,help,cwd" -n "dv2webm V$version" -- "$@"`
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
	-c|--cwd)
	    cwd=1
	    shift 1;;
	-o|--output)
	    outfile="$2"
	    shift 2;;
	-t|--duration)
	    duration="-t $2"
	    shift 2;;
	-b|--bitrate)
	    bitrate="-b:v $2"
	    shift 2;;
	--)
	    shift
	    break;;
    esac
done


infile=$1
shift
if [[ "$outfile" == "" ]]; then
  if [[ "$cwd" == "" ]]; then
    outfile=${infile%.dv}
  else
    outfile=$(basename $infile .dv)
  fi
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

echo ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto $duration $bitrate "$outfile"
sleep 3
ffmpeg -ss $startsecs -i "$infile" -deinterlace -threads auto $duration $bitrate "$outfile"

