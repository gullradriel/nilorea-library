#!/bin/bash
#
# Sanitizer check script for all non-GUI nilorea-library examples.
#
# Runs each example binary (compiled with DEBUG=1 for ASan/LSan, or with
# DEBUG=1 DEBUG_THREADS=1 for ThreadSanitizer) and checks stderr for
# AddressSanitizer / LeakSanitizer / ThreadSanitizer reports.  This
# replaces the previous valgrind-based approach, ASan/LSan catches the
# same classes of bugs at ~2x overhead instead of 20-50x, which is
# critical for network tests that previously timed out under valgrind.
#
# NOTE: binaries MUST be compiled with `make asan` (DEBUG=1) or
#       `make tsan` (DEBUG=1 DEBUG_THREADS=1) for this to work. Without
#       sanitizers baked in, the script still runs the tests but only
#       checks the exit code (no sanitizer error detection).
#
# Usage: ./run_tests.sh [--timeout SECONDS]
#   --timeout SECONDS : per-test timeout (default: 60)
#
# Exit code: highest return code from any tested example.
#

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./

# Verify binaries were compiled with sanitizer instrumentation.
# ASan exports __asan_*, TSAN exports __tsan_*, UBSan exports __ubsan_*.
# Match any of those undefined symbols on a core example binary.
if [ -f ./ex_common ]; then
    if ! nm -a ./ex_common 2>/dev/null | grep -qE "__(asan|tsan|ubsan|lsan)_" && \
       ! strings ./ex_common 2>/dev/null | grep -qiE "(Address|Thread|Leak|UndefinedBehavior)Sanitizer"; then
        echo "ERROR: binaries were not compiled with sanitizer instrumentation."
        echo "Please run 'make asan' (DEBUG=1) or 'make tsan' (DEBUG=1 DEBUG_THREADS=1) before running tests."
        exit 1
    fi
else
    echo "ERROR: ex_common not found. Please build examples first with 'make asan' or 'make tsan'."
    exit 1
fi

# LSan suppressions for GPU/display driver leaks (Mesa, DRM, X11)
if [ -f lsan-suppressions.cfg ]; then
    export LSAN_OPTIONS="suppressions=lsan-suppressions.cfg"
fi

TIMEOUT=60

while [ $# -gt 0 ]; do
    case "$1" in
        --timeout)
            TIMEOUT="$2"
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

most_critical=0

LOCKFILE="$(basename $0).lock"
LOCKTIMEOUT=60

# Create the lockfile.
touch $LOCKFILE

function asan_test {
    proc=$1
    params=$2
    procfile=${proc}_applog$3.txt
    sanitizer_log=${proc}_debugmem$3.txt

    PROC="./$proc $params -V LOG_DEBUG"
    echo "$PROC"
    if command -v timeout &>/dev/null; then
        PROC_OUTPUT=$(timeout ${TIMEOUT}s bash -c "$PROC" 2>"$sanitizer_log")
    else
        PROC_OUTPUT=$(bash -c "$PROC" 2>"$sanitizer_log")
    fi
    PROC_RET=$?

    # 124 is the timeout exit code
    if [ $PROC_RET -eq 124 ]; then
        echo "WARNING: $proc timed out after ${TIMEOUT}s"
    fi

    # Create a file descriptor over the given lockfile.
    exec {FD}<>$LOCKFILE
    if ! flock -x -w $LOCKTIMEOUT $FD; then
        echo "Failed to obtain a lock within $LOCKTIMEOUT seconds"
        echo "Another instance of $(basename $0) is probably running."
        exit 1
    fi

    echo "Running $proc $params returned $PROC_RET"
    echo "$PROC_OUTPUT" > "$procfile"
    if [ $PROC_RET -ne 0 ]; then
        echo "AppliLog:"
        cat "$procfile"
    fi
    if [ $PROC_RET -gt $most_critical ]; then
        most_critical=$PROC_RET
        echo "ERROR $proc returned $PROC_RET"
    fi

    # Check for ASan/LSan/TSan errors in stderr. ThreadSanitizer reports
    # races as "WARNING: ThreadSanitizer:" rather than "ERROR:", so match
    # both. The SUMMARY line is the canonical "something happened" marker
    # for all three sanitizers.
    SANITIZER_ERRORS=0
    if grep -qE "(ERROR|WARNING): (Address|Leak|Thread)Sanitizer" "$sanitizer_log" 2>/dev/null; then
        SANITIZER_ERRORS=1
    fi
    if grep -qE "SUMMARY: (Address|Leak|Thread)Sanitizer" "$sanitizer_log" 2>/dev/null; then
        SANITIZER_ERRORS=1
    fi

    if [ $SANITIZER_ERRORS -ne 0 ]; then
        echo "Memory test KO for $proc!"
        echo "AppliLog:"
        cat "$procfile"
        echo "SanitizerLog:"
        cat "$sanitizer_log"
        if [ $most_critical -eq 0 ]; then
            most_critical=1
        fi
    else
        echo "Memory test OK for $proc."
        rm -f "$sanitizer_log"
    fi

    if ! flock -u $FD; then
        echo "$(basename $0): failed to unlock $FD"
        exit 1
    fi

    echo "##########"

    return $PROC_RET
}

# starting, printing first text separator
echo "##########"

# core examples
asan_test "ex_common"
asan_test "ex_log"
asan_test "ex_exceptions"

# data structure examples
asan_test "ex_list"
asan_test "ex_hash"
asan_test "ex_nstr"
asan_test "ex_stack"
asan_test "ex_trees"
asan_test "ex_iso_astar"

# crypto / encoding
asan_test "ex_base64_encode" "../README.md"
asan_test "ex_crypto" "-i ex_common"

# compression
asan_test "ex_zlib"

# threading
asan_test "ex_threads"

# pcre (conditional, needs HAVE_PCRE)
if [ -f ./ex_pcre ]; then
    asan_test "ex_pcre" '"TEST(.*)TEST" "TESTazerty1234TEST"'
fi

# config file (conditional, needs HAVE_PCRE)
if [ -f ./ex_configfile ]; then
    asan_test "ex_configfile" "CONFIGS/ex_configfile.cfg"
fi

# file operations
asan_test "ex_file"

# avro (conditional, needs HAVE_CJSON)
if [ -f ./ex_avro ]; then
    asan_test "ex_avro"
fi

# git (conditional, needs HAVE_LIBGIT2)
if [ -f ./ex_git ]; then
    asan_test "ex_git"
fi

# n_gui kvtable layout regression (conditional, needs HAVE_ALLEGRO).
# Runs headless (no display) using al_create_builtin_font. Asserts that
# the "+" button stays below row widgets across resize and clear+add
# cycles, and that new rows match the scaled height of older rows.
if [ -f ./ex_gui_kvtable ]; then
    asan_test "ex_gui_kvtable"
fi

# ex_signals intentionally crashes (stack overflow, segfault, etc.) - skip

# ---- Network tests ----
# Pattern: client runs in background, server runs via asan_test (foreground).
# Both sides get full ASan/LSan coverage. Servers self-terminate via -n.
# Disable proxy for localhost network tests (CI runners may have http_proxy set)
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY no_proxy NO_PROXY
# A helper waits for a background PID with a timeout.
wait_or_kill() {
    local pid=$1
    local max_wait=${2:-15}
    local waited=0
    while kill -0 $pid 2>/dev/null && [ $waited -lt $max_wait ]; do
        sleep 1
        waited=$((waited + 1))
    done
    if kill -0 $pid 2>/dev/null; then
        kill $pid 2>/dev/null
        wait $pid 2>/dev/null
    fi
}

# Proxy URL parser test (no live proxy needed)
asan_test "ex_network_proxy"

# Mock HTTP server test (plain HTTP, no SSL needed)
asan_test "ex_network_mock"

echo "#### NETWORK TESTING ####"
NETPORT=6666
for P in 6666 6667 6668 6669 6670; do
    if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
       ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
        NETPORT=$P
        break
    fi
done

# client starts after 1s delay, in background
(sleep 1 && ./ex_network -s localhost -p $NETPORT -n 1 -V LOG_ERR 2>/dev/null) &
CLIENT_PID=$!
# server: 1 connection, tested via asan_test
asan_test "ex_network" "-a localhost -p $NETPORT -n 1 -V LOG_ERR"
wait_or_kill $CLIENT_PID 10

# SSL network tests (conditional, needs HAVE_OPENSSL)
if [ -f ./ex_network_ssl ] || [ -f ./ex_network_ssl_hardened ]; then
    echo "#### SSL NETWORK TESTING ####"
    SSL_DIR="$(pwd)/test_ssl_certs"
    mkdir -p "$SSL_DIR"

    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$SSL_DIR/server.key" \
        -out "$SSL_DIR/server.crt" \
        -days 1 \
        -subj "/CN=localhost/O=NiloreaTest" \
        2>/dev/null

    # Determine SSL client command: prefer curl, fall back to openssl s_client
    # (openssl is guaranteed available since we just used it for cert generation)
    ssl_client_cmd() {
        local port=$1
        if command -v curl &>/dev/null; then
            echo "SSL_CLIENT: using curl to connect to localhost:$port"
            curl -sk -o /dev/null --connect-timeout 5 --max-time 10 "https://localhost:$port/index.html" 2>&1
        else
            echo "SSL_CLIENT: curl not found, using openssl s_client to connect to localhost:$port"
            echo -e "GET /index.html HTTP/1.0\r\nHost: localhost\r\n\r\n" | \
                openssl s_client -connect localhost:$port -quiet 2>&1
        fi
        echo "SSL_CLIENT: exit code: $?"
    }

    if [ -f ./ex_network_ssl ]; then
        SSLPORT=8876
        for P in 8876 8877 8878 8879 8880; do
            if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
               ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
                SSLPORT=$P
                break
            fi
        done

        # SSL client after 2s delay, in background
        (sleep 2 && ssl_client_cmd $SSLPORT) &
        SSL_CLIENT_PID=$!
        # server: 1 connection, tested via asan_test
        asan_test "ex_network_ssl" "-p $SSLPORT -n 1 -k $SSL_DIR/server.key -c $SSL_DIR/server.crt -d $(pwd)/DATAS -V LOG_ERR"
        wait_or_kill $SSL_CLIENT_PID 10
    fi

    if [ -f ./ex_network_ssl_hardened ]; then
        SSLHPORT=8886
        for P in 8886 8887 8888 8889 8890; do
            if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
               ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
                SSLHPORT=$P
                break
            fi
        done

        # SSL client after 2s delay, in background
        (sleep 2 && ssl_client_cmd $SSLHPORT) &
        SSL_CLIENT_PID=$!
        # server: 1 connection, tested via asan_test
        asan_test "ex_network_ssl_hardened" "-p $SSLHPORT -n 1 -k $SSL_DIR/server.key -c $SSL_DIR/server.crt -d $(pwd)/DATAS -V LOG_ERR"
        wait_or_kill $SSL_CLIENT_PID 10
    fi

    # ex_network_ws and ex_network_sse connect to public Internet hosts
    # (echo.websocket.org, sse.dev). The GitLab CI runners have no Internet
    # access, so skip these tests there. Honor SKIP_INTERNET_TESTS=1 as an
    # explicit override for any other offline environment.
    if [ "${GITLAB_CI:-}" = "true" ] || [ "${SKIP_INTERNET_TESTS:-}" = "1" ]; then
        echo "#### SKIPPING WEBSOCKET / SSE TESTS (no Internet access) ####"
    else
        if [ -f ./ex_network_ws ]; then
            echo "#### WEBSOCKET TESTING ####"
            asan_test "ex_network_ws"
        fi

        if [ -f ./ex_network_sse ]; then
            echo "#### SSE TESTING ####"
            asan_test "ex_network_sse"
        fi
    fi

    rm -rf "$SSL_DIR"
fi

# Reactor (epoll) network test: server uses n_reactor + netw_accept_into_reactor,
# client uses the standard thread engine. Linux/Android only, on other
# platforms the server exits 0 with a "skipped" notice.
if [ -f ./ex_network_reactor ]; then
    echo "#### REACTOR (epoll) NETWORK TESTING ####"
    REPORT=6671
    for P in 6671 6672 6673 6674 6675; do
        if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
           ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
            REPORT=$P
            break
        fi
    done

    # client starts after 1s delay, in background; sends 3 sequential
    # connections so the server sees 3 echo round-trips
    (sleep 1 && ./ex_network_reactor -s localhost -p $REPORT -n 3 -V LOG_ERR 2>/dev/null) &
    REACTOR_CLIENT_PID=$!
    # server: handle 3 connections then exit cleanly
    asan_test "ex_network_reactor" "-a \"\" -p $REPORT -n 3 -V LOG_NOTICE"
    wait_or_kill $REACTOR_CLIENT_PID 15
fi

# Accept pool tests, exercise all three -m modes (single-inline,
# single-pool, pooled) so any regression in one path is visible
# independently of the others.
echo "#### ACCEPT POOL TESTING ####"
APPORT=7776
for P in 7776 7777 7778 7779 7780; do
    if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
       ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
        APPORT=$P
        break
    fi
done

# single-inline: 2 conns, 1 accept thread doing accept+handle inline.
(sleep 1 && ./ex_accept_pool_client -s localhost -p $APPORT -n 2 -t 1 -V LOG_NOTICE 2>/dev/null) &
AP_CLIENT_PID=$!
asan_test "ex_accept_pool_server" "-p $APPORT -m single-inline -n 2 -V LOG_NOTICE" "_inline"
wait_or_kill $AP_CLIENT_PID 10

# single-pool: 2 conns, 1 accept thread + 2-thread worker pool.
APPORT_SP=$APPORT
for P in 7781 7782 7783 7784 7785; do
    if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
       ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
        APPORT_SP=$P
        break
    fi
done
(sleep 1 && ./ex_accept_pool_client -s localhost -p $APPORT_SP -n 2 -t 1 -V LOG_NOTICE 2>/dev/null) &
AP_CLIENT_PID=$!
asan_test "ex_accept_pool_server" "-p $APPORT_SP -m single-pool -n 2 -t 2 -V LOG_NOTICE" "_pool"
wait_or_kill $AP_CLIENT_PID 10

# pooled: 2 conns, 2 accept threads + 2-thread worker pool.
APPORT_P=$APPORT_SP
for P in 7786 7787 7788 7789 7790; do
    if ! ss -tlnp 2>/dev/null | grep -q ":${P} " && \
       ! netstat -tlnp 2>/dev/null | grep -q ":${P} "; then
        APPORT_P=$P
        break
    fi
done
(sleep 1 && ./ex_accept_pool_client -s localhost -p $APPORT_P -n 2 -t 2 -V LOG_NOTICE 2>/dev/null) &
AP_CLIENT_PID=$!
asan_test "ex_accept_pool_server" "-p $APPORT_P -m pooled -n 2 -t 2 -V LOG_NOTICE" "_pooled"
wait_or_kill $AP_CLIENT_PID 10

rm -f $LOCKFILE
rm -f ex_*_applog*.txt

exit $most_critical
