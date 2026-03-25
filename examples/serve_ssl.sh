#!/bin/bash
#
# serve_ssl.sh - Persistent HTTPS server using ex_network_ssl
#
# Serves DATAS/ over HTTPS with auto-restart on crash.
# Generates self-signed certificates on first run.
#
# Usage: ./serve_ssl.sh [-p port] [-d root_dir] [-V log_level]
#   -p  port        (default: 8443)
#   -d  root dir    (default: ./DATAS)
#   -V  log level   (default: LOG_ERR)
#

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

EX_BIN="$SCRIPT_DIR/ex_network_ssl"
SSL_DIR="$SCRIPT_DIR/.ssl_serve"
PORT="8443"
ROOT_DIR="$SCRIPT_DIR/DATAS"
LOG_LEVEL="LOG_ERR"
RESTART_DELAY=2

while getopts "p:d:V:h" opt; do
    case "$opt" in
        p) PORT="$OPTARG" ;;
        d) ROOT_DIR="$OPTARG" ;;
        V) LOG_LEVEL="$OPTARG" ;;
        h)
            echo "Usage: $0 [-p port] [-d root_dir] [-V log_level]"
            echo "  -p  port        (default: 8443)"
            echo "  -d  root dir    (default: ./DATAS)"
            echo "  -V  log level   (default: LOG_ERR)"
            exit 0
            ;;
        *) exit 1 ;;
    esac
done

if [ ! -x "$EX_BIN" ]; then
    echo "ERROR: $EX_BIN not found or not executable."
    echo "Build it first with: make examples"
    exit 1
fi

if [ ! -d "$ROOT_DIR" ]; then
    echo "ERROR: root directory $ROOT_DIR not found"
    exit 1
fi

# Generate self-signed certificate if missing
generate_certs() {
    if [ -f "$SSL_DIR/server.key" ] && [ -f "$SSL_DIR/server.crt" ]; then
        return
    fi
    echo "[serve_ssl] Generating self-signed certificate..."
    mkdir -p "$SSL_DIR"
    openssl req -x509 -newkey rsa:2048 -nodes \
        -keyout "$SSL_DIR/server.key" \
        -out "$SSL_DIR/server.crt" \
        -days 365 \
        -subj "/CN=localhost/O=NiloreaServe" \
        2>/dev/null
    echo "[serve_ssl] Certificate generated in $SSL_DIR/"
}

cleanup() {
    echo ""
    echo "[serve_ssl] Shutting down..."
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -SIGUSR1 "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
    echo "[serve_ssl] Stopped."
    exit 0
}

trap cleanup SIGINT SIGTERM SIGHUP

generate_certs

echo "[serve_ssl] Serving $ROOT_DIR on https://localhost:$PORT/"
echo "[serve_ssl] Press Ctrl+C to stop"
echo ""

while true; do
    "$EX_BIN" \
        -p "$PORT" \
        -k "$SSL_DIR/server.key" \
        -c "$SSL_DIR/server.crt" \
        -d "$ROOT_DIR" \
        -V "$LOG_LEVEL" &
    SERVER_PID=$!

    wait "$SERVER_PID" 2>/dev/null
    EXIT_CODE=$?

    # If we caught a signal during wait, cleanup handles it
    if ! kill -0 "$SERVER_PID" 2>/dev/null; then
        if [ "$EXIT_CODE" -eq 0 ]; then
            echo "[serve_ssl] Server exited cleanly."
            break
        fi
        echo "[serve_ssl] Server crashed (exit code $EXIT_CODE), restarting in ${RESTART_DELAY}s..."
        sleep "$RESTART_DELAY"
    fi
done
