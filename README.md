# Nilorea C Library

A portable C library providing common data structures, networking, GUI widgets,
2D/3D helpers, and more. Designed to be used as a monolith library or as a
collection of individual source files dropped into your project.

## Features

### Core modules (no external dependencies beyond pthreads, zlib/LZ4 are vendored)
- Logging system with configurable levels and file output (`n_log`)
- No-duplicate logging to console, file, or syslog (`n_nodup_log`)
- Dynamic strings with formatting helpers (`n_str`)
- Generic linked lists (`n_list`)
- Hash tables (`n_hash`)
- Thread pools (`n_thread_pool`)
- Stack data structure (`n_stack`)
- Tree data structure (`n_trees`)
- Base64 encoding / decoding (`n_base64`)
- Vigenere cipher encoding / decoding (`n_crypto`)
- Enum-to-string macro helpers (`n_enum`)
- Signal handling helpers (`n_signals`)
- Exception-like macros (`n_exceptions`)
- Common macros and typedefs (`n_common`)
- File helpers (`n_files`)
- Time / timer utilities (`n_time`)
- zlib compression helpers (`n_zlib`), vendored under `external/zlib/`, built into the library
- LZ4 block-compression helpers (`n_lz4`), vendored under `external/lz4/`, built unconditionally and used as an opt-in network compression backend

### Networking (requires OpenSSL for SSL support)
- TCP / UDP network engine with optional SSL (`n_network`)
- HTTP CONNECT, HTTPS CONNECT, and SOCKS5 proxy tunneling (`n_network`)
- WebSocket client handshake and framing (`n_network`)
- Server-Sent Events (SSE) client (`n_network`)
- Network message framing (`n_network_msg`)
- Parallel accept pool, nginx-style multi-threaded accept (`n_network_accept_pool`)
- Single-threaded epoll reactor as an opt-in alternative to the per-connection thread engine (`n_reactor`, Linux/Android only)
- Clock synchronization estimator for networked games (`n_clock_sync`)
- Per-connection compression backend (`netw_set_compression_mode`): `NETW_COMPRESS_NONE` / `_ZLIB` / `_LZ4`. The wire layout is self-describing, so the two ends can run different codecs and still interop.

### PCRE (requires libpcre2)
- PCRE2 regex wrapper (`n_pcre`)
- Configuration file parser (`n_config_file`)

### Allegro 5 GUI & game modules (require liballegro5)
- **GUI widget system** with pseudo-windows (`n_gui`) -- buttons, toggle
  buttons, horizontal & vertical sliders, text areas, checkboxes, scrollbars,
  listboxes, radio lists, combo boxes, labels/hyperlinks, images, dropdown menus,
  frameless windows, disabled/hidden widgets, auto-scrollbar windows, resizable
  windows, global display scrollbars, cross-platform DPI scale detection, and
  optional bitmap skinning for all widgets and windows
- Allegro 5 input and display helpers (`n_allegro5`)
- Isometric engine with height segments, depth-sorted object rendering, occlusion
  detection with clipped ghost overlay, 2D camera, multiple projection presets, and
  terrain transitions, with optional cross-chunk neighbor arrays so transition masks
  span chunk edges without water-clamp seams (`n_iso_engine`)
- A* pathfinding (`n_astar`)
- Dead reckoning / prediction (`n_dead_reckoning`)
- Trajectory helpers (`n_trajectory`)
- AABB collision (`n_aabb`)
- Particle system (`n_particles`)
- Fluid dynamics simulation (`n_fluids`)
- 3D helpers (`n_3d`)
- Animation helpers (`n_anim`)
- Game environment utilities (`n_games`)
- Network-oriented user handling (`n_user`)

### SSL/TLS Hardening
- Security audit and hardened HTTPS example (`SSL_SECURITY.md`)
- TLS 1.2+ enforcement, strong cipher suites, HSTS, path traversal protection
- Persistent server scripts with auto-restart (`serve_ssl.sh`, `serve_ssl_hardened.sh`)

### Data interchange (requires cJSON)
- Avro binary format encoding/decoding with JSON conversion (`n_avro`)

### Git operations (requires libgit2)
- Git repository operations via libgit2 (`n_git`), open/init/close repos, stage/unstage
  files, commit, log, diff, checkout, branch management, push/pull with auth (token,
  basic, SSH), fetch from remotes, ahead/behind counts, per-repo operation status reporting

### Optional / extra
- Kafka consumer / producer wrappers (`n_kafka`, requires librdkafka): optional retry-after-timeout for errored events, transactional producing when a `transactional.id` is configured, and optional move-on-ack of produced files to a `sended` directory (`n_kafka_set_sended_dir`) instead of deleting them
- cJSON integration (included as git subtree)

## Dependencies

Most core modules compile with only **pthreads** and a C17 compiler. The zlib and LZ4 codecs are vendored as git subtrees and built into the library, no system `-lz` dependency.

| Dependency | Required for | How to get it |
|---|---|---|
| gcc / make | Building | Your system package manager |
| pthreads | Core (threads, network) | Usually bundled with your C toolchain |
| zlib | Compression helpers (`n_zlib`) | Included as git subtree under `external/zlib/` (no system package needed) |
| LZ4 | LZ4 block compression helpers (`n_lz4`) | Included as git subtree under `external/lz4/` (no system package needed) |
| OpenSSL | SSL networking | `apt install libssl-dev` / `pacman -S openssl` / `brew install openssl` |
| libpcre2 | Regex module | `apt install libpcre2-dev` / `pacman -S pcre2` |
| Allegro 5 | GUI, isometric, particles, etc. | `apt install liballegro5-dev liballegro-acodec5-dev liballegro-audio5-dev liballegro-image5-dev liballegro-primitives5-dev liballegro-ttf5-dev liballegro-font5-dev` |
| cJSON | JSON parsing, Avro support | Included as git subtree (no compilation needed) |
| librdkafka | Kafka wrappers | Included as git subtree (`make integrate-deps` to compile) or `apt install librdkafka-dev` |
| libgit2 | Git operations module | `apt install libgit2-dev` / `pacman -S libgit2` / `brew install libgit2` |
| doxygen + graphviz | Documentation | `apt install doxygen graphviz` |

## Building

### Supported platforms

| Platform | Compiler | Notes |
|---|---|---|
| **Linux** (x86_64, aarch64) | gcc / clang | Primary development platform |
| **Windows** (MinGW-w64) | gcc (MinGW) | Cross-compile from Linux or native MinGW shell |
| **Android** | NDK clang | Via Allegro Android toolchain |
| **Solaris** | gcc | Legacy support |

### Quick start (Linux)

```bash
cd my_project_dir
git clone --recurse-submodules git@github.com:gullradriel/nilorea-library.git
cd nilorea-library
make            # builds the static + shared library
make examples   # builds all example programs
# or
make all        # library + examples in one step
```

### Debug build

```bash
make DEBUG=1 clean all                   # ASan + UBSan + extra warnings
make DEBUG=1 DEBUG_THREADS=1 clean all   # ThreadSanitizer instead
```

Convenience targets wrap the above:

```bash
make asan        # alias for: make DEBUG=1 clean all
make tsan        # alias for: make DEBUG=1 DEBUG_THREADS=1 clean all
make asan-test   # build with ASan, then run examples/run_tests.sh
make tsan-test   # build with TSAN,  then run examples/run_tests.sh
```

### Running tests

```bash
make asan-test   # or: make tsan-test
# equivalent to:
make DEBUG=1 clean all
cd examples && bash run_tests.sh
```

The test runner executes all non-GUI examples and checks for
ASan/LSan/TSan reports. It verifies that binaries were compiled with
`DEBUG=1` before running (exits with an error otherwise). Network tests
(TCP, SSL, accept pool) run both server and client through the
sanitizer. Suppressions for system library leaks (Mesa, OpenSSL) are
loaded automatically from `examples/lsan-suppressions.cfg`.

### Disabling optional features

Use `FORCE_NO_*` flags to disable auto-detected optional dependencies:

```bash
make FORCE_NO_ALLEGRO=1   # disable Allegro 5 GUI/game modules
make FORCE_NO_OPENSSL=1   # disable SSL networking
make FORCE_NO_KAFKA=1     # disable Kafka integration
make FORCE_NO_PCRE=1      # disable PCRE2 regex support
make FORCE_NO_CJSON=1     # disable cJSON support
make FORCE_NO_LIBGIT2=1   # disable libgit2 Git operations
```

### MinGW cross-compilation (Linux host targeting Windows)

```bash
# Install MinGW-w64 toolchain
apt install gcc-mingw-w64-x86-64
# Build with the MINGW flag
make CC=x86_64-w64-mingw32-gcc MINGW=1 clean all
```

### Android (via Allegro Android toolchain)

1. Set up the Android NDK and Allegro 5 Android build as described in
   the [Allegro Android docs](https://liballeg.org/readme.html).
2. Point your `CC` to the NDK clang and set appropriate `CFLAGS` / `LDFLAGS`.
3. Build with `make`.

### Regenerating the documentation

```bash
make doc
# or directly:
doxygen Doxyfile
```

### Running static analysis

```bash
make check                          # cppcheck (gating) + scan-build (informational)
make check SCAN_BUILD_STATUS_BUGS=1  # also fail on scan-build findings
```

Both `cppcheck` and `scan-build` (from the `clang-tools` package on
Debian/Ubuntu) must be in `PATH`; the target reports a clear error and
exits non-zero if either binary is missing. Override the resolved tools
with `CPPCHECK=...` / `SCAN_BUILD=...` on the command line if needed.

Vendored third-party translation units under `external/` (zlib, LZ4,
cJSON) are pre-built **without** scan-build interception, then
scan-build drives only the `src/` rebuild. Their headers are also
included via `-isystem` so warnings emitted from inside them when our
`src/*.c` `#include`s them are suppressed at the source. Result:
analyzer and compiler diagnostics come exclusively from code we own.

### Compiling with extra dependencies

```bash
make update-deps       # update cJSON and librdkafka subtrees (needs git subtree)
make integrate-deps    # compile librdkafka, copy libs to the right directories
make clean ; make all  # full fresh build
```

## Examples

All examples live in the `examples/` directory and are built by `make examples`.

| Example | Description | Requires |
|---|---|---|
| `ex_gui` | Full GUI demo: all widget types, dropdown menus, toggle buttons, vertical sliders, frameless windows, disabled/hidden widgets, auto-scrollbars, resizable windows, global display scrollbars, DPI detection, bitmap-skinned containers (listbox/radiolist/combobox/dropmenu/textarea), titlebar button bitmaps, focused-keycode bindings, layout save/load JSON | Allegro 5 |
| `ex_gui_particles` | Particle system with real-time info overlay | Allegro 5 |
| `ex_gui_dictionary` | Dictionary search app (text input, listbox, labels) | Allegro 5, PCRE2 |
| `ex_gui_isometric` | Isometric map editor with dead reckoning | Allegro 5 |
| `ex_gui_network` | TCP chat application with GUI | Allegro 5, OpenSSL |
| `ex_fluid` | Fluid dynamics simulation | Allegro 5 |
| `ex_trajectory` | Trajectory / ballistic helpers demo | Allegro 5 |
| `ex_common` | Common macros and helpers demo | - |
| `ex_exceptions` | Exception handling demo | - |
| `ex_hash` | Hash table demo | - |
| `ex_list` | Linked list demo | - |
| `ex_log` | Logging system demo | - |
| `ex_nstr` | String helpers demo | - |
| `ex_base64` | Base64 encoding / decoding demo | - |
| `ex_crypto` | Vigenere cipher encryption demo | - |
| `ex_file` | File operations demo | - |
| `ex_zlib` | Zlib compression / decompression demo | - |
| `ex_stack` | Stack data structure demo | - |
| `ex_trees` | Tree data structure demo | - |
| `ex_threads` | Thread pool demo | - |
| `ex_network` | Network client/server demo | OpenSSL |
| `ex_network_mock` | Mock HTTP server for testing (serves canned responses) | OpenSSL |
| `ex_network_proxy` | HTTP/HTTPS CONNECT and SOCKS5 proxy tunneling demo | OpenSSL |
| `ex_network_ssl` | SSL network demo | OpenSSL |
| `ex_network_ssl_hardened` | Hardened HTTPS server (TLS 1.2+, security headers, path traversal protection) | OpenSSL |
| `ex_network_reactor` | Single-threaded epoll reactor demo (`n_reactor` + `netw_accept_into_reactor`), Linux/Android only | - |
| `ex_accept_pool_server` | Accept pool server: single-inline, single-pool, and pooled accept modes | - |
| `ex_accept_pool_client` | Accept pool client: stress-tests the server with concurrent connections | - |
| `ex_pcre` | PCRE regex demo | PCRE2 |
| `ex_configfile` | Config file parser demo | PCRE2 |
| `ex_clock_sync` | Clock synchronization over UDP (offset + RTT estimation) | OpenSSL |
| `ex_signals` | Signal handler demo | - |
| `ex_iso_astar` | A* pathfinding on isometric map | - |
| `ex_avro` | Avro binary encoding/decoding with JSON round-trip | cJSON |
| `ex_kafka` | Kafka event streaming demo | librdkafka, cJSON, PCRE2 |
| `ex_git` | Git repository operations demo (init, stage, commit, log, diff, branch) | libgit2 |
| `ex_monolith` | Monolith build test (all modules linked) | All |

## GUI System (`n_gui`)

The `n_gui` module provides a retained-mode widget system with pseudo-windows,
built on top of Allegro 5 primitives and fonts.

### Widget types
- **Button** (regular + toggle + bitmap)
- **Slider** (horizontal + vertical, value or percentage mode, configurable step with snap, mouse scroll + keyboard support, custom `printf`-style value readout format, toggleable value label)
- **Text area** (single-line + multiline with cursor)
- **Checkbox**
- **Scrollbar** (horizontal + vertical, rect or rounded)
- **Listbox** (none / single / multi select)
- **Radio list**
- **Combo box** (dropdown selector with scrollbar, auto-scroll to selected item on open, optional auto-width to fit longest item)
- **Label** (static text, left/center/right/justified, optional hyperlink)
- **Image** (fit / stretch / center)
- **Dropdown menu** (static + dynamic entries, rebuilt on open, scrollbar when entries overflow)

### Window features
- Draggable title bar
- Minimise (title bar only)
- Frameless (no title bar, drag from body area)
- Fixed position (disable dragging)
- Resizable (drag handle at bottom-right)
- Auto-scrollbar (vertical + horizontal scrollbars appear when content overflows)
- Auto-size (fit window to widget extents)
- Z-ordering (click to raise, always-on-top, always-behind, fixed z-value)
- Show/hide via dropdown menu
- Adaptive resize (per-window policies: none / move / scale)

### Adaptive window resize

Two context-level resize modes control how windows respond to display size changes:

- **`N_GUI_RESIZE_VIRTUAL`** (default): fixed virtual canvas, a uniform transform
  scales all coordinates identically, the existing behaviour.
- **`N_GUI_RESIZE_ADAPTIVE`**: the virtual canvas is disabled and each window
  follows its own per-window resize policy:
  - **`N_GUI_WIN_RESIZE_NONE`**, window stays at its absolute position and size.
  - **`N_GUI_WIN_RESIZE_MOVE`**, window repositions proportionally but keeps its pixel size.
  - **`N_GUI_WIN_RESIZE_SCALE`**, window repositions **and** resizes proportionally;
    child widgets scale with it.

```c
/* after creating all windows: */
n_gui_set_resize_mode(gui, N_GUI_RESIZE_ADAPTIVE);

/* assign policies per window */
n_gui_window_set_resize_policy(gui, win_menu,    N_GUI_WIN_RESIZE_SCALE);
n_gui_window_set_resize_policy(gui, win_buttons, N_GUI_WIN_RESIZE_MOVE);
n_gui_window_set_resize_policy(gui, win_fixed,   N_GUI_WIN_RESIZE_NONE);
```

When the user drags or resizes a window, the new position/size becomes the
reference for future adaptive resizes.

### Keyboard navigation
- **Tab / Shift+Tab** cycles focus between widgets in a window (wraps around)
- **Sliders**: Left/Right (horizontal) or Up/Down (vertical) adjust by one step; Home/End jump to min/max
- **Listbox / Radiolist / Combobox**: Up/Down selects previous/next item; Home/End jump to first/last
- **Checkbox**: Space/Enter toggles
- **Scrollbar**: Arrow keys scroll; Home/End jump to start/end
- **Text areas**: Ctrl+Tab inserts a literal tab; plain Tab moves focus

### Button key bindings
- **Global** (`n_gui_button_set_keycode`): fires when the bound key is pressed
  and no interactive widget has focus. Single-line textareas let Enter pass
  through for backward compatibility.
- **Focused** (`n_gui_button_set_keycode_focused`): fires only when the button
  itself or one of the listed source widgets has focus. This allows e.g. Enter
  in a URL textarea to trigger a Send button without interfering with Enter in
  other textareas. Source widgets are specified as an array of widget IDs (up to
  `N_GUI_KEY_SOURCES_MAX`).
- **Visual feedback**: When a button is activated through its keybind, it briefly
  renders in its pressed (active) visual state, same colours/bitmaps as a mouse
  click, so keyboard activation is as perceivable as a pointer click.

### Theme management

Widgets and windows inherit `ctx->default_theme`. Two helpers simplify
bulk theme changes:

```c
// Set a new default theme and auto-sync scrollbar colors
n_gui_set_default_theme(gui, my_theme);

// Push the default theme to every existing window and widget
n_gui_reset_all_widget_themes(gui);

// Re-apply individual overrides after the reset
n_gui_set_widget_theme(gui, accent_btn_id, accent_theme);
```

`n_gui_set_default_theme()` derives scrollbar track/thumb colors from the
theme's `bg_normal` and `border_normal` fields so scrollbars stay in sync
with the active palette.

### Widget states
- **Enabled/Disabled**, disabled widgets are drawn dimmed and ignore all input
- **Visible/Hidden**, hidden widgets are removed from drawing and hit testing

### Global display scrollbars

When the total GUI bounding box exceeds the display/viewport size, global
scrollbars automatically appear at the edges. This works dynamically with
resizable Allegro displays:

```c
// On startup:
n_gui_set_display_size(gui, (float)display_w, (float)display_h);

// On ALLEGRO_EVENT_DISPLAY_RESIZE:
al_acknowledge_resize(display);
n_gui_set_display_size(gui, (float)al_get_display_width(display),
                            (float)al_get_display_height(display));
```

### DPI scaling

Cross-platform DPI detection works on Linux, Windows, and Android:

```c
// Auto-detect from display (compares framebuffer to logical window size)
float scale = n_gui_detect_dpi_scale(gui, display);

// Or set manually
n_gui_set_dpi_scale(gui, 1.5f);

// Query at any time
float current_scale = n_gui_get_dpi_scale(gui);
```

**How it works:**
- Compares the physical pixel size (backbuffer bitmap) to the logical window
  size. On HiDPI displays these differ (e.g. 2x on Retina, 1.25x at 125% Windows scaling).
- On Android, uses the display DPI relative to the 160 DPI baseline.
- The detected scale is stored in `ctx->dpi_scale` and can be used by the
  application to scale fonts, widget sizes, etc.

### Windows multi-monitor DPI considerations

On Windows, when using per-monitor DPI awareness (e.g. laptop at 125%, external
monitor at 100%), the OS changes the effective DPI when the window is moved
between monitors. This can cause the window content to appear clipped by the
scaling difference (e.g. 25% clipping when moving from 125% to 100%).

**Recommended solution:**

1. **Enable per-monitor DPI awareness** via your application manifest or by
   calling `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)`
   before creating the display.

2. **Handle `ALLEGRO_EVENT_DISPLAY_RESIZE`** (which Allegro fires when Windows
   sends `WM_DPICHANGED`) and update the display size:
   ```c
   case ALLEGRO_EVENT_DISPLAY_RESIZE:
       al_acknowledge_resize(display);
       n_gui_set_display_size(gui, (float)al_get_display_width(display),
                                   (float)al_get_display_height(display));
       n_gui_detect_dpi_scale(gui, display);
       break;
   ```

3. **Scale your fonts** using the detected DPI factor:
   ```c
   float scale = n_gui_get_dpi_scale(gui);
   int font_size = (int)(13.0f * scale);
   ALLEGRO_FONT* font = al_load_ttf_font("font.ttf", font_size, 0);
   ```

4. **Use `ALLEGRO_RESIZABLE`** display flag so the window can be resized by the
   OS during DPI changes.

The `n_gui` module already handles `ALLEGRO_EVENT_DISPLAY_SWITCH_OUT` to reset
all drag/resize states, preventing GUI windows from being unintentionally
moved or resized when the OS window changes focus during monitor transitions.

## API Reference

Full API documentation is generated with Doxygen. Run `make doc` and open
`docs/html/index.html` in your browser.

## Support

- **Issues**: https://github.com/gullradriel/nilorea-library/issues
- **Documentation**: Run `make doc` to generate the full API reference
- **Examples**: See the `examples/` directory for working code samples

## License

This project is licensed under the **GNU General Public License v3.0 or later**, see the [LICENSE](LICENSE) file for the full text.

Copyright (C) 2005-2026 Castagnier Mickael
