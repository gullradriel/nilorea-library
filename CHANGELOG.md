# Changelog

All notable changes to the Nilorea library are recorded here. The format is
based on [Keep a Changelog](https://keepachangelog.com/). This project does not
currently tag semantic versions, so entries accumulate under **Unreleased**.

## [Unreleased]

### Added

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
