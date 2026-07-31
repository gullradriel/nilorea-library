# Changelog

All notable changes to the Nilorea library are recorded here. The format is
based on [Keep a Changelog](https://keepachangelog.com/). This project does not
currently tag semantic versions, so entries accumulate under **Unreleased**.

## [Unreleased]

### Added

- **n_diff: line-oriented text comparison.** `n_diff_lines()` produces the
  shortest edit script turning one text into another using Myers' greedy
  algorithm with a stored trace, so inserting a single line no longer reports
  every following line as changed the way an index-aligned comparison does.
  `n_diff_to_unified()` renders the script with `' '` / `'-'` / `'+'` markers,
  either in full or cut into `@@` hunks around a given amount of context.
  Matching leading and trailing lines are trimmed before the search, which
  keeps the common case (two versions of the same document) far below the
  `max_edits` ceiling; past that ceiling the differing region is reported as
  one delete run plus one insert run and `N_DIFF_RESULT.truncated` says so,
  rather than silently returning a poor diff. `examples/ex_diff.c` covers it.
- **n_clipboard: Win32 clipboard backend for Windows / MSYS2 MinGW64.** Windows
  previously fell through to the "unavailable" stub; `N_CLIPBOARD_CLIPBOARD` now
  maps to the Win32 system clipboard via `CF_UNICODETEXT` (UTF-8 <-> UTF-16
  conversion), with an `OpenClipboard` retry and a process-local mutex. Windows
  has no X11-style PRIMARY selection, so `N_CLIPBOARD_PRIMARY` is a graceful
  no-op there (set does nothing, get returns `NULL`). The Windows link line
  gains `-luser32`. The Linux/X11 backend and the fallback stub are unchanged.
- **n_gui: opt-in redraw signal** `n_gui_needs_redraw()` / `n_gui_mark_dirty()`,
  letting an event-driven host skip `n_gui_draw` + `al_flip_display` while the
  GUI is idle and not animating (a 60-button window drew once over 2000 idle
  frames). Purely additive; hosts that redraw every frame are unaffected.
  `examples/ex_gui.c` demonstrates the pattern.
- **examples/ex_gui_bench.c**: headless-friendly GUI performance benchmark
  (draw ms/frame and event us/event), reproducible under Xvfb.

### Changed

- **License: relicensed from GPL-3.0-or-later to Apache-2.0.** `LICENSE` now
  carries the full Apache License, Version 2.0 text, a `NOTICE` file was added,
  and every first-party source file and shell script carries the Apache short
  header with `SPDX-License-Identifier: Apache-2.0`. Releases and commits made
  before this change remain available under the GNU General Public License
  v3.0 or later; that grant is not withdrawn, and anyone already relying on it
  may continue to do so. Vendored third-party code under `external/` is
  unaffected and keeps its own upstream license, including the
  GPL-2.0-or-later `external/lz4/programs/`, `external/lz4/examples/` and
  `external/lz4/tests/` directories, which are redistributed as part of the
  upstream import but are not compiled into or linked against this library.
  See `NOTICE` for the per-component breakdown.
- **n_gui: major draw-path performance work** on the reference heavy layout,
  benchmark draw time **15.4 -> 4.66 ms/frame (~70%, 63 -> 214 fps-equivalent)**:
  batch item text with `al_hold_bitmap_drawing` (two-pass background/text) in the
  scrolling and dropdown widgets; memoize text measurement (`_text_w` /
  `_text_dims`); cache the syntax view's line count / headers-end; coalesce the
  text area's per-glyph draws into per-run draws; and gate the per-motion
  tooltip / datagrid cursor scans on a pointer-over-window test (~3.7x faster
  background motion events at scale). Rendering is pixel-identical; the library
  stays ASan/LSan/UBSan/TSan clean and cppcheck + scan-build clean.

### Fixed

- **n_network / examples: `SSL_get1_peer_certificate()` no longer breaks the
  build on OpenSSL 1.x.** OpenSSL 3.0 renamed `SSL_get_peer_certificate()` to
  `SSL_get1_peer_certificate()` and deprecated the old spelling, so the tree had
  one of each: `examples/ex_network_sni.c` called the 3.0 name (undeclared on
  1.0.2/1.1.x, which is what broke the RHEL 7 example build) while
  `netw_ssl_get_verify_result()` called the 1.x name (deprecated on 3.0).
  `n_network.h` now maps `SSL_get1_peer_certificate` to the old spelling below
  `OPENSSL_VERSION_NUMBER` `0x30000000L`, and both call sites use the 3.0 name.
  The two functions are semantically identical, each returns a certificate with
  the reference count already bumped, so ownership is unchanged.
- **examples/ex_network_ssl, ex_network_ssl_hardened: two crashes on the
  certificate-failure path.** Neither checked what `netw_set_crypto()` returned,
  so an unreadable key/cert left the listener accepting in plaintext instead of
  exiting; every client then failed its handshake (`curl` exit 35) and the
  server crashed. Two distinct defects sat behind that: `handle_request()` ran
  `SSL_read(netw_ptr->ssl, ...)` without checking that a session exists, which
  segfaulted on OpenSSL 1.0.2, and its error paths called
  `netw_close(&netw_ptr)` on their own parameter, a copy, so the owning
  `ssl_network_thread()` closed the same NETWORK a second time. That double
  close is a heap-use-after-free on every platform, reachable from an ordinary
  early client disconnect, and reproduced on OpenSSL 3 as well. Both examples
  now treat a crypto setup failure as fatal, assert on the session before
  reading, and leave the close to the single owner.
- **n_network: `netw_set_crypto()` / `netw_set_crypto_pem()` no longer leak the
  `SSL_CTX` when setup fails.** Both create the context up front but only set
  `crypto_algo = NETW_ENCRYPT_OPENSSL` once every step has succeeded, and
  `netw_close()` keys its `SSL_CTX_free()` on exactly that flag. Any failure in
  between (unreadable certificate, unreadable key, key that does not match the
  certificate, malformed PEM) therefore returned FALSE leaving `netw->ctx`
  allocated and unowned, ~20 KB per attempt under LeakSanitizer. A new
  `_netw_crypto_setup_failed()` helper releases it on all ten failure paths.
  The `_chain` variants were already safe, they only fail after the base call
  has set the flag.
- **n_x509: certificates now verify on OpenSSL 1.0.2, not just compile.** With
  the build fixed, `ex_x509` still failed at run time on RHEL 7: every minted
  leaf was rejected with `X509_V_ERR_CERT_SIGNATURE_FAILURE`
  ("certificate signature failure"), and `ASN1_item_verify()` underneath it
  reported an unknown message digest algorithm. OpenSSL 1.1.0 initialises
  itself on first use, but before that the EVP algorithm table starts empty:
  signing worked because `X509_sign()` is handed `EVP_sha256()` directly, while
  verification has to resolve sha256WithRSAEncryption by OID and found nothing.
  Programs that also use `n_network` were unaffected, `netw_init_openssl()`
  already calls `OpenSSL_add_all_algorithms()`; `n_x509` used standalone had no
  equivalent. The three public entry points now run a `pthread_once` guarded
  initialiser, compiled out entirely from 1.1.0 on.
- **n_x509: builds again against OpenSSL 1.0.2 (RHEL 7).** The module used three
  things introduced in OpenSSL 1.1.0 on the unconditional path: the `BN_rand()`
  named constants `BN_RAND_TOP_ANY` / `BN_RAND_BOTTOM_ANY` (hard compile error
  on 1.0.2), the `X509_getm_notBefore()` / `X509_getm_notAfter()` accessors
  (implicit declaration, then an `int` passed where `X509_gmtime_adj()` wants an
  `ASN1_TIME*`), and the `const char*` value argument of
  `X509V3_EXT_conf_nid()`, which 1.0.2 declares as plain `char*`
  (`-Wdiscarded-qualifiers`). A compatibility block guarded on
  `OPENSSL_VERSION_NUMBER < 0x10100000L` now supplies the two constants and the
  two accessor spellings, and the extension value goes through an
  `N_X509_EXT_VALUE()` macro that strips the qualifier only on the old API.
  LibreSSL reports `0x20000000L` and already provides all of them, so it keeps
  the modern path; 1.1.0 and later are byte-for-byte unchanged.
- **Static analysis: `make check` is clean again on the cppcheck shipped by CI
  (2.10, Debian bookworm), which is older than the locally installed one.** Two
  diagnostics only that version reports: the `constParameterPointer` inline
  suppression on `n_gui_window_from_display()` did not match, because 2.10 still
  emits the pre-2.11 id `constParameter` (the check was split into
  `constParameterPointer` / `constParameterReference` / `constParameterCallback`
  in 2.11), so the suppression now lists both ids on consecutive lines; and
  `variableScope` on `line` in `h3_setup_h3()`, fixed for real by declaring it
  inside the `if (hdr_copy)` block that uses it. No behavior change.
- **examples/ex_gui_multiwin.c: the own-chrome title bar check no longer depends
  on the graphics driver's buffer-swap strategy.** It cleared the native
  backbuffer, called `n_gui_draw_detached()` (which ends in `al_flip_display`)
  and then read the pixels back. Backbuffer contents are undefined after a flip:
  a page-flipping driver (Mesa on real hardware) returns a discarded buffer, so
  the readback saw the pre-clear black and the check failed, while a
  copy-swapping driver (llvmpipe under Xvfb, i.e. CI) preserved it and passed.
  The pixels are now counted from inside the frame via a content-draw callback,
  which runs after the window and its title bar are painted and before the flip.
  The assertion itself is unchanged and now holds on both driver classes; no
  library code was involved.
