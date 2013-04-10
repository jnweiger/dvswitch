#! /usr/bin/perl -w
# dvtrim.pl is a userfriendly wrapper for ffmpeg -ss -t
#
# (C) 2012, jw@suse.de, distribute under GPL-2.0+ or ask.
# 2012-03-21 -- V0.1 initial draft.
# 2012-03-22 -- V0.2 enabled experimental --fastcopy option.
#
## Requires: ffmpeg

use strict;
use Data::Dumper;
use Getopt::Long qw(:config no_ignore_case);
use Pod::Usage;

my $version = '1.1';

my $verbose = 1;
my $help = undef;
my $outfile = undef;
my $start_time = undef;
my $end_time = undef;
my $duration_time = undef;
my $fastcopy = 0;

GetOptions(
	"verbose|v+"	=> \$verbose,
	"quiet|q"	=> sub { $verbose = 0; },
	"fastcopy|f"	=> \$fastcopy,
	"start|ss|s=s"	=> \$start_time,
	"end|stop|e=s"	=> \$end_time,
	"duration|t=s"	=> \$duration_time,
	"help|?"	=> \$help,
	"output|o=s"	=> \$outfile
) or $help++;

pod2usage(-verbose => 1, -msg => qq{
dvtrim.pl V$version

$0 [OPTIONS] INFILE.dv [-o OUTFILE.dv]

Valid options are:
 -v --verbose    Print more information at startup.
 -q --quiet      Print less information.
 -f --fastcopy	 Experimental copy codec. It may be faster or may fail to start.

 --start [hh:mm:]ss[.xxx]
		 The timecode can be specified in hours, minutes, seconds (with
		 fractions) or all seconds. Default: --start 0
 --duration [hh:mm:]ss[.xxx]
 		 'duration' and 'end' are exclusive.
 		 Default: until the end of the video.
 --end [-][hh:mm:]ss[.xxx]
 		 'duration' and 'end' are exclusive.
		 'end' can be specified as a negative value, which trims
		 relative to the end of the video. 
		 Default: until the end of the video: --end -0
}) if $help;

my $infile = shift || die "No input file name given.\n";
die "Missing input file $infile\n" unless -e $infile;

die "--end and --duration are mutually exclusive.\n" 
  if defined $duration_time and defined $end_time;

my $start    = linear_time($start_time, '--start')       if defined $start_time;
               linear_time($duration_time, '--duration') if defined $duration_time;
my $duration = linear_time($end_time, '--end') - $start  if defined $end_time;
$duration_time = $duration if defined $duration;
die "end must be greater than start.\n" if defined $duration and $duration <= 0;

unless (defined $outfile)
  {
    $outfile = $infile;
    $outfile =~ s{.*/}{};	# no path, save it in cwd.
    my $suf = $1 if $outfile =~ s{(\.\w+)$}{};
    $outfile .= "_dvtrim";
    $outfile .= $suf if defined $suf;
  }

my $ff_verbose = ($verbose > 1) ? 'verbose' : 'quiet';

my $ffmpeg = "ffmpeg -v $ff_verbose -i '$infile' -sameq";
if ($fastcopy)
  {
    # ffmpeg fails to initialize the copy codecs. We use -sameq for now. It is bit-identical.
    $ffmpeg = "ffmpeg -v $ff_verbose -i '$infile' -vcodec copy -acodec copy";
  }

$ffmpeg .= " -ss $start_time" if defined $start_time;
$ffmpeg .= " -t $duration_time" if defined $duration_time;
## we go for accuracy, not for speed. Thus -- according to the man page --
## -ss -t should come after -i.
$ffmpeg .= " '$outfile'";

warn "$ffmpeg\n" if $verbose;
system $ffmpeg and die "failed to run ($ffmpeg)\n";
exit 0;
##################################################


sub linear_time
{
  my ($hhmmss_ttt, $msg) = @_;
  return 0 unless defined $hhmmss_ttt;
  return $hhmmss_ttt+.0 if $hhmmss_ttt =~ m{^\d+(\.\d*)?$};
  if ($hhmmss_ttt =~ m{^(\d\d+):(\d\d):(\s\s(\.\d*))?$})
    {
      my ($hh, $mm, $ss) = ($1, $2, $3);
      return ($hh * 60 + $mm) * 60 + $ss;
    }
  die "$msg: supported formats: HH:MM:SS.TTT, HH:MM:SS, SSS.TTT, or SSS; got '$hhmmss_ttt'\n";
}
