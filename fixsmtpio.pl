#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(_exit);

sub send_request {
    my ($to_smtpd, $verb, $arg) = @_;
    print $to_smtpd "$verb $arg$/";
    $to_smtpd->flush();
}

sub receive_response {
    my ($from_smtpd) = @_;
    chomp(my $response = <$from_smtpd>);
    return $response;
}

sub munge_response {
    my ($verb, $arg, $response) = @_;

    if ('banner' eq $verb) {;
        $response = qq{235 ok go ahead $< (#2.0.0)};
        if (defined $ENV{SMTPUSER}) {
            $response =~ s/ok go/ok, $ENV{SMTPUSER}, go/;
        }
    }

    if ('help' eq $verb) {
        $response = q{214-fixsmtpio.pl home page: https://schmonz.com/qmail/authutils} . $/ . $response;
    }

    $response .= q{ and also it's mungeable}
        if 'test' eq $verb;

    return $response;
}

sub send_response {
    my ($to_client, $response) = @_;
    print $to_client "$response$/";
    $to_client->flush();
}

sub setup_smtpd {
    my ($from_proxy, $to_smtpd, $from_smtpd, $to_proxy) = @_;
    close($from_smtpd);
    close($to_smtpd);
    open(STDIN, '<&=', $from_proxy);
    open(STDOUT, '>>&=', $to_proxy);
}

sub exec_smtpd_and_never_return {
    my (@argv) = @_;
    exec @argv;
}

sub smtp_test {
    my ($verb, $arg) = @_;
    return "250 fixsmtpio.pl test ok: $arg";
}

sub handle_internally {
    my ($verb, $arg) = @_;

    return \&smtp_test if 'test' eq $verb;

    return 0;
}

sub setup_proxy {
    my ($from_proxy, $to_proxy) = @_;
    close($from_proxy);
    close($to_proxy);
    $/ = "\r\n" if is_network_service();
}

sub do_proxy_stuff {
    my ($from_client, $to_smtpd, $from_smtpd, $to_client) = @_;
    my $response;

    send_response($to_client, munge_response('banner', '', receive_response($from_smtpd)));

    while (my $request = <$from_client>) {
        chomp $request;
        my $response;

        my ($verb, $arg) = split(/ /, $request, 2);
        $arg ||= '';
        if (my $sub = handle_internally($verb, $arg)) {
            $response = $sub->($verb, $arg);
        } else {
            send_request($to_smtpd, $verb, $arg);
            $response = receive_response($from_smtpd);
        }

        send_response($to_client, munge_response($verb, $arg, $response));
    }
}

sub teardown_proxy_and_never_return {
    my ($from_smtpd, $to_smtpd) = @_;
    close($from_smtpd);
    close($to_smtpd);
    _exit(77);
}

sub is_network_service {
    return defined $ENV{TCPREMOTEIP};
}

sub main {
    my (@args) = @_;

    if ($< == 0) {
        warn "fixsmtpio.pl refuses to run as root\n";
        exit(1);
    }
    die "usage: fixsmtpio.pl program [ arg ... ]\n" unless @args >= 1;

    pipe(my $from_smtpd, my $to_proxy) or die "pipe: $!";
    pipe(my $from_proxy, my $to_smtpd) or die "pipe: $!";

    my $from_client = \*STDIN;
    my $to_client = \*STDOUT;

    if (my $pid = fork()) {
        setup_smtpd($from_proxy, $to_smtpd, $from_smtpd, $to_proxy);
        exec_smtpd_and_never_return(@args);
    } elsif (defined $pid) {
        setup_proxy($from_proxy, $to_proxy);
        do_proxy_stuff($from_client, $to_smtpd, $from_smtpd, $to_client);
        teardown_proxy_and_never_return($from_smtpd, $to_smtpd);
    } else {
        die "fork: $!"
    }
}

main(@ARGV);

__DATA__

We'll deadlock if we're expecting the wrong number of lines from the server
- use 4-arg select() with sysread(), instead of $line = <$fd>
- and therefore must use syswrite() as well

We don't understand multiline responses (such as from EHLO)
- maybe the above will help receive them
- then we have to parse, and munge according to the rules

The rules are hardcoded
- put them in a config file
- parse it and load the rules

When fixsmtpio makes itself smtpd's child, and smtpd times out
- fixsmtpio quits (this is good)
- smtpd exits nonzero, so smtpup says "authorization failed" (bad)

So maybe fixsmtpio needs to make itself smtpd's parent
- waitpid(smtpd)
- _exit(0) unconditionally, until proven otherwise

If smtpd sometimes needs to communicate an error to qmail-smtpup...
- have fixsmtpio exit a unique nonzero from observing SMTP code and message
- have smtpup understand all these nonzero exit codes

# sudo tcpserver 0 26 ./qmail-smtpup me.local checkpassword-pam -s sshd /Users/schmonz/Documents/trees/qmail/fixsmtpio.pl qmail-smtpd
