#!/usr/bin/perl

use strict;

my $want_main = 0;
my $want_netbook = 0;
my $want_ivi = 0;
my $want_n900 = 0;
my $want_panda = 0;
my $want_u8500 = 0;


sub do_patch_source
{
  my $patchcounter = 1;
  open MYFILE, "<series" || die "Cannot open series\n";;
  while (<MYFILE>) {
    my $line = $_;
    chomp($line);

    if ($line =~/^#.*maintainer/) {
      $patchcounter = 100 * int($patchcounter / 100) + 100;
    }

    # we print comment lines
    if ($line =~/^#/ || length($line) < 4) {
      print "$line\n";
      next;
    }

    # ok now we have a patch
    print "Patch$patchcounter: $line\n";
    $patchcounter = $patchcounter + 1;
  }
  close MYFILE;
}

sub do_patch_apply
{
  my $patchcounter = 1;
  open MYFILE, "<series" || die "Cannot open series\n";;
  while (<MYFILE>) {
    my $line = $_;
    chomp($line);

    if ($line =~/^#.*maintainer/) {
      $patchcounter = 100 * int($patchcounter / 100) + 100;
    }

    # we print comment lines
    if ($line =~/^#/ || length($line) < 4) {
      print "$line\n";
      next;
    }

    # ok now we have a patch
    print "# $line\n";
    print "\%patch$patchcounter -p1\n";
    $patchcounter = $patchcounter + 1;
  }
  close MYFILE;
}


if ( -e "./MAIN") {
  $want_main = 1;
}
if ( -e "./IVI") {
  $want_ivi = 1;
}
if ( -e "./NETBOOK") {
  $want_netbook = 1;
}
if ( -e "./N900") {
  $want_n900 = 1;
}
if ( -e "./PANDA") {
  $want_panda = 1;
}
if ( -e "./U8500") {
  $want_u8500 = 1;
}

while (<>) {
  my $line = $_;
  chomp($line);

  if ($line =~ /\@\@/) {

    if ($want_netbook > 0 && $line =~ /^\@\@NETBOOK (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_ivi > 0 && $line =~ /^\@\@IVI (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_n900 > 0 && $line =~ /^\@\@N900 (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_panda > 0 && $line =~ /^\@\@PANDA (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_u8500 > 0 && $line =~ /^\@\@U8500 (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_main > 0 && $line =~ /^\@\@MAIN (.*)/) {
        print "$1\n";
        next;
    }

    if ($want_netbook > 0 && $want_main == 1 && $line =~ /\@\@\@NETBOOK (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_ivi > 0 && $want_main == 1 && $line =~ /\@\@\@IVI (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_n900 > 0 && $want_main == 1 && $line =~ /\@\@\@N900 (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_panda > 0 && $want_main == 1 && $line =~ /\@\@\@PANDA (.*)/) {
        print "$1\n";
        next;
    }
    if ($want_u8500 > 0 && $want_main == 1 && $line =~ /\@\@\@U8500 (.*)/) {
        print "$1\n";
        next;
    }

    # Patch directive
    if ($line =~ /^\@\@PATCHSOURCE$/) {
        do_patch_source();
        next;
    }
    # patch directive
    if ($line =~ /^\@\@PATCHAPPLY$/) {
        do_patch_apply();
        next;
    }
  } else {
    print "$line\n";
  }

}
