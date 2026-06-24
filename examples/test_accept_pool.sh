#!/bin/bash
#
# Stress test script for the accept pool
# Compares three modes of accept + client handling:
#   1. single-inline: 1 thread doing accept + handle (fully serialized)
#   2. single-pool:   1 thread doing accept, thread pool for handling
#   3. pooled:        N accept threads + thread pool for handling
#
# Usage: ./test_accept_pool.sh [PORT] [COUNT] [COUNT_HEAVY]
#

set -e

# pick a random port in high range to avoid collisions with leftover sockets
PORT="${1:-$((30000 + RANDOM % 10000))}"
COUNT="${2:-500}"
# tests 2 & 3 use higher count + more client threads to stress the accept path
COUNT_HEAVY="${3:-2000}"
POOL_THREADS=16
CLIENT_THREADS=16
CLIENT_THREADS_HEAVY=64
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

SERVER_BIN="${SCRIPT_DIR}/ex_accept_pool_server"
CLIENT_BIN="${SCRIPT_DIR}/ex_accept_pool_client"

if [ ! -x "$SERVER_BIN" ]; then
    echo "Error: $SERVER_BIN not found. Run 'make examples' first."
    exit 1
fi
if [ ! -x "$CLIENT_BIN" ]; then
    echo "Error: $CLIENT_BIN not found. Run 'make examples' first."
    exit 1
fi

cleanup() {
    echo ""
    echo "Cleaning up..."
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null || true
        wait "$SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# check a port is free
check_port_free() {
    if ss -tln | grep -q ":${1} "; then
        echo "Error: port $1 is already in use. Pick a different port or wait."
        ss -tlnp | grep ":${1} "
        exit 1
    fi
}

# wait for server to finish, kill if stuck
wait_for_server() {
    local wait_count=0
    while kill -0 "$SERVER_PID" 2>/dev/null && [ "$wait_count" -lt 30 ]; do
        sleep 1
        wait_count=$((wait_count + 1))
    done
    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo "Server still running after ${wait_count}s, killing..."
        kill "$SERVER_PID" 2>/dev/null || true
    fi
    wait "$SERVER_PID" 2>/dev/null || true
    SERVER_PID=""
}

# allocate 3 ports
PORT1=$PORT
PORT2=$((PORT + 1))
PORT3=$((PORT + 2))

check_port_free "$PORT1"
check_port_free "$PORT2"
check_port_free "$PORT3"

echo "============================================="
echo " Accept Pool Stress Test"
echo " Ports: $PORT1 / $PORT2 / $PORT3"
echo " Test 1: $COUNT connections, $CLIENT_THREADS client threads"
echo " Test 2-3: $COUNT_HEAVY connections, $CLIENT_THREADS_HEAVY client threads"
echo " Server threads: $POOL_THREADS"
echo "============================================="
echo ""

# --- Test 1: single-inline ---
echo ">>> TEST 1: single-inline ($COUNT connections)"
echo "    1 thread: accept + handle in same thread (fully serialized)"
echo "Starting server..."
"$SERVER_BIN" -p "$PORT1" -m single-inline -n "$COUNT" -V LOG_NOTICE &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Error: server failed to start"
    exit 1
fi

echo "Starting client..."
"$CLIENT_BIN" -s 127.0.0.1 -p "$PORT1" -n "$COUNT" -t "$CLIENT_THREADS" -e -V LOG_NOTICE
CLIENT_EXIT=$?

wait_for_server
echo ""

if [ "$CLIENT_EXIT" -ne 0 ]; then
    echo "Warning: client exited with code $CLIENT_EXIT"
fi

sleep 2

# --- Test 2: single-pool ---
echo ">>> TEST 2: single-pool ($COUNT_HEAVY connections, $POOL_THREADS worker threads, $CLIENT_THREADS_HEAVY client threads)"
echo "    1 accept thread, dispatching to thread pool"
echo "Starting server..."
"$SERVER_BIN" -p "$PORT2" -m single-pool -n "$COUNT_HEAVY" -t "$POOL_THREADS" -V LOG_NOTICE &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Error: server failed to start"
    exit 1
fi

echo "Starting client..."
"$CLIENT_BIN" -s 127.0.0.1 -p "$PORT2" -n "$COUNT_HEAVY" -t "$CLIENT_THREADS_HEAVY" -e -V LOG_NOTICE
CLIENT_EXIT=$?

wait_for_server
echo ""

if [ "$CLIENT_EXIT" -ne 0 ]; then
    echo "Warning: client exited with code $CLIENT_EXIT"
fi

sleep 2

# --- Test 3: pooled ---
echo ">>> TEST 3: pooled ($COUNT_HEAVY connections, $POOL_THREADS accept+worker threads, $CLIENT_THREADS_HEAVY client threads)"
echo "    N accept threads + thread pool (fully parallelized)"
echo "Starting server..."
"$SERVER_BIN" -p "$PORT3" -m pooled -n "$COUNT_HEAVY" -t "$POOL_THREADS" -V LOG_NOTICE &
SERVER_PID=$!
sleep 1

if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Error: server failed to start"
    exit 1
fi

echo "Starting client..."
"$CLIENT_BIN" -s 127.0.0.1 -p "$PORT3" -n "$COUNT_HEAVY" -t "$CLIENT_THREADS_HEAVY" -e -V LOG_NOTICE
CLIENT_EXIT=$?

# wait for server to finish
wait "$SERVER_PID" 2>/dev/null || true
SERVER_PID=""
echo ""

if [ "$CLIENT_EXIT" -ne 0 ]; then
    echo "Warning: client exited with code $CLIENT_EXIT"
fi

echo "============================================="
echo " Test complete. Compare the conn/sec values"
echo " above to see the performance difference."
echo ""
echo " Expected: single-inline << single-pool < pooled"
echo "============================================="
