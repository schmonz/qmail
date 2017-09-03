#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(_exit);

sub send_request {
    my ($to_server, $verb, $arg) = @_;
    syswrite($to_server,"$verb $arg$/");
}

sub send_data {
    my ($to_server, $data) = @_;
    syswrite($to_server, $data);
}

sub accepted_data {
    my ($response) = @_;
    return $response =~ /^354 /;
}

sub munge_banner {
    my ($response) = @_;

    $response = q{235 ok};
    $response .= qq{, $ENV{SMTPUSER},} if defined $ENV{SMTPUSER};
    $response .= qq{ go ahead $< (#2.0.0)};

    return $response;
}

sub munge_help {
    my ($response) = @_;

    $response = q{214 fixsmtpio.pl home page: }
        . q{https://schmonz.com/qmail/authutils}
        . $/
        . $response;

    return $response;
}

sub munge_test {
    my ($response) = @_;

    $response .= q{ and also it's mungeable};

    return $response;
}

sub munge_ehlo {
    my ($response) = @_;

    my @lines = split(/\r\n/, $response);
    @lines = grep { ! /^250.AUTH / } @lines;
    @lines = grep { ! /^250.STARTTLS/ } @lines;
    $response = join("\r\n", @lines);

    return $response;
}

sub change_every_line_fourth_char_to_dash {
    my ($multiline) = @_;
    $multiline =~ s/^(.{3})./$1-/msg;
    return $multiline;
}

sub change_last_line_fourth_char_to_space {
    my ($multiline) = @_;
    $multiline =~ s/(.{3})-(.+)$/$1 $2/;
    return $multiline;
}

sub reformat_multiline_response {
    my ($response) = @_;

    # XXX maybe fix up line breaks? or just keep them correct all along

    $response = change_every_line_fourth_char_to_dash($response);
    $response = change_last_line_fourth_char_to_space($response);

    return $response;
}

sub munge_response {
    my ($verb, $arg, $response) = @_;

    chomp($response);

    $response = munge_banner($response) if 'banner' eq $verb;
    $response = munge_help($response) if 'help' eq $verb;
    $response = munge_test($response) if 'test' eq $verb;
    $response = munge_ehlo($response) if 'ehlo' eq $verb;

    return reformat_multiline_response($response);
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
    my @file_descriptors = @_;

    for my $fd (@file_descriptors) {
        vec($rin, fileno($fd), 1) = 1;
    }
}

my $rout;
sub can_read_more {
    my ($file_descriptor) = @_;

    return 1 == vec($rout, fileno($file_descriptor), 1);
}

sub something_can_be_read_from {
    my $nfound = select($rout = $rin, undef, undef, 1200);

    return $nfound;
}

sub saferead {
    my ($file_descriptor) = @_;
    my $read_buffer;
    my $read_size = 128;

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
    $verb = lc($verb);
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

sub is_entire_line {
    my ($line) = @_;
    return substr($line, -1, 1) eq "\n";
}

sub is_last_line_of_data {
    my ($line) = @_;
    return $line =~ /^\.\r$/;
}

sub could_be_final_line {
    my ($line) = @_;
    return length($line) >= 4 && substr($line, 3, 1) eq " ";
}

sub is_entire_response {
    my ($response) = @_;
    my @lines = split(/\r\n/, $response);
    return could_be_final_line($lines[-1]) && substr($response, -1, 1) eq "\n";
}

sub handle_request {
    my ($from_client, $to_server, $to_client, $request, $verb_ref, $arg_ref, $want_data_ref, $in_data_ref) = @_;

    if (${$in_data_ref}) {
        send_data($to_server, $request);
        if (is_last_line_of_data($request)) {
            ${$in_data_ref} = 0;
        }
    } else {
        (${$verb_ref}, ${$arg_ref}) = parse_request($request);
        if (${$verb_ref} eq 'data') {
            ${$want_data_ref} = 1;
        }
        dispatch_request(${$verb_ref}, ${$arg_ref}, $to_server, $to_client);
    }
}

sub handle_response {
    my ($to_client, $response, $banner_sent_ref, $verb_ref, $arg_ref, $want_data_ref, $in_data_ref) = @_;

    if (! ${$banner_sent_ref}) {
        (${$verb_ref}, ${$arg_ref}) = ('banner', '');
        ${$banner_sent_ref} = 1;
    } elsif (${$want_data_ref}) {
        ${$want_data_ref} = 0;
        if (accepted_data($response)) {
            ${$in_data_ref} = 1;
        }
    }

    send_response($to_client, munge_response(${$verb_ref}, ${$arg_ref}, $response));
}

sub do_proxy_stuff {
    my ($from_client, $to_server, $from_server, $to_client) = @_;

    intend_to_be_reading($from_client, $from_server);

    my ($verb, $arg) = ('', '');
    my ($request, $response) = ('', '');
    my $banner_sent = 0;
    my $want_data = 0;
    my $in_data = 0;

	for (;;) {
        next unless something_can_be_read_from();

        if (can_read_more($from_client)) {
            $request .= saferead($from_client);
            if (is_entire_line($request)) {
                handle_request($from_client, $to_server, $to_client, $request, \$verb, \$arg, \$want_data, \$in_data);
                $request = '';
            }
        }

        if (can_read_more($from_server)) {
            $response .= saferead($from_server);
            if (is_entire_response($response)) {
                handle_response($to_client, $response, \$banner_sent, \$verb, \$arg, \$want_data, \$in_data);
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

When fixsmtpio makes itself server's child, and server times out
- fixsmtpio quits (this is good)
- server exits nonzero, so authup says "authorization failed" (bad)

So maybe fixsmtpio needs to make itself server's parent
- waitpid(server)
- _exit(0) unconditionally, until proven otherwise

Do it more like C:
- no chomp()
- loop over characters looking for \n (and deal with \r being around)

The rules are hardcoded
- put them in a config file
- parse it and load the rules

If server sometimes needs to communicate an error to qmail-authup...
- have fixsmtpio exit a unique nonzero from observing SMTP code and message
- have authup understand all these nonzero exit codes

# sudo tcpserver 0 26 ./qmail-authup me.local checkpassword-pam -s sshd /Users/schmonz/Documents/trees/qmail/fixsmtpio.pl qmail-smtpd
