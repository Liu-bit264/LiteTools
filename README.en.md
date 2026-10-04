# LiteTools — Embedded Project Toolset

[简体中文](README.md) | English

General-purpose project tools for the LiteBootLoader family:

- [LiteBootLoader](../LiteBootLoader) — STM32 BootLoader framework (primary consumer of these tools)
- [LiteBootUpgrader](../LiteBootUpgrader) — serial upgrade host tool

The tools come in four groups: `uvprojx/` covers Keil projects (`parser.py` project → JSON
spec, `generator.py` spec → project, `chipfill.py` chip manifest + templates → spec/scatter,
plus the generic `templates/` sct templates and the `test_uvprojx.py` round-trip tests);
`ico/` covers icons (`parser.py` parsing/validation, `generator.py` PNG → ICO, plus
`test_ico.py`); `configgen/` bootstraps chip support packages (CSP) — it derives a
`chips/<id>.json` + `board_config.h` skeleton from an existing chip, with matching CLI and
tkinter GUI; `pinout/` produces development-board pinout diagrams (a C++/Qt6 GUI with
project management, five annotation tools, a reuse-definition library and high-res PNG
export). Each tool is one directory; small tools stay on the pure Python standard library
(Pillow is optional and used by the ICO generator only), while large GUI tools may use
their own stack with third-party UI dependencies (`pinout/` is C++20 + Qt6). Both Python
generators read the sibling `parser.py` at runtime to self-validate before writing, so
copying a single file out of the repo breaks them. How to run the tests is described in
[CONTRIBUTING.md](CONTRIBUTING.md).

## uvprojx Tools

### parser.py — Project Parsing

```bash
uv run --python 3.12 ../LiteTools/uvprojx/parser.py <project.uvprojx> -o spec.json
```

Extracts target/toolchain/device/output/defines/include paths/scatter/source groups into
JSON. Safety constraints: rejects XML with a DTD, limits input size (against XML entity
expansion).

### generator.py — Project Generation

```bash
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json -o new.uvprojx        # create mode
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json --update old.uvprojx  # update mode (keeps unknown fields)
```

Device-related fields (FlashDriverDll/RegisterFile/SFDFile/CpuType/debug DLL arguments)
always come from the spec's `device` field (data-driven, no device-name hardcoding);
create and update modes self-validate before writing and back up automatically. **When the
spec omits IRAM/IROM/CpuType the generator falls back to the F103C8 defaults**
(`_DEFAULT_MEMORY`), so non-F1 chips must state them explicitly (the CSP flow derives them
via chipfill and is unaffected). Verify generated projects in Keil uVision manually before
building.

### chipfill.py — CSP Chip-Manifest Filling (with LiteBootLoader)

```bash
# Run from the firmware repo root; spec templates default to <root>/chips/templates/,
# sct templates ship with this tool
uv run --python 3.12 ../LiteTools/uvprojx/chipfill.py \
    --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
```

Placeholders `{{chip.a.b}}` (whole-value reference: when the whole string is exactly one
reference the value is inserted as-is; inside a longer string it degrades to scalar text
substitution) and `{"$chip": "a.b"}` (container expansion: inlined into objects,
concatenated inside lists); derives `derived.cpu_bootloader/cpu_app`
(Keil Cpu strings: 8-digit hex addresses, leading zeros trimmed from sizes).
Field conventions are documented in the LiteBootLoader repo's `docs/dev/design.md`
ADR-015 and `docs/porting_guide.md` §2.

## ICO Tools

```bash
uv run --python 3.12 ../LiteTools/ico/parser.py <file.ico>
uv run --python 3.12 --with pillow ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

The parser validates the ICO header (`00 00 01 00`), directory boundaries and entry data
ranges, and identifies PNG / BMP-DIB entries; the generator prefers Pillow for scaling to
32bpp BMP-DIB and falls back to direct PNG embedding without it (sizes must match
one-to-one), self-validating and backing up before writing.

## configgen — CSP Bootstrap Generator (CLI + GUI)

Derives a new chip support package (CSP) skeleton from an existing chip, producing a
`chips/<id>.json` + `port/<family>/<id>/board_config.h` pair; the partition/erase-unit
geometry checks mirror the firmware `bl_storage` runtime self-check (APP boundaries must
land on unit boundaries). Chip facts that cannot be derived (DFP flash_driver, startup
file names, …) are emitted as TODO placeholders with forced attention notes.

```bash
# CLI: derive from f103c8t6 (run from the LiteBootLoader repo root)
uv run --python 3.12 ../LiteTools/configgen/generator.py \
    --from f103c8t6 --id f103rc --dry-run          # preview, writes nothing
uv run --python 3.12 ../LiteTools/configgen/generator.py \
    --from f411ceu6 --id f411ceu6x --app-base 0x08020000 \
    --flash-size 0x000C0000 --erase-mode table     # override differences; auto-backup
# GUI (tkinter, standard library)
uv run --python 3.12 ../LiteTools/configgen/gui.py
```

Afterwards: fill the TODO placeholders → implement the port ops → run the firmware
repo's `chips/test_chip.py` consistency tests → render/build via chipfill.
Tests: `uv run --python 3.12 python configgen/test_configgen.py`.

## pinout — Board Pinout Diagram Tool (C++/Qt6)

Project-based pin annotation: each task is a `.pinout.json` project created by importing a
board photo; entering a project shows the board centered on a blueprint-grid canvas. Five
tools — **single pin** (circular marker, crosshair follows the mouse with the marker center
at the crosshair intersection), **multi pin** (click an anchor, pins extend up/down with
equal spacing, Ctrl+wheel adjusts the spacing live, batch naming on confirm), **single
header** (square), **multi header**, and **append reuse definition** (every definition you
type joins the project-level reuse library; click a pin to append via inline autocomplete,
or rubber-band several pins and batch-append from the properties panel). The properties
panel edits name/side/name category/definition chain (names guess PA/PB/PC/GND/power
category colors automatically); Ctrl+E exports a high-res PNG poster (centered board +
definition chips chained with dashed leaders + a two-row legend).

### Build (MSYS2 ucrt64 toolchain, Qt ≥ 6.8)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools \
    mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cd pinout
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=E:/dev-tools/compilers/msy2/ucrt64
cmake --build build
ctest --test-dir build    # offscreen unit tests (project/layout/exporter)
```

### Run

```bash
PATH="E:/dev-tools/compilers/msy2/ucrt64/bin:$PATH" ./build/pinout.exe [project.pinout.json]
# No argument opens the start page (new / open / recent projects)
```

**Standalone double-click launch**: deploy the runtime DLLs next to the exe first, otherwise
Windows may load an incompatible copy from PATH and fail with "entry point not found":

```bash
<ucrt64>/bin/windeployqt --release --compiler-runtime build/pinout.exe
# The MinGW windeployqt does NOT copy the compiler runtime; also copy from ucrt64/bin:
#   libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll   (zlib1.dll ships with Qt)
```

### CLI export

```bash
./build/pinout.exe --export project.pinout.json [out.png] [scale]   # shares the exporter with the GUI; defaults to <project>.png at 2x
```

## License

[MIT](LICENSE) © 2026 Qingc.

## Contributing

Issues, PRs and how to run the unit tests: [CONTRIBUTING.md](CONTRIBUTING.md).
