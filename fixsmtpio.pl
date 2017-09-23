#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(_exit);

my $exitcode = 0;

sub accepted_data { my ($response) = @_;
    return $response =~ /^354 /;
}

sub munge_timeout { my ($response) = @_;
    $exitcode = 16;
    return $response;
}

sub munge_greeting { my ($response) = @_;
    if ($response =~ /^4[0-9]{2} /) {
        $exitcode = 14;
    } elsif ($response =~ /^5[0-9]{2} /) {
        $exitcode = 15;
    } else {
        $response = q{235 ok};
        $response .= qq{, $ENV{AUTHUSER},} if defined $ENV{AUTHUSER};
        $response .= qq{ go ahead $< (#2.0.0)};
    }
    return $response;
}

sub munge_help { my ($response) = @_;
    $response = q{214 fixsmtpio.pl home page: }
        . q{https://schmonz.com/qmail/authutils}
        . "\r\n"
        . $response;
    return $response;
}

sub munge_test { my ($response) = @_;
    $response .= q{ and also it's mungeable};
    return $response;
}

sub munge_ehlo { my ($response) = @_;
    my @lines = split(/\r\n/, $response);
    @lines = grep { ! /^250.AUTH / } @lines;
    @lines = grep { ! /^250.STARTTLS/ } @lines;
    $response = join("\r\n", @lines);
    return $response;
}

sub change_every_line_fourth_char_to_dash { my ($multiline) = @_;
    $multiline =~ s/^(.{3})./$1-/msg;
    return $multiline;
}

sub change_last_line_fourth_char_to_space { my ($multiline) = @_;
    $multiline =~ s/(.{3})-(.+)$/$1 $2/;
    return $multiline;
}

sub reformat_multiline_response { my ($response) = @_;
    # XXX maybe fix up line breaks? or just keep them correct all along
    $response = change_every_line_fourth_char_to_dash($response);
    $response = change_last_line_fourth_char_to_space($response);
    $response .= "\r\n";
    return $response;
}

sub strip_last_eol { my ($string) = @_;
    $string =~ s/\r?\n$//;
    return $string;
}

sub munge_response { my ($response, $verb, $arg) = @_;
    $response = strip_last_eol($response);
    $response = munge_timeout($response) if '' eq $verb;
    $response = munge_greeting($response) if 'greeting' eq $verb;
    $response = munge_help($response) if 'help' eq $verb;
    $response = munge_test($response) if 'test' eq $verb;
    $response = munge_ehlo($response) if 'ehlo' eq $verb;
    return reformat_multiline_response($response);
}

sub could_be_final_response_line { my ($line) = @_;
    return length($line) >= 4 && substr($line, 3, 1) eq " ";
}

sub is_entire_response { my ($response) = @_;
    my @lines = split(/\r\n/, $response);
    return could_be_final_response_line($lines[-1]) && is_entire_line($response);
}

#####

sub die_usage { die "usage: fixsmtpio.pl prog [ arg ... ]\n"; }
sub die_pipe  { die "fixsmtpio.pl: unable to open pipe: $!\n"; }
sub die_fork  { die "fixsmtpio.pl: unable to fork: $!\n"; }
sub die_read  { die "fixsmtpio.pl: unable to read: $!\n"; }
sub die_write { die "fixsmtpio.pl: unable to write: $!\n"; }

sub use_as_stdin { my ($fd) = @_;
    open(STDIN, '<&=', $fd) || die_pipe();
}

sub use_as_stdout { my ($fd) = @_;
    open(STDOUT, '>>&=', $fd) || die_pipe();
}

sub mypipe { my ($from_ref, $to_ref) = @_;
    pipe(${$from_ref}, ${$to_ref}) or die_pipe();
}

sub setup_server { my ($from_proxy, $to_server,
                       $from_server, $to_proxy) = @_;
    close($from_server);
    close($to_server);
    use_as_stdin($from_proxy);
    use_as_stdout($to_proxy);
}

sub exec_server_and_never_return { my (@argv) = @_;
    exec @argv;
    die;
}

sub be_child { my ($from_proxy, $to_proxy,
                   $from_server, $to_server,
                   @args) = @_;
    setup_server($from_proxy, $to_server, $from_server, $to_proxy);
    exec_server_and_never_return(@args);
}

sub setup_proxy { my ($from_proxy, $to_proxy) = @_;
    close($from_proxy);
    close($to_proxy);
}

sub is_entire_line { my ($string) = @_;
    return substr($string, -1, 1) eq "\n";
}

my $want_to_read_bits;

sub want_to_read { my ($fd) = @_;
    vec($want_to_read_bits, fileno($fd), 1) = 1;
}

my $can_read_bits;

sub can_read { my ($fd) = @_;
    return 1 == vec($can_read_bits, fileno($fd), 1);
}

sub can_read_something {
    my $ready;
    $ready = select($can_read_bits = $want_to_read_bits, undef, undef, undef);
    die_read() if $ready == -1 && ! $!{EINTR};
    return $ready;
}

sub saferead { my ($fd, $buf_ref, $len) = @_;
    my $r;
    $r = sysread($fd, ${$buf_ref}, $len);
    die_read() if $r == -1 && ! $!{EINTR};
    return $r;
}

sub safeappend { my ($string_ref, $fd) = @_;
    my ($r, $buf);
    $r = saferead($fd, \$buf, 128);
    ${$string_ref} .= $buf;
    return $r;
}

sub is_last_line_of_data { my ($r) = @_;
    return $r =~ /^\.\r$/;
}

sub parse_request { my ($request, $verb_ref, $arg_ref) = @_;
    my $chomped;

    $chomped = strip_last_eol($request);

    (${$verb_ref}, ${$arg_ref}) = split(/ /, $chomped, 2);
    ${$verb_ref} ||= '';
    ${$arg_ref}  ||= '';
    ${$verb_ref} = lc(${$verb_ref});
}

sub logit { my ($logprefix, $s) = @_;
    print STDERR "$logprefix: $s";
}

sub write_to_client { my ($client, $response) = @_;
    syswrite($client, $response) || die_write();
    logit('O', $response);
}

sub write_to_server { my ($server, $request) = @_;
    syswrite($server, $request) || die_write();
    logit('I', $request);
}

sub smtp_test { my ($verb, $arg) = @_;
    return "250 fixsmtpio.pl test ok: $arg";
}

sub smtp_unimplemented { my ($verb, $arg) = @_;
    return "502 unimplemented (#5.5.1)";
}

sub verb_matches { my ($s, $sa) = @_;
    return 0 unless length $sa;
    return lc $s eq lc $sa;
}

sub handle_internally { my ($request, $verb_ref, $arg_ref) = @_;
    parse_request($request, $verb_ref, $arg_ref);

    return smtp_test(${$verb_ref}, ${$arg_ref}) if verb_matches('test', ${$verb_ref});
    return smtp_unimplemented(${$verb_ref}, ${$arg_ref}) if verb_matches('auth', ${$verb_ref});
    return smtp_unimplemented(${$verb_ref}, ${$arg_ref}) if verb_matches('starttls', ${$verb_ref});

    return "";
}

sub send_keepalive { my ($server, $request) = @_;
    write_to_server($server, "NOOP " . $request);
}

sub blocking_line_read { my ($fd) = @_;
    my $line = <$fd>;
    return $line;
}

sub handle_request { my ($from_client, $to_server,
                         $from_server, $to_client,
                         $request, $verb_ref, $arg_ref,
                         $want_data_ref, $in_data_ref) = @_;
    my $internal_response;

    $request = strip_last_eol($request) . "\r\n";

    if (${$in_data_ref}) {
        write_to_server($to_server, $request);
        if (is_last_line_of_data($request)) {
            ${$in_data_ref} = 0;
        }
    } else {
        if ($internal_response = handle_internally($request, $verb_ref, $arg_ref)) {
            logit('I', $request);
            write_to_client($to_client,
                munge_response($internal_response, ${$verb_ref}, ${$arg_ref}));
            (${$verb_ref}, ${$arg_ref}) = ('', '');

            send_keepalive($to_server, $request);
            $internal_response = blocking_line_read($from_server);
            logit('O', $internal_response);
        } else {
            ${$want_data_ref} = 1 if (verb_matches('data', ${$verb_ref}));
            write_to_server($to_server, $request);
        }
    }
}

sub handle_response { my ($to_client, $response, $verb, $arg,
                          $want_data_ref, $in_data_ref) = @_;
    if (${$want_data_ref}) {
        ${$want_data_ref} = 0;
        if (accepted_data($response)) {
            ${$in_data_ref} = 1;
        }
    }
    write_to_client($to_client, munge_response($response, $verb, $arg));
}

sub do_proxy_stuff { my ($from_client, $to_server,
                         $from_server, $to_client) = @_;
    my ($request, $verb, $arg, $response) = ('', '', '', '');
    my ($want_data, $in_data) = (0, 0);

    handle_request($from_client, $to_server, $from_server, $to_client,
        'greeting', \$verb, \$arg,
        \$want_data, \$in_data);

    want_to_read($from_client);
    want_to_read($from_server);
    for (;;) {
        next unless can_read_something();

        if (can_read($from_client)) {
            last unless safeappend(\$request, $from_client);
            if (is_entire_line($request)) {
                handle_request($from_client, $to_server,
                    $from_server, $to_client,
                    $request, \$verb, \$arg,
                    \$want_data, \$in_data);
                $request = '';
            }
        }

        if (can_read($from_server)) {
            last unless safeappend(\$response, $from_server);
            if (is_entire_response($response)) {
                handle_response($to_client, $response, $verb, $arg,
                    \$want_data, \$in_data);
                $response = '';
                ($verb, $arg) = ('','');
            }
        }
    }
}

sub wait_crashed { my ($wstat) = @_;
    return $wstat & 127;
}

sub teardown_proxy_and_exit { my ($child, $from_server, $to_server) = @_;
    my $wstat;

    close($from_server);
    close($to_server);

    die if (waitpid($child, 0) == -1); $wstat = $?;
    die if (wait_crashed($wstat));

    _exit($exitcode);
}

sub be_parent { my ($from_client, $to_client,
                    $from_proxy, $to_proxy,
                    $from_server, $to_server,
                    $child) = @_;
    setup_proxy($from_proxy, $to_proxy);
    do_proxy_stuff($from_client, $to_server, $from_server, $to_client);

    teardown_proxy_and_exit($child, $from_server, $to_server);
}

sub main { my (@args) = @_;
    my $from_client;
    my ($from_proxy, $to_server);
    my ($from_server, $to_proxy);
    my $to_client;
    my $child;

    die_usage() unless @args >= 1;

    $from_client = \*STDIN;
    mypipe(\$from_proxy, \$to_server);
    mypipe(\$from_server, \$to_proxy);
    $to_client = \*STDOUT;

    if ($child = fork()) {
        be_parent($from_client, $to_client,
                  $from_proxy, $to_proxy,
                  $from_server, $to_server,
                  $child);
    } elsif (defined $child) {
        be_child($from_proxy, $to_proxy,
                 $from_server, $to_server,
                 @args);
    } else {
        die_fork();
    }
}

main(@ARGV);
