#! /usr/bin/perl -w
# dvsink-icecast.pl is a userfriendly wrapper for dvsink-command.
# It creates a live stream suitable for icecast servers
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-10-10 -- V0.1 initial draft.
#
## Requires: ffmpeg2theora, oggfwd

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '0.2';
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $verbose = 1;
my $help = undef;
my $ffmpeg_fmt  = 'ffmpeg2theora - -f dv %s -F 25:5 -k 8 --speedlevel 0 -v 6 -a 0 -c 1 -H 9600 -o - | oggfwd -g "%s" -n "%s" "%s" "%s" "%s" "%s"';
my $ffmpeg_opt_def  = '-x 480 -y 360 -V 300';
my $ffmpeg_opt = undef;
my $ogg_group_def = 'opensusetv';
my $ogg_group = undef;
my $ogg_name = undef;
my $config_file = undef;

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"group|g=s"	=> \$ogg_group,
	"name|n=s"	=> \$ogg_name,
	"config|c=s"	=> sub { $ENV{ICECAST_CONF_FILE} = $_[1]; },
	"320"		=> sub { $ffmpeg_opt = '-x 320 -y 240 -V 150'; },
	"480"		=> sub { $ffmpeg_opt = '-x 480 -y 360 -V 300'; },
	"640"   	=> sub { $ffmpeg_opt = '-x 640 -y 480 -V 400'; },
	"720"   	=> sub { $ffmpeg_opt = '-x 720 -y 768 -V 500'; },
	"opt|o=s"       => \$ffmpeg_opt,
	"help|?"	=> \$help,
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsink-icecast.pl V$version


$0 [OPTIONS] ICECAST_SERVER PORT PASSPHRASE /MOUNTPOINT.ogv
$0 [OPTIONS] [-c icecast.conf]


Valid options are:
 -v	Print more information at startup.

 -R, --retry	
 	Keep retrying to connect, in case DVswitch is not yet listening.

 -h, --host=HOST
 -p, --port=PORT
 	Specify the network address on which DVswitch is listening.  The
	host address may be specified by name or as an IPv4 or IPv6 literal.

 -g, --group=CHANNEL_NAME
 	Specify channel/group name for the ogv stream. Default '$ogg_group_def'.
 -n, --name=STREAM_NAME
        Specify logical names for the ogv stream. Default from mountpoint.

 --320 --480 --640 --720
 	Select a specific output size and bandwidth. 
	Default: '$ffmpeg_opt_def'

 --opt='FFMPEG2THEORA_OPTIONS'
 	Default: --opt='$ffmpeg_opt_def'

 -c, --config=ICECAST_CONF_FILE
        Specify a config file that overwrites the environment variable
	ICECAST_CONF_FILE, if any. See below.

The 4 command line parameters are optional, if a config file 
'.icecast.cfg', '~/.icecast.cfg', '/etc/icecast.cfg' exists 
or was specified with -c, containing the following lines:
 icecast_server=ICECAST_SERVER
 icecast_port=PORT
 icecast_pass=PASSPHRASE
 icecast_path=/MOUNTPOINT.ogv

}) if $help;
	
my $icecast_server=shift || getconfig('icecast_server') || die "please provide: *>SERVER_NAME<* PORT PASSPHRASE /MOUNTPOINT.OGV\n";
my $icecast_port=shift   || getconfig('icecast_port')   || die "please provide: SERVER_NAME *>PORT<* PASSPHRASE /MOUNTPOINT.OGV\n";
my $icecast_pass=shift   || getconfig('icecast_pass')   || die "please provide: SERVER_NAME PORT *>PASSPHRASE<* /MOUNTPOINT.OGV\n";
my $icecast_path=shift   || getconfig('icecast_path')   || die "please provide: SERVER_NAME PORT PASSPHRASE *>/MOUNTPOINT.OGV<*\n";
$ffmpeg_opt = getconfig('ffmpeg_opt') || $ffmpeg_opt_def unless defined $ffmpeg_opt;
$ogg_group =  getconfig('ogg_group')  || $ogg_group_def  unless defined $ogg_group;
$ogg_name  =  getconfig('ogg_name')                      unless defined $ogg_name;

$icecast_path = '/'.$icecast_path if $icecast_path !~ m{^/};
$icecast_path .= ".ogv" if $icecast_path !~ m{\.w+$};

unless (defined $ogg_name)
  {
    $ogg_name = $icecast_path;
    $ogg_name =~ s@^.*/@@;
    $ogg_name =~ s@\.\w+$@@;
  }

my $ff_verbose = ($verbose > 2) ? 'verbose' : 'quiet';

my $ffmpeg = sprintf $ffmpeg_fmt, $ffmpeg_opt, $ogg_group, $ogg_name, 
	$icecast_server, $icecast_port, $icecast_pass, $icecast_path;

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
      open IN, ".icecast.cfg" 
      or open IN, "$ENV{HOME}/.icecast.cfg"
      or open IN, "/etc/icecast.cfg" 
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
