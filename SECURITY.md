# Security Policy

## Supported Versions

Only the latest version on the `main` branch is actively supported with security fixes. If you are using an older checkout or fork, please update before reporting.

## Reporting a Vulnerability

**Please do not open a public issue for security vulnerabilities.**

Instead, use [GitHub Security Advisories](https://github.com/gullradriel/nilorea-library/security/advisories/new) to report vulnerabilities privately. This allows coordinated disclosure and keeps details confidential until a fix is available.

When reporting, please include:

- A description of the vulnerability and its potential impact
- Steps to reproduce or a minimal proof of concept
- The affected module(s) (e.g., `n_network`, `n_crypto`, `n_str`)
- Your suggested severity (critical, high, moderate, low)

## Scope

As a C library, the following types of issues are especially relevant:

- Memory safety issues (buffer overflows, use-after-free, double-free)
- Integer overflows leading to exploitable behavior
- Vulnerabilities in networking modules (TCP/UDP, SSL/TLS handling)
- Weaknesses in cryptographic or encoding routines
- Injection or parsing flaws in configuration or input handling

General bugs that do not have a security impact should be reported as regular [GitHub issues](https://github.com/gullradriel/nilorea-library/issues).

## Response Timeline

This library is maintained on a volunteer basis — nobody is paid to work on it. Responses are provided on a best-effort basis:

- **Acknowledgment**: as soon as we can review the report
- **Assessment and fix**: timelines depend on severity and maintainer availability. Including a fix or a pull request with your report will significantly speed things up

Reporters will be credited in the fix commit unless they prefer to remain anonymous.
