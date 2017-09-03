#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(_exit);

sub send_request {
    my ($to_server, $verb, $arg) = @_;
    syswrite($to_server,"$verb $arg$/");
}

sub receive_response {
    my ($from_server) = @_;
    chomp(my $response = <$from_server>);
    return $response;
}

sub munge_response {
    my ($verb, $arg, $response) = @_;

    chomp($response);

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
    syswrite($to_client, "$response$/");
}

sub setup_server {
    my ($from_proxy, $to_server, $from_server, $to_proxy) = @_;
    close($from_server);
    close($to_server);
    open(STDIN, '<&=', $from_proxy);
    open(STDOUT, '>>&=', $to_proxy);
}

sub exec_server_and_never_return {
    my (@argv) = @_;
    exec @argv;
}

sub smtp_test {
    my ($verb, $arg) = @_;
    return "250 fixsmtpio.pl test ok: $arg";
}

sub smtp_unimplemented {
    my ($verb, $arg) = @_;
    return "502 unimplemented (#5.5.1)";
}

sub handle_internally {
    my ($verb, $arg) = @_;

    return \&smtp_test if 'test' eq $verb;
    return \&smtp_unimplemented if 'auth' eq $verb;
    return \&smtp_unimplemented if 'starttls' eq $verb;

    return 0;
}

sub setup_proxy {
    my ($from_proxy, $to_proxy) = @_;
    close($from_proxy);
    close($to_proxy);
    $/ = "\r\n" if is_network_service();
}

my $rin;
sub intend_to_be_reading {
    my ($file_descriptor) = @_;

    vec($rin, fileno($file_descriptor), 1) = 1;
}

my $rout;
sub can_read_more {
    my ($file_descriptor) = @_;

    return 1 == vec($rout, fileno($file_descriptor), 1);
}

sub something_can_be_read_from {
    my $timeout = 3.14159;
    my $nfound = select($rout = $rin,undef,undef,$timeout);

    return $nfound;
}

sub saferead {
    my ($file_descriptor) = @_;
    my $read_buffer;
    my $read_size = 1;

    my $num_bytes = sysread($file_descriptor, $read_buffer, $read_size);

    # XXX as child, seems ok
    # XXX as parent, close() / waitpid() / other cleanup before exit
    die "sysread: $!\n" if $num_bytes == -1;
    die "\n" if $num_bytes == 0; # EOF

    return $read_buffer;
}

sub parse_request {
    my ($request) = @_;

    chomp($request);
    my ($verb, $arg) = split(/ /, $request, 2);
    $arg ||= '';

    return ($verb, $arg);
}

sub dispatch_request {
    my ($verb, $arg, $to_server, $to_client) = @_;

    if (my $sub = handle_internally($verb, $arg)) {
        send_response($to_client, munge_response($verb, $arg, $sub->($verb, $arg)));
    } else {
        send_request($to_server, $verb, $arg);
    }
}

sub do_proxy_stuff {
    my ($from_client, $to_server, $from_server, $to_client) = @_;

    intend_to_be_reading($from_client);
    intend_to_be_reading($from_server);

    my ($verb, $arg) = ('', '');
    my ($request, $response) = ('', '');
    my $banner_sent = 0;

	for (;;) {
        next unless something_can_be_read_from();

        if (can_read_more($from_client)) {
            my $partial_request = saferead($from_client);

            $request .= $partial_request;
            if ("\n" eq $partial_request) {
                ($verb, $arg) = parse_request($request);
                $request = '';
                dispatch_request($verb, $arg, $to_server, $to_client);
            }
        }

        if (can_read_more($from_server)) {
            my $partial_response = saferead($from_server);

            $response .= $partial_response;
            # XXX incorrect! could be multiple lines
            if ("\n" eq $partial_response) {
                if (! $banner_sent) {
                    ($verb, $arg) = ('banner', '');
                    $banner_sent = 1;
                }
                send_response($to_client, munge_response($verb, $arg, $response));
                $response = '';
                ($verb, $arg) = ('','');
            }
        }
	}
}

sub teardown_proxy_and_never_return {
    my ($from_server, $to_server) = @_;
    close($from_server);
    close($to_server);
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

    pipe(my $from_server, my $to_proxy) or die "pipe: $!";
    pipe(my $from_proxy, my $to_server) or die "pipe: $!";

    my $from_client = \*STDIN;
    my $to_client = \*STDOUT;

    if (my $pid = fork()) {
        setup_server($from_proxy, $to_server, $from_server, $to_proxy);
        exec_server_and_never_return(@args);
    } elsif (defined $pid) {
        setup_proxy($from_proxy, $to_proxy);
        do_proxy_stuff($from_client, $to_server, $from_server, $to_client);
        teardown_proxy_and_never_return($from_server, $to_server);
    } else {
        die "fork: $!"
    }
}

main(@ARGV);

__DATA__

Then set read size to whatever qmail does
Then understand the "timeout" param to select() and set it to something
Then try being the parent instead of the child

We don't understand multiline responses (such as from EHLO)
- maybe the above will help receive them
- then we have to parse, and munge according to the rules

We don't understand multiline requests (such as after DATA)
- are there any others besides DATA?
- must know when it ends

The rules are hardcoded
- put them in a config file
- parse it and load the rules

When fixsmtpio makes itself server's child, and server times out
- fixsmtpio quits (this is good)
- server exits nonzero, so authup says "authorization failed" (bad)

So maybe fixsmtpio needs to make itself server's parent
- waitpid(server)
- _exit(0) unconditionally, until proven otherwise

If server sometimes needs to communicate an error to qmail-authup...
- have fixsmtpio exit a unique nonzero from observing SMTP code and message
- have authup understand all these nonzero exit codes

# sudo tcpserver 0 26 ./qmail-authup me.local checkpassword-pam -s sshd /Users/schmonz/Documents/trees/qmail/fixsmtpio.pl qmail-smtpd
