#!/usr/bin/env perl
# Copyright 2022 Vlad Mesco
# 
# Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

use strict;
use warnings;
use Getopt::Long;

my %defines=();

my $prefix = "/usr/local";
my $showHelp = 0;
my $etc_conf = "/etc/jakobmenu.conf";
my $home_conf = "~/.config/jakobmenu/conf";

GetOptions("prefix=s" => \$prefix,
           "help" => \$showHelp,
           "etc_conf=s" => \$etc_conf,
           "home_conf=s" => \$home_conf)
           or die("Error parsing command line");

if($showHelp) {
    print <<EOT;
$0 [-prefix=/usr/local]
    -prefix=/usr/local                     set prefix for install
    -help                                  print this message
    -etc_conf=/etc/jakobmenu.conf          default /etc conf path
    -user_conf=\\~/.config/jakobmenu/conf   default user config path
                                           a quoted ~ will be expanded
                                           to the user's home directory
                                           at runtime.

This script generates Makefile.vars and config.h to generate sensible
defaults based on what is detected in the environment, or your whims.

This script inspects the following environment variables and saves them
to Makefile.vars if defined:
    CC              C compiler
    CFLAGS          additional flags to the \${CC} invocation
    LDFLAGS         linker flags passed to \${CC}

If CC is not defined, it will try, in order, 'cc', 'gcc' and 'clang'.
EOT
    exit(1);
}

sub compile {
    my ($what, $ccode, $cc, $lambda) = @_;

    print "Checking $what...\n";
    open A, ">test.c";
    print A $ccode;

    my $compiles = system("$cc test.c") ? 0 : 1;
    my $exitCode = -1;
    if($compiles) {
        $exitCode = system("./a.out") >> 8;
    }

    close A;
    unlink("test.c");
    unlink("a.out");

    return &$lambda($compiles, $exitCode);
}

####################################################################
# checks
####################################################################

sub tryCompiler {
    my ($cc) = @_;

    my $ccode = <<EOT;
#include <stdio.h>
int main(int argc, char* argv[]) {
    printf("Hello, world!\\n");
    return 0;
}
EOT

    return compile("$cc", $ccode, $cc, sub {
        my ($compiles, $status) = @_;
        return $compiles && $status == 0;
    })
}

my $compiler = undef;

print("Trying to find a C compiler...\n");
foreach my $cc ($ENV{CC}, 'cc', 'gcc', 'clang') {
    next if !defined($cc);
    next if !tryCompiler($cc);
    $compiler = $cc;
    last;
}

die("Can't rely on compiler") if not $compiler;

my $pledgeCode = <<EOT;
#include <unistd.h>
int main(int argc, char* argv[]) {
    pledge(NULL, NULL);
    return 0;
}
EOT
compile("for pledge", $pledgeCode, $compiler, sub {
    my ($compiles, $status) = @_;
    $defines{HAVE_PLEDGE} = ($compiles && $status == 0) ? 1 : 0;
});

my $unveilCode = <<EOT;
#include <unistd.h>
int main(int argc, char* argv[]) {
    unveil(NULL, NULL);
    return 0;
}
EOT
compile("for unveil", $unveilCode, $compiler, sub {
    my ($compiles, $status) = @_;
    $defines{HAVE_UNVEIL} = ($compiles && $status == 0) ? 1 : 0;
});

my $errhCode = <<EOT;
#include <err.h>
#include <errno.h>
int main(int argc, char* argv[]) {
    errno = EAGAIN;
    warn("Warning %d", 42);
    err(126, "Message %d", 126);
    return 0;
}
EOT
compile("for err() and warn()", $errhCode, $compiler, sub {
    my ($compiles, $status) = @_;
    my $include = "#include <err.h>";
    my $fake = <<EOT;
#define err(eval, fmt, ...) do{\\
if(errno) {\\
fprintf(stderr, "ERR: " fmt ": %d\n",##__VA_ARGS__, errno);\\
exit((eval));\\
}\\
}while(0)
#define warn(fmt, ...) do{\\
fprintf(stderr, "WARN: " fmt ": %d\n",##__VA_ARGS__, errno);\\
}while(0)
EOT
    $defines{ERRH} = ($compiles && $status == 126) ? $include : $fake;
});



####################################################################
# write output
####################################################################

my $systempaths = <<EOT;
#define ETC_CONF "$etc_conf"
#define HOME_CONF "$home_conf"
EOT

open A, ">config.h";
print A <<EOT;
// This file was generated by ./configure.pl
// Feel free to modify the defaults
#ifndef CONFIG_H
#define CONFIG_H

$defines{ERRH}

#define HAVE_PLEDGE $defines{HAVE_PLEDGE}
#define HAVE_UNVEIL $defines{HAVE_UNVEIL}

$systempaths

#endif
EOT
close(A);

my $cflags = $ENV{CFLAGS} || "";
my $ldflags = $ENV{LDFLAGS} || "";

open A, ">Makefile.vars";
print A <<EOT;
# This file was generated by ./configure.pl
# Feel free to modify the defaults
CC = $compiler
CFLAGS = $cflags
LDFLAGS = $ldflags
PREFIX = $prefix
EOT
close(A);
