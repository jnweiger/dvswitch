#! /usr/bin/perl -w
# dvsink-bambuser.pl is a userfriendly wrapper for dvsink-command.
# It creates a live stream suitable for bambuser.com
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-09-30 -- V0.1 initial draft.
# 2012-10-21 -- V0.2 broken config file support added
# 2012-10-25 -- V0.3 fixed config file support
#
## Requires: ffmpeg

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '0.3';
my $a_chan = undef;
my $a_name = undef;
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $verbose = 1;
my $help = undef;
my $ffmpeg_fmt  = 'ffmpeg -v %s -y -i - -re -af aresample=22050 %s -vcodec flv -g 150 -cmp 2 -subcmp 2 -mbd 2 -f flv "%s"';
my $ffmpeg_opt_def  = '-s 480x360 -b:v 300k';
my $ffmpeg_opt = undef;
my $ffmpeg_rtmp_def = 'rtmp://68NNNN.fme.bambuser.com/b-fme/ad6XXXXXXXXXXXXXXXXXXX';
my $ffmpeg_rtmp = undef;
my $channel_url     = 'http://bambuser.com/channel/opensusetv';
my $channel_admin   = 'Juergen Weigert <jw@suse.de> #opensuse-video@irc.freenode.org';
my $config_file = undef;

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"config|c=s"	=> sub { $ENV{ICECAST_CONF_FILE} = $_[1]; },
	"320"		=> sub { $ffmpeg_opt = '-s 320x240 -b:v 150k'; },
	"480"		=> sub { $ffmpeg_opt = '-s 480x360 -b:v 300k'; },
	"640"   	=> sub { $ffmpeg_opt = '-s 640x480 -b:v 400k'; },
	"720"   	=> sub { $ffmpeg_opt = '-s 720x768 -b:v 500k'; },
	"opt|o=s"       => \$ffmpeg_opt,
	"help|?"	=> \$help,
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsink-bambuser.pl V$version


$0 [OPTIONS] RTMP_STREAM_URL
$0 [OPTIONS] [-c bambuser.conf]


The RTMP_STREAM_URL can be obtained from bambuser.com,
-> dashboard -> Standalone Desktop app -> Flash Media Live Encoder 
-> Download your Authentication Profile
http://api.bambuser.com/user/fmle_profile.xml 
search for <output><stream>RTMP_STREAM_URL</stream><output>
where RTMP_STREAM_URL should look like this: $ffmpeg_rtmp

Valid options are:
 -v	Print more information at startup.

 -R, --retry	
 	Keep retrying to connect, in case DVswitch is not yet listening.

 -h, --host=HOST
 -p, --port=PORT
 	Specify the network address on which DVswitch is listening.  The
	host address may be specified by name or as an IPv4 or IPv6 literal.

 --320 --480 --640 --720
 	Select a specific output size and bandwidth. 
	Default: '$ffmpeg_opt'

 --opt='FFMPEG_OPTIONS'
 	Default: --opt='$ffmpeg_opt'

 -c, --config=BAMBUSER_CONF_FILE
        Specify a config file that overwrites the environment variable
	BAMBUSER_CONF_FILE, if any. See below.

The RTMP_STREAM_URL command line parameters are optional, if a config file 
'.bambuser.cfg', '~/.bambuser.cfg', '/etc/bambuser.cfg' exists 
or was specified with -c, containing the following lines:
 
 bambuser_rtmp_url=rtmp://68NNNN.fme.bambuser.com/b-fme/ad6XXXXXXXXXXXXXXXXXXX
 ffmpeg_opt=...

The channel at $channel_url 
is run by $channel_admin

}) if $help;
	
$ffmpeg_opt = getconfig('ffmpeg_opt') || $ffmpeg_opt_def;
my $url = shift || getconfig('bambuser_rtmp_url') || die "please provide a URL like $ffmpeg_rtmp\n";
$ffmpeg_rtmp = $url;

my $ff_verbose = ($verbose > 1) ? 'verbose' : 'quiet';

my $ffmpeg = sprintf $ffmpeg_fmt, $ff_verbose, $ffmpeg_opt, $ffmpeg_rtmp;

my $cmd = 'dvsink-command';
$cmd .= ' --retry' if $retry_connect;
$cmd .= " --port=$port" if defined $port;
$cmd .= " --host=$host" if defined $host;

print "+ $cmd -- $ffmpeg\n" if $verbose;
system "$cmd -- $ffmpeg" and die "failed to run '$cmd'\n";
exit 0;

sub getconfig
{
  my ($token) = @_;

  if (defined $ENV{ICECAST_CONF_FILE})
    {
      open IN, "$ENV{ICECAST_CONF_FILE}" or die "cannot read ENV{ICECAST_CONF_FILE}=$ENV{ICECAST_CONF_FILE}: $!\n";
    }
  else
    {
      open IN, ".bambuser.cfg" 
      or open IN, "$ENV{HOME}/.bambuser.cfg"
      or open IN, "/etc/bambuser.cfg" 
      or return undef;
    }

  while (defined (my $line = <IN>))
    {
      chomp $line;
      if ($line =~ m{^\s*$token=})
        {
	  print "$line\n";
	}
      if ($line =~ m{^\s*$token=(.*?)\s*$})
        {
	  close IN;
	  return $1;
	}
    }
  return undef;
}
