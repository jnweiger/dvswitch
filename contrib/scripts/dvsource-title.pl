#! /usr/bin/perl -w
# dvsource-title.pl is a wrapper for dvsource-file.
# It allows to use jpeg or png still images as title slide, by forwarding
# these stills through ffmpeg to create the needed DV format for
# dvsource-file.
#
# It also supports overwriting the image files while running. The new image
# will be reconverted to DV and the stream will switch seamlessly.
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-10-10 -- V0.1 initial draft.
# 2012-10-16 -- V0.2 added support for directories
#############

## Requires: perl, ffmpeg
## dvsource-file with title SIGHUP patch.

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;
use POSIX ":sys_wait_h";

my $version = '0.2';
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $tally = undef;
my $verbose = undef;
my $help = undef;
my $dv_mode = 'pal-dv';
my $audio_bits = 16;
my $loop_sec = '0.5';	# 0.2 is 200msec
my $poll_sec = '0.3';	# 0.2 is 200msec

sub ffmpeg_newest_img;

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"tally|t"	=> \$tally,
	"12"		=> sub { $audio_bits = 12; },
	"16"		=> sub { $audio_bits = 16; },
	"help|?"	=> \$help,
	"pal-dv"	=> sub { $dv_mode = 'pal-dv'; },
	"ntsc-dv"	=> sub { $dv_mode = 'ntsc-dv'; },
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsource-title.pl V$version


$0 [OPTIONS] IMAGE_FILE
$0 [OPTIONS] IMAGE_DIRECTORY/

IMAGE_FILES (jpeg, png, or anything that ffmpeg can parse) are converted to
DV streams.  If you replace the image file, while dvsource-title is running,
the stream will switch to the new image seamlessly.

If you specify an IMAGE_DIRECTORY instead, the newest jpeg or png file from
that directory will be used. The directory will be monitored for timestamp
changes, then again the newest file will be selected. This is great for
streaming a series of screendumps.

Valid options are:
 -v	Print more information at startup.

 -R, --retry	
 	Keep retrying to connect, in case DVswitch is not yet listening.

 -h, --host=HOST
 -p, --port=PORT
 	Specify the network address on which DVswitch is listening.  The
	host address may be specified by name or as an IPv4 or IPv6 literal.

 -t, --tally
 	Print notices of activation and deactivation in the  mixer,  for
	use with a tally light.  These will take the form "TALLY: on" or
	"TALLY: off".

 --12
 --16
 	Specify the audio format of the generated DV stream.
	Either 12bit with 44.1khz, or 16bit with 48khz.
	FIXME: ffmpeg always runs with 16bit/48khz. No switch to 12bit could 
	be found.

 --pal
 --ntsc
 	Specify video mode for the generated DV stream. Default: $dv_mode
}) if $help;
	
my $img = shift or die "Missing image name.\n";
die "Image $img not found\n" unless -e $img;
my $stat = stat $img;
my $last_touch = $stat->mtime;
my $last_size = $stat->size;

my $ff_ver = $verbose ? 'info' : 'error';
my $ffmpeg_fmt = "ffmpeg -v $ff_ver -f s16le -ar 8000 -i /dev/zero -loop 1 -t $loop_sec -i '%s' -target pal-dv '$img.dv'";

my @cmd = ( $ENV{DVSOURCE_FILE} || 'dvsource-file' );

my $dvsource_file_help = `$cmd[0] --help 2>&1`;
unless ($dvsource_file_help =~ m{kill\s.*hup}i)
  {
    warn "$cmd[0]:\n  Support for title_hup.diff not found.\n  Updating the image file may not work as expected.\n";
  }

push @cmd, "--tally"      if $tally;
# push @cmd, "--verbose"    if $verbose; # not supported with dvsource-file
push @cmd, "--retry"      if $retry_connect;
push @cmd, "--port=$port" if defined $port;
push @cmd, "--host=$host" if defined $host;
push @cmd, "--loop", "$img.dv";


unlink "$img.dv";
my ($ffmpeg,$img2) = ffmpeg_newest_img($ffmpeg_fmt, $img);
warn "\n+ $ffmpeg\n";
system "$ffmpeg";

my $dvsource_pid = fork();
if (!$dvsource_pid)
  {
    # child process
    exec @cmd or die "failed to exec @cmd: $!\n";
    die "?continued after exec?";
  }

print "dvsource pid=$dvsource_pid\n";

for(;;)
  {
    my $stat = stat $img;
    if (!$stat)
      {
        warn "missing '$img'\n";
      }
    else
      {
	if ($stat->mtime != $last_touch)
	  {
	    if ($stat->size <= 0)
	      {
		warn "empty '$img'\n";
	      }
	    else
	      {
		if ($last_size != $stat->size)
		  {
		    $last_size = $stat->size;
		    warn "$img: changeing ...(now $last_size bytes)\n";
		  }
		else
		  {
		    # its timestamp changed, but its size stabilized.
		    printf STDERR "new $img: size=%d\n", $stat->size;
		    unlink "$img.dv";
		    my ($ffmpeg,$img2) = ffmpeg_newest_img($ffmpeg_fmt, $img);
		    warn "\n+ $ffmpeg\n";
		    system "$ffmpeg";
		    if (-s "$img.dv" < 10000)
		      {
			warn "$ffmpeg: short output file '$img.dv' (min. 10000 bytes).\n";
			warn "Conversion failed. Retrying in 3 sec. Please replace $img ...\n";
			sleep 3;
		      }
		    else
		      {
			# we successfully created a new dv file.
			warn "new $img2:\n" if $verbose;
			warn "+ kill -hup $dvsource_pid\n";
			die "$dvsource_pid: @cmd is gone" unless kill "HUP", $dvsource_pid;
			$last_touch = $stat->mtime;
		      }
		  }
	      }
	  }
      }
    select(undef, undef, undef, $poll_sec);
    die "@cmd pid=0\n" unless $dvsource_pid;
    waitpid($dvsource_pid, WNOHANG);
    die "pid=$dvsource_pid: @cmd is gone.\n" unless kill 0 => $dvsource_pid;
    print STDERR "." if $verbose;
  }

exit 0;
#############################################################

## implements directory lookup for the newest file.
## this might get slow, if you have thousands of files in the directory.
sub ffmpeg_newest_img
{
  my ($ffmpeg_fmt, $img) = @_;
  if (-f $img)
    {
       die "filename with ' in it are not supported" if $img =~ m{'};
       return (sprintf($ffmpeg_fmt, $img), $img);
    }
  die "ffmpeg_newest_img: img must be file or directory\n" unless -d $img;

  opendir DIR, $img;
  my @files = grep { /\.(jpg|jpeg|png)$/i } readdir DIR;
  closedir DIR;

  my $newest_age = 99999999;
  my $newest_file = undef;
  my @nsupp;
  for my $file (@files)
    {
      my $age = -M "$img/$file";
      if ($age < $newest_age)
        {
	  if ("$img/$file" =~ m{'})
	    {
	      push @nsupp, "$img/$file";
	    }
	  else
	    {
	      $newest_age = $age;
	      $newest_file = "$img/$file";
	    }
	}
    }

  unless (defined $newest_file)
    {
      $newest_file = '/dev/null';
      warn "filenames with ' in them are not supported: '$nsupp[-1]'\n" 
        if scalar @nsupp;
    }

  # wait until the file stabilizes
  my $last_size = -s $newest_file;
  for(;;)
    {
      select(undef, undef, undef, $poll_sec);
      my $size = -s $newest_file;
      last if $size == $last_size;
      $last_size = $size;
      warn "$newest_file: changeing ... ($size bytes)\n";
    }

  return (sprintf($ffmpeg_fmt, $newest_file), $newest_file);
}
