#!/usr/bin/env perl

use warnings;
use strict;

use POSIX qw(_exit);

sub die_usage { die "usage: fixsmtpio.pl prog [ arg ... ]\n"; }
sub die_pipe  { die "fixsmtpio.pl: unable to open pipe: $!\n"; }
sub die_fork  { die "fixsmtpio.pl: unable to fork: $!\n"; }
sub die_read  { die "fixsmtpio.pl: unable to read: $!\n"; }
sub die_write { die "fixsmtpio.pl: unable to write: $!\n"; }

# XXX struct request_response

my $exitcode = 0;

sub strip_last_eol { my ($string_ref) = @_;
    ${$string_ref} =~ s/\r?\n$//;
}

sub accepted_data { my ($response) = @_;
    return $response =~ /^354 /;
}

sub munge_timeout { my ($response_ref) = @_;
    $exitcode = 16;
}

sub munge_greeting { my ($response_ref) = @_;
    if (${$response_ref} =~ /^4/) {
        $exitcode = 14;
    } elsif (${$response_ref} =~ /^5/) {
        ${$response_ref} = 15;
    } else {
        ${$response_ref} = q{235 ok};
        ${$response_ref} .= qq{, $ENV{AUTHUSER},} if defined $ENV{AUTHUSER};
        ${$response_ref} .= qq{ go ahead $< (#2.0.0)};
    }
}

sub munge_help { my ($response_ref) = @_;
    ${$response_ref} = q{214 fixsmtpio.pl home page: }
        . q{https://schmonz.com/qmail/authutils}
        . "\r\n"
        . ${$response_ref};
}

sub munge_test { my ($response_ref) = @_;
    ${$response_ref} .= q{ and also it's mungeable};
}

sub munge_ehlo { my ($response_ref) = @_;
    # XXX @avoids
    my @lines = split(/\r\n/, ${$response_ref});
    @lines = grep { ! /^250.AUTH / } @lines;
    @lines = grep { ! /^250.STARTTLS/ } @lines;
    ${$response_ref} = join("\r\n", @lines);
}

sub verb_matches { my ($s, $sa) = @_;
    return 0 unless length $sa;
    return lc $s eq lc $sa;
}

sub change_every_line_fourth_char_to_dash { my ($multiline_ref) = @_;
    ${$multiline_ref} =~ s/^(.{3})./$1-/msg;
}

sub change_last_line_fourth_char_to_space { my ($multiline_ref) = @_;
    ${$multiline_ref} =~ s/(.{3})-(.+)$/$1 $2/;
}

sub reformat_multiline_response { my ($response_ref) = @_;
    # XXX maybe fix up line breaks? or just keep them correct all along
    change_every_line_fourth_char_to_dash($response_ref);
    change_last_line_fourth_char_to_space($response_ref);
    ${$response_ref} .= "\r\n";
}

sub munge_response { my ($response_ref, $verb, $arg) = @_;
    strip_last_eol($response_ref);
    munge_timeout($response_ref) if '' eq $verb;
    munge_greeting($response_ref) if verb_matches('greeting', $verb);
    munge_help($response_ref) if verb_matches('help', $verb);
    munge_test($response_ref) if verb_matches('test', $verb);
    munge_ehlo($response_ref) if verb_matches('ehlo', $verb);
    reformat_multiline_response($response_ref);
}

sub could_be_final_response_line { my ($line) = @_;
    return length($line) >= 4 && substr($line, 3, 1) eq " ";
}

sub is_entire_response { my ($response) = @_;
    my @lines = split(/\r\n/, $response);
    return could_be_final_response_line($lines[-1]) && is_entire_line($response);
}

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

sub parse_request { my ($request_ref, $verb_ref, $arg_ref) = @_;
    strip_last_eol($request_ref);

    (${$verb_ref}, ${$arg_ref}) = split(/ /, ${$request_ref}, 2);
    ${$verb_ref} ||= '';
    ${$arg_ref}  ||= '';

    ${$request_ref} .= "\r\n";
}

sub logit { my ($logprefix, $s) = @_;
    print STDERR "$logprefix: $s";
}

sub write_to_client { my ($fd, $s) = @_;
    syswrite($fd, $s) || die_write();
    logit('O', $s);
}

sub write_to_server { my ($fd, $s) = @_;
    syswrite($fd, $s) || die_write();
    logit('I', $s);
}

sub smtp_test { my ($verb, $arg) = @_;
    return "250 fixsmtpio.pl test ok: $arg";
}

sub smtp_unimplemented { my ($verb, $arg) = @_;
    return "502 unimplemented (#5.5.1)";
}

sub handle_internally { my ($request, $verb_ref, $arg_ref) = @_;
    return "" if verb_matches('noop', ${$verb_ref});
    return smtp_test(${$verb_ref}, ${$arg_ref}) if verb_matches('test', ${$verb_ref});
    return smtp_unimplemented(${$verb_ref}, ${$arg_ref}) if verb_matches('auth', ${$verb_ref});
    return smtp_unimplemented(${$verb_ref}, ${$arg_ref}) if verb_matches('starttls', ${$verb_ref});

    return "";
}

sub send_keepalive { my ($server, $request) = @_;
    write_to_server($server, "NOOP fixsmtpio.pl " . $request);
}

sub check_keepalive { my ($client, $response) = @_;
    if ($response !~ /^250 ok/) {
        write_to_client($client, $response);
        die();
    }
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
    my $keepalive_response;

    if (${$in_data_ref}) {
        write_to_server($to_server, $request);
        ${$in_data_ref} = 0 if is_last_line_of_data($request);
    } else {
        if ($internal_response = handle_internally($request, $verb_ref, $arg_ref)) {
            send_keepalive($to_server, $request);
            $keepalive_response = blocking_line_read($from_server);
            check_keepalive($to_client, $keepalive_response);
            logit('O', $keepalive_response);

            logit('I', $request);
            munge_response(\$internal_response, ${$verb_ref}, ${$arg_ref});
            write_to_client($to_client, $internal_response);
            (${$verb_ref}, ${$arg_ref}) = ('', '');
        } else {
            ${$want_data_ref} = 1 if (verb_matches('data', ${$verb_ref}));
            write_to_server($to_server, $request);
        }
    }
}

sub handle_response { my ($to_client,
                          $response, $verb, $arg,
                          $want_data_ref, $in_data_ref) = @_;
    if (${$want_data_ref}) {
        ${$want_data_ref} = 0;
        ${$in_data_ref} = 1 if accepted_data($response);
    }
    munge_response(\$response, $verb, $arg);
    write_to_client($to_client, $response);
}

# XXX sub request_response_init()

sub do_proxy_stuff { my ($from_client, $to_server,
                         $from_server, $to_client) = @_;
    my ($partial_request, $partial_response) = ('', '');
    my ($request, $verb, $arg, $response) = ('', '', '', '');
    my ($want_data, $in_data) = (0, 0);

    $request = 'greeting';

    want_to_read($from_client);
    want_to_read($from_server);
    for (;;) {
        if (length $request && ! length $response) {
            parse_request(\$request, \$verb, \$arg);
            handle_request($from_client, $to_server,
                           $from_server, $to_client,
                           $request, \$verb, \$arg,
                           \$want_data, \$in_data);
        }

        if (length $response) {
            handle_response($to_client,
                            $response, $verb, $arg,
                            \$want_data, \$in_data);
            ($request, $verb, $arg, $response) = ('', '', '', '');
        }

        next unless can_read_something();

        if (can_read($from_client)) {
            last unless safeappend(\$partial_request, $from_client);
            if (is_entire_line($partial_request)) {
                $request = $partial_request;
                $partial_request = '';
            }
        }

        if (can_read($from_server)) {
            last unless safeappend(\$partial_response, $from_server);
            if (is_entire_response($partial_response)) {
                $response = $partial_response;
                $partial_response = '';
            }
        }
    }
}

sub wait_crashed { my ($wstat) = @_;
    # XXX untested
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
