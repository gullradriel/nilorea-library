# SSL Security Audit: ex_network_ssl.c

Security analysis of `examples/ex_network_ssl.c` and the underlying `n_network.c` SSL implementation.

## Critical Issues

### 1. No TLS Protocol Version Pinning (Library Level)
**File:** `src/n_network.c:1232`
**Severity:** HIGH

`netw_set_crypto()` uses `TLS_method()` which allows _all_ TLS versions including deprecated TLS 1.0 and 1.1. No call to `SSL_CTX_set_min_proto_version()` is made.

**Impact:** Vulnerable to protocol downgrade attacks. Clients can negotiate TLS 1.0/1.1, which have known weaknesses (BEAST, POODLE).

**Fix:** Set minimum protocol version to TLS 1.2:
```c
SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);
```

### 2. No Cipher Suite Restriction
**File:** `src/n_network.c:1236`
**Severity:** HIGH

No call to `SSL_CTX_set_cipher_list()` or `SSL_CTX_set_ciphersuites()`. OpenSSL defaults include weak ciphers (NULL, RC4, DES, export-grade).

**Impact:** Weak cipher negotiation allows eavesdropping or man-in-the-middle attacks.

**Fix:** Restrict to strong ciphers:
```c
SSL_CTX_set_cipher_list(ctx, "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:!aNULL:!MD5:!DSS");
SSL_CTX_set_ciphersuites(ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256");
```

### 3. No SSL_CTX_set_options() Hardening
**File:** `src/n_network.c:1236`
**Severity:** HIGH

No SSL context options are set. Missing protections:
- `SSL_OP_NO_SSLv2` / `SSL_OP_NO_SSLv3` — disable broken protocols
- `SSL_OP_NO_COMPRESSION` — prevents CRIME attack
- `SSL_OP_CIPHER_SERVER_PREFERENCE` — server chooses cipher order
- `SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION`
- `SSL_OP_NO_TICKET` — disable stateless session tickets to force server-side state

**Impact:** Compression oracle attacks, client-chosen weak ciphers, session resumption abuse.

### 4. No DH/ECDH Parameter Configuration
**File:** `src/n_network.c`
**Severity:** MEDIUM

No Diffie-Hellman parameters or ECDH curve selection. Without explicit configuration, forward secrecy may use weak defaults or fail entirely on older OpenSSL.

**Fix:**
```c
SSL_CTX_set_ecdh_auto(ctx, 1); // OpenSSL < 1.1.0
// or for 1.1.0+, auto mode is default but explicit curve selection is better:
EC_KEY *ecdh = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
SSL_CTX_set_tmp_ecdh(ctx, ecdh);
EC_KEY_free(ecdh);
```

## Application-Level Issues

### 5. Path Traversal Vulnerability
**File:** `examples/ex_network_ssl.c:280-286`
**Severity:** CRITICAL

The URL from the HTTP request is concatenated directly into a filesystem path:
```c
snprintf(system_url, sizeof(system_url), "./DATAS%s", http_url);
```
An attacker can send `GET /../../etc/passwd` to read arbitrary files outside the document root.

**Fix:** Sanitize the URL by resolving `realpath()` and verifying it stays within the root directory.

### 6. Stack-based Buffer via Alloca with User-Controlled Size
**File:** `examples/ex_network_ssl.c:233`
**Severity:** HIGH

```c
Alloca(http_buffer, (size_t)(max_http_request_size + 1));
```
`max_http_request_size` is set via `-s` command-line argument using `atoi()` (line 121) with no upper bound validation. While this is an admin-controlled value (not attacker-controlled at runtime), `alloca()` on the stack with large values can cause stack overflow.

**Fix:** Use heap allocation (`Malloc`) and enforce a sane maximum.

### 7. No HTTP Request Timeout
**File:** `examples/ex_network_ssl.c:239`
**Severity:** MEDIUM

`SSL_read()` blocks indefinitely waiting for data. A slow-loris style attacker can hold connections open, exhausting the thread pool.

**Impact:** Denial of service by connection exhaustion.

**Fix:** Set `SO_RCVTIMEO` on the socket or use `select()`/`poll()` with a timeout before reading.

### 8. No Connection Rate Limiting
**File:** `examples/ex_network_ssl.c:454-476`
**Severity:** MEDIUM

The accept loop has no rate limiting. An attacker can flood the server with connections, filling the thread pool queue.

**Fix:** Implement per-IP connection tracking or a simple accept rate limiter.

### 9. Server Version Disclosure
**File:** `examples/ex_network_ssl.c:267,275,296,301,...`
**Severity:** LOW

HTTP responses include `Server: ex_network_ssl server` which reveals the server software identity.

**Fix:** Use a generic or empty server name string.

### 10. Missing Security Headers
**File:** `examples/ex_network_ssl.c` (all responses)
**Severity:** LOW-MEDIUM

No security headers are sent in HTTP responses:
- `Strict-Transport-Security` (HSTS) — prevents downgrade to HTTP
- `X-Content-Type-Options: nosniff` — prevents MIME sniffing
- `X-Frame-Options: DENY` — prevents clickjacking
- `Content-Security-Policy` — XSS mitigation

### 11. No SSL Session Cache Configuration
**File:** `src/n_network.c`
**Severity:** LOW

SSL session caching is left at OpenSSL defaults. For a server, explicit session cache configuration and timeout controls improve both performance and security.

### 12. No OCSP Stapling
**File:** `src/n_network.c`
**Severity:** LOW

No OCSP stapling support. Clients cannot efficiently verify certificate revocation status.

### 13. HTTP Request Size Validated Only at Read, Not Parse
**File:** `examples/ex_network_ssl.c:253`
**Severity:** LOW

The URL buffer is a fixed 4096 bytes, while `max_http_request_size` defaults to 16384. A very long URL in a valid-sized request could potentially cause issues in URL parsing, though `snprintf` prevents overflow.

### 14. No SIGPIPE Handling on SSL_write
**File:** `examples/ex_network_ssl.c:418`
**Severity:** LOW (mitigated)

`SIGPIPE` is ignored via `signal(SIGPIPE, SIG_IGN)` which is correct, but only on non-Windows. This is already handled.

## Summary Table

| # | Issue | Severity | Category |
|---|-------|----------|----------|
| 1 | No TLS version pinning | HIGH | TLS Config |
| 2 | No cipher suite restriction | HIGH | TLS Config |
| 3 | No SSL context options | HIGH | TLS Config |
| 4 | No DH/ECDH parameters | MEDIUM | TLS Config |
| 5 | Path traversal | CRITICAL | Application |
| 6 | Stack alloca with unchecked size | HIGH | Application |
| 7 | No read timeout (slow-loris) | MEDIUM | DoS |
| 8 | No connection rate limiting | MEDIUM | DoS |
| 9 | Server version disclosure | LOW | Information Leak |
| 10 | Missing security headers | LOW-MEDIUM | HTTP Hardening |
| 11 | No SSL session cache config | LOW | TLS Config |
| 12 | No OCSP stapling | LOW | TLS Config |
| 13 | URL buffer vs request size | LOW | Input Validation |
