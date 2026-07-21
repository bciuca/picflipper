# Host unit tests

Pure-logic unit tests that compile and run **on the dev machine** with plain
`cc`/`clang` — not on the Flipper, not through `ufbt`. They exercise the
project's hardware-independent logic in isolation.

```sh
make -C test test     # build + run everything; non-zero exit on any failure
make -C test clean
```

No dependencies beyond a C11 compiler and `make`. CI runs the same target as a
separate `host-tests` job (`.github/workflows/build.yml`), alongside the ufbt
FAP build.

## The SDK-header problem, and the approach

The units under test live in `src/*.c` and `#include` Flipper SDK headers
(`furi.h`, `gui/*`, `storage/*`, `furi_hal_gpio.h`, …) that do not exist on the
host. Two mechanisms bridge that gap; **no product source is modified**:

- **Minimal stub headers** under `test/stubs/` mirror the SDK include paths and
  declare only the types/enums/functions each unit references. Inert calls
  (e.g. `canvas_clear`, the View lifecycle) are no-op `static inline`. The few
  calls whose behavior the test observes are declared in the stub header and
  **defined in the test** (`canvas_string_width`, the `storage_file_*` capture
  backend, the GPIO tracer).
- **Direct `#include` of the product `.c`** so tests can reach `static`
  functions (`ihex_record`, `about_wrap`/`about_emit`, `pic_icsp_*`) and the
  file's `#define`d constants.

Include search is `-I../src` (resolves `#include "<unit>.c"` and product
headers) and `-Istubs` (resolves the SDK stubs). Built `-Werror`.

## What is covered

| Test file | Unit | Checks |
|---|---|---|
| `test_sha256.c` | `sha256.c` | FIPS 180-4 vectors: empty, `"abc"`, the 56-byte two-block message, one-million-`a`; plus byte-at-a-time streaming equals one-shot. No SDK deps — included as-is. |
| `test_dump_writer.c` | `dump_writer.c` | Intel-HEX records with hand-computed checksums: data records, type-04 extended-linear-address records, EOF; the streaming writer's 16-byte record grouping, the tail record, upper-address change at a 64 KB boundary; raw `.bin` passthrough. Storage is stubbed to capture written bytes. |
| `test_pic_icsp.c` | `pic_icsp.c` | LSb-first 4-bit command and 16-bit payload shifting; `core_inst`/`set_tblptr` opcode sequences; table-read (`1001`), shift-out-TABLAT (`0010`), table-write (`1100`/`1101`/`1111`) opcodes; live data-read core instructions; the MSb-first 32-bit entry key `0x4D434850`. Verified against DS39688D. GPIO is a tracer that samples PGD on each PGC rising edge; reads are served from a queue. |
| `test_view_about.c` | `view_about.c` | `about_wrap`/`about_emit`: word-boundary wrapping, `\n` handling (incl. preserved blank lines), preserved leading indent, hard-break of an over-long token (the URL), and the `ABOUT_LINE_CHARS-1` line clamp. `canvas_string_width` is stubbed as 6 px/char so wrap points are deterministic. |
| `test_prog_progress.c` | `prog_progress.h` (`prog_apply_progress`, used by `pic_jobs.c`) | Maps a write/verify engine progress event onto the `AppState` view model: `ProgStage` -> stage name, phase/byte counters, and the device ID copy. Regression guard for the write/verify progress screen showing `Device ID: 0000` (DEVID, read during ICSP entry, must appear from the first tick, not only after the op finishes). The pure mapping was split out of `pic_jobs.c`'s progress callback so it is importable without the worker/ICSP/storage stack. |
| `test_ui_wrap.c` | `ui_wrap.h` (`ui_wrap_hard`, used by `app_ui.c`) | Hard character-wrap for space-less strings on the Done/Error screens. Regression guard for a 32-char timestamped dump filename overflowing the 128 px display on one line: every wrapped line must measure within the width budget, all characters preserved in order, and a glyph wider than the budget must still advance one char per line (no infinite loop). Measurement is injected (6 px/char stub) so it runs without a Canvas. |

## What is deliberately **not** covered, and why

Anything that genuinely needs the SDK, RTOS, or hardware is out of scope — the
goal is pure logic, not a firmware simulator:

- **Views / draw / input** (`view_console.c`, `view_pins.c`, the About view's
  draw/input callbacks) — need a live `Gui`/`Canvas`/`ViewDispatcher`.
- **Workers, threading, timing** (`pic_jobs.c`, `pic_ops.c`) — `FuriThread`,
  message queues, `furi_delay_*` pacing.
- **ICSP electrical timing** (`pic_clock_bit` half-periods, program/erase
  self-timing NOPs) — bit *order* and *opcodes* are tested; wall-clock timing is
  not observable or meaningful on the host.
- **Real storage / filesystem** (`dump_store.c`, directory scans) — the writer's
  *formatting* is tested via a capture stub; actual SD-card I/O is not.
- **`main.c`, `app_ui.c`, `pic_program.c`, `pic_dump.c`** — app wiring and
  hardware sequencing.

## Adding a test

Create `test/test_<unit>.c` (include `tap.h`, define any behavioral stubs,
`#include "<unit>.c"`), add `test_<unit>` to `TESTS` in the `Makefile`, and add
whatever new symbols the unit references to the stub headers. Use `CHECK(cond,
"desc")`; end `main` with `return tap_done("<unit>")`.
