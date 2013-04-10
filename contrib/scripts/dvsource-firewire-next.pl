#! /usr/bin/perl -w
# dvsource-firewire_jw.pl -- a rewrite of the firewire grabber.
# The original dvsource-firewire suffers from the following issues:
# if we have both /dev/fw0 and /dev/fw1, it takes the first one, even if the 
#  other one is only functional.
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-10-14 -- V0.1 initial draft.
# 2012-10-16 -- V0.2 skip empty units
#
#############
## Requires: dvgrab
#
# Improvement: 
# check all cat /sys/bus/firewire/devices/*/guid
# and ignore those with 0x0000000000000000
# and ignore those with empty /sys/bus/firewire/devices/*/units

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '0.2';
my $a_chan = undef;
my $a_name = undef;
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $tally = undef;
my $verbose = 0;
my $help = undef;
my $dvgrab = 'dvgrab';
my $dvgrab_opts = '-noavc';
my $ignore_missing_units = 0;

sub get_ps();

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v+"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"tally|t"	=> \$tally,
	"ignore-units|u" => \$ignore_missing_units,
	"help|?"	=> \$help,
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsource-firewire_jw.pl V$version


$0 [OPTIONS]

Start a dvsource on the next available firewire device. Dead devices (without guid) are skipped,
devices that are in use (guid appears in /proc/*/cmdline) are also skipped.
Returns nonzero if no usable devices were found.

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

}) if $help;

opendir DIR, "/sys/bus/firewire/devices/" or die "no /sys/bus/firewire/devices/: $!";
my @fw = grep { !/^\./ } readdir DIR;
closedir DIR;
my %fw = map { $_ => { guid => undef } } @fw;
my @good;
for my $fw (sort @fw)
  {
    if (open IN, '<', "/sys/bus/firewire/devices/$fw/guid")
      {
        my $guid = <IN> || '';
        chomp $guid;
        $fw{$fw}{guid} = $guid;
        close IN;

        if (open IN, '<', "/sys/bus/firewire/devices/$fw/units")
          {
	    # expect 0x00a02d:0x010001
            my $units = <IN> || '';
            chomp $units;
            $fw{$fw}{units} = $units;
            close IN;
	    $units = '0x0:0x0' if $ignore_missing_units;

            if (($guid =~ m{^0x[\da-f]*[1-9a-f][\da-f]*$}) and
	        ($units =~ m{^0x[\da-f]+:0x[\da-f]+$}))
	      {
		push @good, [$fw, $guid];
		print STDERR "Usable devices: /dev/$fw guid=$guid\n" if $verbose > 1;
	      }
	  }
      }
  }

my $all_commands = join("\n", get_ps());
while (@good && $all_commands =~ m{\b$good[0][1]\b})
  {
    print STDERR "already in use: /dev/$good[0][0] guid=$good[0][1]\n" if $verbose > 1;
    $fw{$good[0][0]}{in_use} = 1;
    shift @good;
  }
die "No usable devices: " . Dumper \%fw unless scalar @good;
$dvgrab_opts .= " -g '$good[0][1]'";
	
my $cmd = 'dvsource-command';
$cmd .= " --tally" if $tally;
$cmd .= " --verbose" if $verbose;
$cmd .= ' --retry' if $retry_connect;
$cmd .= " --port=$port" if defined $port;
$cmd .= " --host=$host" if defined $host;

print STDERR "+ $cmd '$dvgrab $dvgrab_opts -'\n" if $verbose;
system "$cmd '$dvgrab $dvgrab_opts -'" and die "failed to run '$cmd \"$dvgrab -noavc $dvgrab_opts -\"'\n";
exit 0;

#####################################################
sub get_ps()
{
  my @r = ();
  opendir DIR, "/proc/" or return @r;
  my @pids = grep { /^\d+$/ } readdir DIR;
  closedir DIR;

  for my $pid (@pids)
    {
      if (open IN, "<", "/proc/$pid/cmdline")
        {
          my $cmdline = <IN> || '';
          chomp $cmdline;
          push @r, $cmdline;
        }
    }
  return @r;
}
