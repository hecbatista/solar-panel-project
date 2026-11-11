#!/usr/bin/perl
# Set the DS3231 to this Mac's local *standard* time (no daylight saving).
# Usage: perl tools/set_rtc_time.pl [/dev/cu.usbmodem1301]
# The Uno must be running a sketch that accepts "T<unix seconds>\n" (rtc_test does).
use strict;
use warnings;
use Fcntl;
use POSIX qw(:termios_h mktime);
use Time::HiRes qw(time sleep);
use Time::Local qw(timegm);

my $port = shift // '/dev/cu.usbmodem1301';

# Standard-time offset from UTC in seconds: the smaller of the January and
# July offsets (in the US, January is standard time).
sub utc_offset {
    my ($t) = @_;
    my @lt = localtime($t);
    return timegm(@lt[0..5]) - $t;
}
my $year  = (localtime)[5];
my $std_offset = (sort { $a <=> $b }
    utc_offset(mktime(0,0,12,1,0,$year)), utc_offset(mktime(0,0,12,1,6,$year)))[0];

sysopen(my $fh, $port, O_RDWR | O_NOCTTY) or die "Cannot open $port: $!\n";
my $tio = POSIX::Termios->new;
$tio->getattr(fileno $fh);
$tio->setispeed(B9600); $tio->setospeed(B9600);
$tio->setlflag(0); $tio->setiflag(0); $tio->setoflag(0);
$tio->setcflag(CS8 | CREAD | CLOCAL | HUPCL);
$tio->setattr(fileno $fh, TCSANOW);
POSIX::tcflush(fileno $fh, TCIFLUSH);  # drop output left over from before the reset
$| = 1;

# Read serial for up to $secs seconds, stop early once $want matches.
my $pending = '';
sub read_until {
    my ($want, $secs) = @_;
    my $end = time + $secs;
    while (time < $end) {
        my $rin = '';
        vec($rin, fileno $fh, 1) = 1;
        next unless select($rin, undef, undef, 0.1);
        my $n = sysread($fh, my $chunk, 256);
        next unless $n;
        $pending .= $chunk;
        while ($pending =~ s/^(.*?)\r?\n//) {
            my $line = $1;
            print "  board: $line\n";
            return 1 if defined $want && $line =~ $want;
        }
    }
    return 0;
}

# Opening the port resets the Uno; wait for the sketch to start.
print "Waiting for the board to start...\n";
read_until(qr/RTC found/, 6) or die "Board never printed 'RTC found'. Is rtc_test uploaded?\n";

# Send right after a second boundary so the clock lands on the exact second.
my $now = time;
sleep(1 - ($now - int $now));
my $local_std = int(time) + $std_offset;
syswrite($fh, "T$local_std\n");
printf "Sent local standard time (UTC%+d).\n", $std_offset / 3600;

read_until(qr/Time set/, 3) or die "Board didn't confirm the new time.\n";
read_until(undef, 3);
close $fh;
