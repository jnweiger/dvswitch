#! /usr/bin/perl -w
# dvsink-fullscreen.pl is a userfriendly wrapper for dvsink-command.
# It creates a fullscreen monitor app with mplayer
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-10-12 -- V0.1 initial draft.
#
## Requires: mplayer

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '0.1';
my $a_chan = undef;
my $a_name = undef;
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $verbose = 1;
my $fullscreen = 1;
my $help = undef;
my $mplayer_cmd  = 'mplayer %s -';
my $mplayer_opts = '-fs';

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"normal-size|n"	=> sub { $mplayer_opts = ''; },
	"opt|o=s"       => \$mplayer_opts,
	"help|?"	=> \$help,
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsink-fullscreen.pl V$version


$0 [OPTIONS]

Valid options are:
 -v	Print more information at startup.

 -R, --retry	
 	Keep retrying to connect, in case DVswitch is not yet listening.

 -h, --host=HOST
 -p, --port=PORT
 	Specify the network address on which DVswitch is listening.  The
	host address may be specified by name or as an IPv4 or IPv6 literal.

 --normal-size -n
 	Disable the default fullscreen option.
	Default: '$mplayer_opts'

 --opt='MPLAYER_OPTIONS'
 	Default: --opt='$mplayer_opts'

}) if $help;
	
$mplayer_opts .= ' -msglevel all=6' if $verbose > 1;

my $mplayer = sprintf $mplayer_cmd, $mplayer_opts;

my $cmd = 'dvsink-command';
$cmd .= ' --retry' if $retry_connect;
$cmd .= " --port=$port" if defined $port;
$cmd .= " --host=$host" if defined $host;

print "+ $cmd -- $mplayer\n" if $verbose;
system "$cmd -- $mplayer" and die "failed to run '$cmd'\n";
exit 0;
