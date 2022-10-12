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
$ARGV[0] [-prefix=/usr/local]
    -prefix=/usr/local                     set prefix for install
    -help                                  print this message
    -etc_conf=/etc/jakobmenu.conf          default /etc conf path
    -etc_conf=\\~/.config/jakobmenu/conf   default user config path
EOT
    exit(1);
}

sub tryCompiler {
    my ($cc) = @_;

    # TODO check if compiler is $CC, cc, gcc, clang, etc...
    print "Checking compiler works...\n";
    open A, ">test.c";
    # TODO check actual pledges work...
    print A <<EOT;
#include <stdio.h>
int main(int argc, char* argv[]) {
    printf("Hello, world!\\n");
    return 0;
}
EOT
    close(A);
    
    my $compilerOk = system("$cc test.c && ./a.out") ? 0 : 1;
    unlink("test.c");
    unlink("a.out");

    return $compilerOk;
}

my $compiler = undef;

foreach my $cc ($ENV{CC}, 'cc', 'gcc', 'clang') {
    next if !defined($cc);
    next if !tryCompiler($cc);
    $compiler = $cc;
    last;
}

die("Can't rely on compiler") if not $compiler;

print "Checking for pledge...\n";
open A, ">test.c";
# TODO check actual pledges work...
print A <<EOT;
#include <unistd.h>
int main(int argc, char* argv[]) {
    pledge(NULL, NULL);
    return 0;
}
EOT
close(A);

$defines{HAVE_PLEDGE} = system("$compiler test.c && ./a.out") ? 0 : 1;
unlink("test.c");
unlink("a.out");


print "Checking for unveil...\n";
open A, ">test.c";
print A <<EOT;
#include <unistd.h>
int main(int argc, char* argv[]) {
    unveil(NULL, NULL);
    return 0;
}
EOT
close(A);

$defines{HAVE_UNVEIL} = system("$compiler test.c && ./a.out") ? 0 : 1;
unlink("test.c");
unlink("a.out");

# TODO check err.h or fake it
print("Checking for err...\m");
open A, ">test.c";
print A <<EOT;
#include <err.h>
#include <errno.h>
int main(int argc, char* argv[]) {
    errno = EAGAIN;
    warn("Warning %d", 42);
    err(126, "Message %d", 126);
    return 0;
}
EOT
close(A);
if(system("$compiler test.c") > 0) {
    $defines{HAVE_GOOD_ERR} = 0;
} else {
    $defines{HAVE_GOOD_ERR} = (system("./a.out") >> 8) == 126 ? 1 : 0;
}
unlink("test.c");
unlink("a.out");

my $errh = $defines{HAVE_GOOD_ERR} ? "#include <err.h>" : <<EOT;
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

my $systempaths = <<EOT;
#define ETC_CONF "$etc_conf"
#define HOME_CONF "$home_conf"
EOT

open A, ">config.h";
print A <<EOT;
#ifndef CONFIG_H
#define CONFIG_H

$errh

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
CC = $compiler
CFLAGS = $cflags
LDFLAGS = $ldflags
PREFIX = $prefix
EOT
close(A);
