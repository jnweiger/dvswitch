#! /usr/bin/perl -w
# dvsource-webcam.pl is a more userfriendly wrapper for dvsource-command.
# It assists in constructing and ffmpeg commandline (audio and video) and
# then passes all relevant parameters to dvsource-command.
#
# It is designed to work with a simple v4l2 webcam, but may be helpful for other
# sources too.
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-03-04 -- V0.1 initial draft.
# 2012-03-06 -- V1.0 works.
# 2012-03-09 -- V1.1 run arecord with LANG=C, to get predictable output.
# 2012-03-21 -- V1.2 changed default, to last /dev/videoN, not /dev/video0
#############
# FIXME: this goes extremly async, quite soon.
# http://dvswitch.alioth.debian.org/wiki/inputs/ suggests this command line:
# ffmpeg -f video4linux2 -s vga -r 25 -i /dev/video0 -f s16le -ar 48000 -ac 2 -i /dev/zero -target pal-dv - | dvsource-file /dev/stdin
## FIXME: add -re, to read the input file in realtime, instead of prebuffering.

## Requires: alsa-utils, ffmpeg

use strict;
use Data::Dumper;
use File::stat;
use File::Glob;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '1.2';
my $a_chan = undef;
my $a_name = undef;
my $retry_connect = 0;
my $host = undef;
my $port = undef;
my $tally = undef;
my $verbose = undef;
my $help = undef;
my $dv_mode = 'pal-dv';

GetOptions(
	"retry|R" 	=> \$retry_connect,
	"verbose|v"	=> \$verbose,
	"port|p=s"	=> \$port,
	"host|h=s"	=> \$host,
	"tally|t"	=> \$tally,
	"adev=s"	=> \$a_name,
	"achan=s"	=> \$a_chan,
	"help|?"	=> \$help,
	"pal-dv"	=> sub { $dv_mode = 'pal-dv'; },
	"ntsc-dv"	=> sub { $dv_mode = 'ntsc-dv'; },
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvsource-webcam.pl V$version


$0 [OPTIONS] [VIDEO_DEV]

The default VIDEO_DEV is /dev/videoN, where N is the highest available number, 
aka the device that was connected last.
$0 converts video from any v4l2 device (e.g. a webcam) together with an audio 
source into a DV stream that is usable by DVswitch. This conversion requires
ffmpeg.

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

 --adev=ALSA_DEVICE
 	Specify which alsa audio capture device to use. Run 'arecord -l' for
	a listing. Default: search for same USB-id as VIDEO_DEV.
 --achan=N
 	Specify how many channels the alsa audio device captures. Default: 
	query USB subsystem, or '2'.

 --pal
 --ntsc
 	Specify video mode for the generated DV stream. Default: $dv_mode
}) if $help;
	
my $vdev = shift || first_existing_file('/dev/video%d', [reverse (0..20)]);
die "Missing video device $vdev. Check your webcam cable or driver?\n" unless -e $vdev;

my $a_dev = { id => 0 };
if (defined $a_name)
  {
    $a_dev = { id => $1 } if $a_name =~ m{(\d+)};
  }
else
  {
    ($a_name,$a_dev) = find_matching_audio_source($vdev);
  }

my $warn;
($a_chan, $warn) = find_n_channels($a_dev) unless defined $a_chan;
print "WARNING: find_n_channels(): $warn\n" if defined $warn;

die "no audio found for $vdev -- please retry using --adev=hw:N\n" unless $a_name;
die "unclear number audio channels in $a_name -- please retry using --achan=N\n" unless $a_chan;

my $ff_verbose = $verbose ? 'verbose' : 'quiet';

my $ffmpeg = "ffmpeg -v $ff_verbose -f alsa -ac $a_chan -i $a_name -f v4l2 -i $vdev -target $dv_mode pipe:1";

my $cmd = 'dvsource-command';
$cmd .= " --tally" if $tally;
$cmd .= " --verbose" if $verbose;
$cmd .= ' --retry' if $retry_connect;
$cmd .= " --port=$port" if defined $port;
$cmd .= " --host=$host" if defined $host;

system "$cmd '$ffmpeg'" and die "failed to run '$cmd'\n";
exit 0;


## FIXME: can we run with a dummy audio stream, or even without?
###
## how to compute LONG_ENOUGH, how to loop a short audio, 
# print "ffmpeg -f s8 -ar 8000 -t LONG_ENOUGH -i /dev/zero /tmp/silence.wav;\n";
# print "ffmpeg -f s8 /tmp/silence.wav -f v4l2 -i $vdev -target pal-dv pipe:1\n";

## slurps in 300 MB audio per minute, this is too much!
# print "ffmpeg -f s16le -ar 48000 -i /dev/zero -f v4l2 -i $vdev -target pal-dv pipe:1\n";

sub find_matching_audio_source
{
  my ($video_dev) = (@_);

  my $arecord = $ENV{DVSWITCH_ARECORD} || '/usr/bin/arecord';
  die "$arecord not found. Try env DVSWITCH_ARECORD=...\n" unless -x $arecord;
  ## FALLBACK: browse through /proc/asound/cards

  my @adev_list;
  # card 0, not Karte 0, please.
  open IN, "-|", "env LANG=C $arecord -l" or die "failed to run '$arecord -l': $!\n";
  while (defined( my $line = <IN>))
    {
      chomp $line;
      # card 0: Intel [HDA Intel], device 0: AD198x Analog [AD198x Analog]
      # card 1: U0x46d0x805 [USB Device 0x46d:0x805], device 0: USB Audio [USB Audio]
      if ($line =~ m{^\s*card\s+(\d+):\s+(.*)$})
	{
	  push @adev_list, { id => $1, arecord_line => $2 };
	}
    }
  close IN;

  warn "Failed to list audio devices: $arecord -l: returned no 'card \\d: ...' lines.\n" unless @adev_list;

  my $st = File::stat::stat($video_dev);
  die "cannot stat '$video_dev', need an existing video device name as parameter\n" unless defined $st and $st->rdev;

  my $dev = { rdev => $st->rdev, name => $video_dev };

  $dev->{dev}  = $dev->{rdev} >> 8;
  $dev->{node} = $dev->{rdev} - ($dev->{dev} << 8);

  # /sys/dev/char/81:0/name
  my $syspath = "/sys/dev/char/$dev->{dev}:$dev->{node}/name";

  if (open IN, "<", $syspath)
    { 
      chomp($dev->{fluffy_name} = <IN>);
      close IN;
    }
  else
    {
      warn "cannot open $syspath for " . Dumper $dev;
    }

  # /sys/bus/usb/devices/1-5/1-5:1.0/video4linux/video0/dev
  my @all = File::Glob::bsd_glob("/sys/bus/usb/devices/*/*/video4linux/video0/dev");
  for my $usb (@all)
    {
      next unless open IN, "<", $usb;
      my $line = <IN>;
      close IN;
      chomp $line;
      next if $line ne "$dev->{dev}:$dev->{node}";
      next unless $usb =~ m{^/sys/bus/usb/devices/([^/]+)/};
      my $usb_dev = $1;
      $dev->{sys_bus_usb} = $usb;
      $dev->{usb_dev} = $usb_dev;
      if (open IN, "<", "/sys/bus/usb/devices/$usb_dev/idVendor")
	{
	  chomp($dev->{idVendor} = <IN>);
	  close IN;
	}
      if (open IN, "<", "/sys/bus/usb/devices/$usb_dev/idProduct")
	{
	  chomp($dev->{idProduct} = <IN>);
	  close IN;
	}
    }

  delete $dev->{usb_dev};

  unless ($dev->{usb_dev})
    {
      ## hmm, /sys/bus/usb path lookup did not work, maybe the name 
      ## contains a usb idVendor idProduct pair?
      if ($dev->{fluffy_name} =~ m{\b(0x)?([0-9a-f]{4}):(0x)?([0-9a-f]{4})\b})
	{
	  $dev->{idVendor} = $2;
	  $dev->{idProduct} = $4;
	  $dev->{usb_dev} = "not in /sys/bus/usb, but matching '$dev->{fluffy_name}'";
	}
    }

  if ($dev->{usb_dev})
    {
      # $video_dev is an usb device. Try to match by Vendor:Product
      my $p1 = $dev->{idProduct};
      my $p2 = $p1; $p2 =~ s{^0*}{};	# strip leading zeros
      my $v1 = $dev->{idVendor};
      my $v2 = $v1; $v2 =~ s{^0*}{};	# strip leading zeros

      for my $adev (@adev_list)
	{
	  $adev->{score} = 0;
	  $adev->{score} +=    1 if $adev->{arecord_line} =~ m{\busb\b}i;

	  $adev->{score} +=   10 if $adev->{arecord_line} =~ m{\b$v1\b} and
				    $adev->{arecord_line} =~ m{\b$p1\b}i;
	  $adev->{score} +=   10 if $adev->{arecord_line} =~ m{\b$v2\b} and
				    $adev->{arecord_line} =~ m{\b$p2\b}i;
	  $adev->{score} +=   10 if $adev->{arecord_line} =~ m{\b0x$v1\b} and
				    $adev->{arecord_line} =~ m{\b0x$p1\b}i;
	  $adev->{score} +=   10 if $adev->{arecord_line} =~ m{\b0x$v2\b} and
				    $adev->{arecord_line} =~ m{\b0x$p2\b}i;

	  $adev->{score} +=  100 if $adev->{arecord_line} =~ m{\b$v1:$p1\b}i;
	  $adev->{score} +=  100 if $adev->{arecord_line} =~ m{\b$v2:$p2\b}i;
	  $adev->{score} +=  100 if $adev->{arecord_line} =~ m{\b0x$v1:0x$p1\b}i;
	  $adev->{score} +=  100 if $adev->{arecord_line} =~ m{\b0x$v2:0x$p2\b}i;
	}
    }
  else
    {
      warn "Not a USB video device, audio matching not impl." . Dumper $dev;
      return undef;
    }

  # die Dumper \@adev_list, $dev, \@all;

  my @adev_scored = sort { $b->{score} <=> $a->{score} } @adev_list;
  if (scalar @adev_scored and $adev_scored[0]{score})
    {
      $adev_scored[0]{v_dev} = $dev;
      return ("hw:$adev_scored[0]{id}", $adev_scored[0]);
    }
}


sub find_n_channels
{
  my ($dev) = @_;
  # /proc/asound/card1/usbmixer
  # if exists, contains this:
  # USB Mixer: usb_id=0x046d0805, ctrlif=2, ctlerr=0
  # Card: USB Device 0x46d:0x805 at usb-0000:00:1d.7-5, high speed
  #   Unit: 5
  #     Control: name="Mic Capture Volume", index=0
  #     Info: id=5, control=2, cmask=0x0, channels=1, type="S16"
  #     Volume: min=0, max=1, dBmin=0, dBmax=0
  #   Unit: 5
  #     Control: name="Mic Capture Switch", index=0
  #     Info: id=5, control=1, cmask=0x0, channels=1, type="INV_BOOLEAN"
  #     Volume: min=0, max=1, dBmin=0, dBmax=0
  ###
  # my /proc/asound/card0/ contains no mixer, and no other
  # reliable way to read a channel count. 
  # So we default to 2 channels, unless we see the above.
  ###
  #

  return (2, "default=2. audio device undefined") unless defined $dev;

  my $usbmixer = "/proc/asound/card$dev->{id}/usbmixer";
  unless (open IN, "<", $usbmixer)
    {
      # well, we have no clue, actually, lets default to stereo
      return (2, "default=2. no $usbmixer, no clue");
    }
  while (defined (my $line = <IN>))
    {
      chomp $line;
      if ($line =~ m{^\s+Info:.*\bchannels=(\d+)\b})
        {
	  close IN;
	  return $1;
	}
    }
  return (2, "default=2. $usbmixer had no 'Info:' with 'channels='");
  close IN;
}

sub first_existing_file
{
  my ($fmt_str, $range) = @_;
  for my $n (@$range)
    {
      my $name = sprintf $fmt_str, $n;
      return $name if -e $name;
    }
  return $fmt_str;
}

