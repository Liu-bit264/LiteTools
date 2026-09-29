# LiteTools — Embedded Project Toolset

[简体中文](README.md) | English

General-purpose project tools for the LiteBootLoader family:

- [LiteBootLoader](../LiteBootLoader) — STM32 BootLoader framework (primary consumer of these tools)
- [LiteBootUpgrader](../LiteBootUpgrader) — serial upgrade host tool

The tools come in two groups: `uvprojx/` covers Keil projects (`parser.py` project → JSON
spec, `generator.py` spec → project, `chipfill.py` chip manifest + templates → spec/scatter,
plus the generic `templates/` sct templates and the `test_uvprojx.py` round-trip tests);
`ico/` covers icons (`parser.py` parsing/validation, `generator.py` PNG → ICO, plus
`test_ico.py`). Every tool is a single-file CLI built on the pure standard library
(Pillow optional, for automatic ICO scaling) with no hard third-party dependencies;
how to run the tests is described in [CONTRIBUTING.md](CONTRIBUTING.md).

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
create and update modes self-validate before writing and back up automatically. Verify
generated projects in Keil uVision manually before building.

### chipfill.py — CSP Chip-Manifest Filling (with LiteBootLoader)

```bash
# Run from the firmware repo root; spec templates default to <root>/chips/templates/,
# sct templates ship with this tool
uv run --python 3.12 ../LiteTools/uvprojx/chipfill.py \
    --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
```

Placeholders `{{chip.a.b}}` (whole-value reference, lists/objects expanded) and
`{"$chip": "a.b"}` (container expansion); derives `derived.cpu_bootloader/cpu_app`
(Keil Cpu strings: 8-digit hex addresses, leading zeros trimmed from sizes).
Field conventions are documented in the LiteBootLoader repo's `docs/dev/design.md`
ADR-015 and `docs/porting_guide.md` §2.

## ICO Tools

```bash
uv run --python 3.12 --with pillow ../LiteTools/ico/parser.py <file.ico>
uv run --python 3.12 --with pillow ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

The parser validates the ICO header (`00 00 01 00`), directory boundaries and entry data
ranges, and identifies PNG / BMP-DIB entries; the generator prefers Pillow for scaling to
32bpp BMP-DIB and falls back to direct PNG embedding without it (sizes must match
one-to-one), self-validating and backing up before writing.

## License

[MIT](LICENSE) © 2026 Qingc.

## Contributing

Issues, PRs and how to run the unit tests: [CONTRIBUTING.md](CONTRIBUTING.md).
