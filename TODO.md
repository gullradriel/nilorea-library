# TODO

## GUI performance optimization (n_gui) — DONE

Result: `ex_gui_bench` draw time **15.4 -> 4.66 ms/frame** (~70%, 63 -> 214
fps-equivalent) on the reference heavy layout (Xvfb/llvmpipe software GL; the
relative win is larger on real GPU hardware), plus an opt-in idle-skip signal
and a scale-only event-path win. Every phase: build clean (`-Wall -Wextra
-Wconversion` ...), ASan/LSan/UBSan clean, ThreadSanitizer clean, cppcheck
(exhaustive) + scan-build (`--status-bugs`) clean, and pixel-/behaviour-verified.

- [x] Phase 0: performance benchmark harness `examples/ex_gui_bench.c` (eae88e92)
- [x] Phase 1: batch item text with `al_hold_bitmap_drawing` (two-pass bg/text) in listbox, radiolist, datagrid, syntaxview, combobox/dropmenu panels (fd96be89)
- [x] Phase 2: memoize text measurement (`_text_w` / `_text_dims` file-scope cache) (be371a3e)
- [x] Phase 3a: cache syntaxview line count / headers-end index (da7e4457)
- [x] Phase 3b: coalesce textarea per-glyph draws into per-run draws (6227ff7b)
- [x] Phase 4: gate motion-time tooltip/datagrid scans on `_pointer_over_window` (ce05e6c9)
- [x] Phase 5: opt-in redraw signal `n_gui_needs_redraw` / `n_gui_mark_dirty` (30fe59e1)
- [x] Demonstrate the redraw signal in `ex_gui.c` main loop (fb474938)

## Cross-platform clipboard (n_clipboard) — DONE

- [x] Win32 clipboard backend for Windows/MSYS2 MinGW64: `N_CLIPBOARD_CLIPBOARD` via `CF_UNICODETEXT` with UTF-8 <-> UTF-16 conversion, `OpenClipboard` retry, process-local mutex; `N_CLIPBOARD_PRIMARY` is a graceful no-op (Windows has no PRIMARY selection). Makefile Windows `CLIBS` gains `-luser32` (978b73fe). Cross-compiles clean with `x86_64-w64-mingw32-gcc` (GCC 15) under the full warning set, links into an `.exe`, Linux X11 branch unchanged, cppcheck exhaustive clean.

## Deferred GUI optimizations (lower value / higher risk after the above)

- [ ] Gate the per-motion hover-reset scan (still O(widgets) per `MOUSE_AXES`); needs tracking the hovered widget plus titlebar-button and listbox-row hover state
- [ ] Gate the per-frame autofit / `_window_update_content_size` / `_compute_gui_bounds` passes on the dirty bit; make `n_gui_draw_detached` clear the dirty bit (detached-only-host case)
- [ ] Integer-keyed `n_gui_get_widget` (drop the per-call `snprintf` + string hash)
- [ ] Cache static window chrome / justified-label wrap layout; datagrid per-cell truncation cache; store a parent-window back-pointer on widgets to drop `_find_widget_window`'s nested scan

## Verification not runnable in the dev sandbox

- [ ] MinGW / Windows cross-compile (CI job) — `n_clipboard.c` was cross-compiled + linked per-file here (clean), but the full library via the Makefile Windows path was not; no POSIX-only headers or symbols were added, expected clean
- [ ] Run the Win32 clipboard (n_clipboard) copy/paste round-trip on a real MSYS2 MinGW64 host — compiled + linked here, but not executed (no Windows / wine in the sandbox)
- [ ] Real-GPU benchmark numbers (all figures so far are Xvfb/llvmpipe software GL)
- [ ] Drive the interactive GUI examples by hand (`ex_gui`, `ex_gui_datagrid`, `ex_gui_dropmenu`, ...)
