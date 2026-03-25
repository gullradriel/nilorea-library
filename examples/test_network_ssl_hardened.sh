#!/bin/bash
#
# test_network_ssl_hardened.sh - Test script for ex_network_ssl_hardened
#
# Generates SSL certificates and launches two HTTPS server instances:
#   - Port 8888: using separate key + cert files
#   - Port 9999: using a combined PEM file (key+cert in one .pem)
# Both serve DATAS/ as root directory.
# Then runs curl tests against both, including hardening-specific tests
# (path traversal, security headers, TLS version enforcement), and cleans up.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

SSL_DIR="$SCRIPT_DIR/ssl_test_certs_hardened"
EX_BIN="$SCRIPT_DIR/ex_network_ssl_hardened"
LOG_LEVEL="LOG_ERR"
PIDS=()

cleanup() {
    echo "--- Cleaning up ---"
    for pid in "${PIDS[@]}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill -SIGUSR1 "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf "$SSL_DIR"
    echo "--- Cleanup done ---"
}

trap cleanup EXIT

# Check binary exists
if [ ! -x "$EX_BIN" ]; then
    echo "ERROR: $EX_BIN not found or not executable."
    echo "Build it first with: make examples"
    exit 1
fi

# Check DATAS directory
if [ ! -d "$SCRIPT_DIR/DATAS" ]; then
    echo "ERROR: DATAS/ directory not found in $SCRIPT_DIR"
    exit 1
fi

mkdir -p "$SSL_DIR"

echo "=== Generating SSL certificates ==="

# --- Instance 1 (port 8888): separate key and cert files ---
echo "Generating separate key + cert for port 8888..."
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$SSL_DIR/server.key" \
    -out "$SSL_DIR/server.crt" \
    -days 1 \
    -subj "/CN=localhost/O=NiloreaTest" \
    2>/dev/null

# --- Instance 2 (port 9999): combined PEM file ---
echo "Generating combined PEM for port 9999..."
openssl req -x509 -newkey rsa:2048 -nodes \
    -keyout "$SSL_DIR/combined_key.pem" \
    -out "$SSL_DIR/combined_cert.pem" \
    -days 1 \
    -subj "/CN=localhost/O=NiloreaTestPEM" \
    2>/dev/null

# Create a combined PEM (key + cert in one file)
cat "$SSL_DIR/combined_key.pem" "$SSL_DIR/combined_cert.pem" > "$SSL_DIR/combined.pem"

echo "=== Starting HTTPS servers ==="

# Instance 1: port 8888 with separate key/cert
echo "Starting server on port 8888 (separate key + cert)..."
"$EX_BIN" -p 8888 -k "$SSL_DIR/server.key" -c "$SSL_DIR/server.crt" -d "$SCRIPT_DIR/DATAS" -V "$LOG_LEVEL" &
PIDS+=($!)
echo "  PID: ${PIDS[-1]}"

# Instance 2: port 9999 with combined PEM
# The combined PEM contains both key and cert, so we pass it to both -k and -c
echo "Starting server on port 9999 (combined PEM)..."
"$EX_BIN" -p 9999 -k "$SSL_DIR/combined.pem" -c "$SSL_DIR/combined.pem" -d "$SCRIPT_DIR/DATAS" -V "$LOG_LEVEL" &
PIDS+=($!)
echo "  PID: ${PIDS[-1]}"

# Wait for servers to start
sleep 2

# Check servers are running
FAILED=0
for i in 0 1; do
    if ! kill -0 "${PIDS[$i]}" 2>/dev/null; then
        echo "ERROR: Server PID ${PIDS[$i]} is not running!"
        FAILED=1
    fi
done

if [ "$FAILED" -eq 1 ]; then
    echo "ERROR: One or more servers failed to start"
    exit 1
fi

echo ""
echo "=== Running tests ==="

TEST_PASS=0
TEST_FAIL=0

run_test() {
    local description="$1"
    local url="$2"
    local expected_code="$3"
    local extra_curl_flags="${4:-}"

    printf "  %-50s " "$description..."
    http_code=$(curl -sk $extra_curl_flags -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 "$url" 2>/dev/null || echo "000")

    if [ "$http_code" = "$expected_code" ]; then
        echo "PASS (HTTP $http_code)"
        TEST_PASS=$((TEST_PASS + 1))
    else
        echo "FAIL (expected $expected_code, got $http_code)"
        TEST_FAIL=$((TEST_FAIL + 1))
    fi
}

# Tests on port 8888 (separate key/cert)
echo "--- Port 8888 (separate key/cert) ---"
run_test "GET /index.html" "https://localhost:8888/index.html" "200"
run_test "GET / (root redirect)" "https://localhost:8888/" "301"
run_test "GET / (follow redirect)" "https://localhost:8888/" "200" "-L"
run_test "GET /nonexistent" "https://localhost:8888/nonexistent.html" "404"
run_test "GET /favicon.ico" "https://localhost:8888/favicon.ico" "200"

echo ""

# Tests on port 9999 (combined PEM)
echo "--- Port 9999 (combined PEM) ---"
run_test "GET /index.html" "https://localhost:9999/index.html" "200"
run_test "GET / (root redirect)" "https://localhost:9999/" "301"
run_test "GET / (follow redirect)" "https://localhost:9999/" "200" "-L"
run_test "GET /nonexistent" "https://localhost:9999/nonexistent.html" "404"
run_test "GET /favicon.ico" "https://localhost:9999/favicon.ico" "200"

echo ""

# Content verification test
echo "--- Content verification ---"
printf "  %-50s " "Verify content match between ports..."
content_8888=$(curl -sk --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null | md5sum)
content_9999=$(curl -sk --connect-timeout 5 --max-time 10 "https://localhost:9999/index.html" 2>/dev/null | md5sum)
if [ "$content_8888" = "$content_9999" ]; then
    echo "PASS (content matches)"
    TEST_PASS=$((TEST_PASS + 1))
else
    echo "FAIL (content differs)"
    TEST_FAIL=$((TEST_FAIL + 1))
fi

echo ""

# =====================================================================
# Hardening-specific tests
# =====================================================================
echo "=== Hardening-specific tests ==="

# --- Path traversal protection ---
echo "--- Path traversal protection ---"
run_test "GET /../../../etc/passwd (traversal)" "https://localhost:8888/../../../etc/passwd" "404"
run_test "GET /..%2f..%2fetc/passwd (encoded)" "https://localhost:8888/..%2f..%2fetc/passwd" "404"

echo ""

# --- Security headers ---
echo "--- Security headers (port 8888) ---"

check_header() {
    local description="$1"
    local header_name="$2"
    local expected_value="$3"

    printf "  %-50s " "$description..."
    header_value=$(curl -sk -D - -o /dev/null --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null | grep -i "^${header_name}:" | head -1 | tr -d '\r\n')

    if [ -z "$header_value" ]; then
        echo "FAIL (header not found)"
        TEST_FAIL=$((TEST_FAIL + 1))
    elif echo "$header_value" | grep -qi "$expected_value"; then
        echo "PASS ($header_value)"
        TEST_PASS=$((TEST_PASS + 1))
    else
        echo "FAIL (got: $header_value)"
        TEST_FAIL=$((TEST_FAIL + 1))
    fi
}

check_header "Strict-Transport-Security" "Strict-Transport-Security" "max-age="
check_header "X-Content-Type-Options" "X-Content-Type-Options" "nosniff"
check_header "X-Frame-Options" "X-Frame-Options" "DENY"
check_header "Content-Security-Policy" "Content-Security-Policy" "default-src"
check_header "Referrer-Policy" "Referrer-Policy" "strict-origin"

echo ""

# --- Server name not leaking implementation details ---
echo "--- Server name disclosure ---"
printf "  %-50s " "Server header is generic..."
server_header=$(curl -sk -D - -o /dev/null --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null | grep -i "^Server:" | head -1 | tr -d '\r\n')
if echo "$server_header" | grep -qi "ex_network_ssl"; then
    echo "FAIL (leaks implementation: $server_header)"
    TEST_FAIL=$((TEST_FAIL + 1))
else
    echo "PASS ($server_header)"
    TEST_PASS=$((TEST_PASS + 1))
fi

echo ""

# --- TLS version enforcement ---
echo "--- TLS version enforcement ---"

# TLS 1.2 should work
printf "  %-50s " "TLS 1.2 connection succeeds..."
tls12_code=$(curl -sk --tls-max 1.2 --tlsv1.2 -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null || echo "000")
if [ "$tls12_code" = "200" ]; then
    echo "PASS (HTTP $tls12_code)"
    TEST_PASS=$((TEST_PASS + 1))
else
    echo "FAIL (expected 200, got $tls12_code)"
    TEST_FAIL=$((TEST_FAIL + 1))
fi

# TLS 1.3 should work (if supported by curl/openssl)
printf "  %-50s " "TLS 1.3 connection succeeds (if available)..."
tls13_code=$(curl -sk --tlsv1.3 -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null || echo "000")
if [ "$tls13_code" = "200" ]; then
    echo "PASS (HTTP $tls13_code)"
    TEST_PASS=$((TEST_PASS + 1))
elif [ "$tls13_code" = "000" ]; then
    echo "SKIP (TLS 1.3 not supported by curl)"
else
    echo "FAIL (expected 200, got $tls13_code)"
    TEST_FAIL=$((TEST_FAIL + 1))
fi

# TLS 1.1 should be rejected
printf "  %-50s " "TLS 1.1 connection rejected..."
tls11_code=$(curl -sk --tls-max 1.1 --tlsv1.1 -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null) || true
if [ "$tls11_code" = "000" ] || [ -z "$tls11_code" ]; then
    echo "PASS (connection refused as expected)"
    TEST_PASS=$((TEST_PASS + 1))
else
    echo "FAIL (expected connection failure, got HTTP $tls11_code)"
    TEST_FAIL=$((TEST_FAIL + 1))
fi

# TLS 1.0 should be rejected
printf "  %-50s " "TLS 1.0 connection rejected..."
tls10_code=$(curl -sk --tls-max 1.0 --tlsv1.0 -o /dev/null -w "%{http_code}" --connect-timeout 5 --max-time 10 "https://localhost:8888/index.html" 2>/dev/null) || true
if [ "$tls10_code" = "000" ] || [ -z "$tls10_code" ]; then
    echo "PASS (connection refused as expected)"
    TEST_PASS=$((TEST_PASS + 1))
else
    echo "FAIL (expected connection failure, got HTTP $tls10_code)"
    TEST_FAIL=$((TEST_FAIL + 1))
fi

echo ""
echo "=== Results: $TEST_PASS passed, $TEST_FAIL failed ==="

if [ "$TEST_FAIL" -gt 0 ]; then
    exit 1
fi

exit 0
