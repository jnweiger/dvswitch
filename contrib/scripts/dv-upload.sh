#!/bin/sh

# Script to easily upload videos to nue suse streaming server
# Kilian Petsch <kpetsch@suse.de>
#
# 
# 2013-01-31, V0.2  jw -- made it work also without metafile.txt

version=0.2

FILENAME=''
DIRECTORY=''
SERVER='ftp@streaming.nue.suse.com'
URL='http://streaming.nue.suse.com'
METAFILENAME=''
ALLFILE=''
LIST=''
OVERWRITE=''

usage () 
{
# Usage section
# Explain commands etc.
echo "      
      This script allows for easy uploading of videos to the SUSE streaming server.
      It automatically checks if the filename has the correct format, if it's
      already uploaded and if the upload was successful.

      Usage:
      dv-upload [OPTIONS] [FILE]
      
      Options:
        -d,--directory: Specify the directory you want to upload to.
                        Default upload directory is upload/.
        -l,--list: List directories on the server. Use '-d' to specify which one.
        -k,--key: Uses ssh-copy-id to authorize your key with the server.
                  This only works if you already have a rsa-key:
                  Use 'ssh-keygen -t rsa' to generate one.
        -o,--overwrite: Allow overwrite of existing files. Default: abort.
        -h,--help: Display this help.

      [FILE]
        Specifiy the file you want to upload. Filename needs to contain a date.
        If you want to add a metadata file, make sure it is in the same directory,
        has the same name and the file extension '.txt'


"
}

file_on_server () 
{

# Head request via curl to check if the file is already on the server
filecheck=$(mktemp /tmp/filecheck_XXXXXX.txt)
curl -sI "$URL/$DIRECTORY/$FILENAME" > $filecheck

if [[ $? = '0' ]]; then
   # Check the file for 404 Error
   grep -q '404 Not Found' $filecheck
   if [[ $? != '0' ]]; then
       rm $filecheck
       echo "File is on server"
       echo "$URL/$DIRECTORY/$FILENAME"
       if [[ -n $1 ]]; then
         if [[ -z $OVERWRITE ]]; then
           echo
           echo "Use -o to overwrite"
           exit
         else
           echo "overwriting ..."
           echo
         fi
       fi
   else
       rm $filecheck
       echo "File is not on server"
   fi
else
    echo "Error server not found"
    rm $filecheck
    exit 2
fi
}

servername ()
{
echo $SERVER
}

ARGS=`getopt -o "d:hlko" -l "directory:,help,list,key,overwrite" -n "$ME" -- "$@"`

eval set -- "$ARGS"
while true;
do
    case "$1" in
	-h|--help)
	    usage
	    exit 0;;
	-d|--directory)
	    DIRECTORY="$2"
	    shift 2;;
	-l|--list)
	    LIST=1
	    shift
	    ;;
	-o|--overwrite)
	    OVERWRITE=1
	    shift
	    ;;
	-k|--key)
            # check if there is a key available 
	    if [[ -e ~/.ssh/id_rsa ]]; then
		servername
		ssh-copy-id -i ~/.ssh/id_rsa "$SERVER"
	    else
		echo "Error no key found, please run 'ssh-keygen -t rsa' to generate one first."
		exit 4
	    fi
	    exit 0;;
	--)
	    shift
	    break;;
    esac
done

if [[ "$LIST" == "1" ]]; then
    servername
    ssh $SERVER "ls -la $DIRECTORY"
    exit
fi
  
FILENAME="$1"

# Using '20' as fixed numbers to ensure the 4 numbers are a date
if [[ "$FILENAME" =~ 20[0-9]{2} ]]; then
    echo "Filename ok!"
elif [[ -z "$FILENAME" ]]; then
    usage
    exit
else
    echo "Filename not valid! Did you include a date (Year-Month-Day)?"
    exit 1
fi

# Default upload directory is upload/ to ensure files are always atleast somewhat sorted
if [[ -z "$DIRECTORY" ]]; then
    DIRECTORY="upload/"
fi

file_on_server check_overwrite


# test if metadata file exists and add to files to upload
METAFILENAME="$FILENAME"
METAFILENAME=${METAFILENAME%.*}
METAFILENAME="$METAFILENAME.txt"

ALLFILE=$FILENAME
for i in $(ls); do
    if [[ "$i" == "$METAFILENAME" ]]; then
	ALLFILE="$ALLFILE $METAFILENAME"
    fi
done

# upload files with the same name
servername
rsync -vz --progress $ALLFILE "$SERVER":"$DIRECTORY"

#check if upload was successful

file_on_server

exit 0;

