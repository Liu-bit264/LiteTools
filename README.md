# LiteTools — 嵌入式工程工具集

简体中文 | [English](README.en.md)

LiteBootLoader 家族的通用工程工具：

- [LiteBootLoader](../LiteBootLoader) —— STM32 BootLoader 框架（工具的主要使用方）
- [LiteBootUpgrader](../LiteBootUpgrader) —— 串口升级上位机

工具分两组：`uvprojx/` 面向 Keil 工程（`parser.py` 工程 → JSON 规格、`generator.py`
规格 → 工程、`chipfill.py` 芯片清单 + 模板 → spec/scatter，另有 `templates/` 通用 sct
模板与 `test_uvprojx.py` 往返单测）；`ico/` 面向图标（`parser.py` 解析校验、
`generator.py` PNG → ICO，另有 `test_ico.py` 单测）。每个工具一个文件、纯标准库实现
（Pillow 仅 ICO 生成器可选使用），无第三方硬依赖；两个 generator 运行时会读取同目录的
`parser.py` 做写入前自校验，单独拷走一个文件会失效。单测运行方式见
[CONTRIBUTING.md](CONTRIBUTING.md)。

## uvprojx 工具

### parser.py — 工程解析

```bash
uv run --python 3.12 ../LiteTools/uvprojx/parser.py <工程.uvprojx> -o spec.json
```

提取 target/工具链/device/输出/宏定义/include 路径/scatter/源文件分组为 JSON。
安全约束：拒绝含 DTD 的 XML，限制输入大小（防 XML 实体扩展）。

### generator.py — 工程生成

```bash
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json -o new.uvprojx        # 创建模式
uv run --python 3.12 ../LiteTools/uvprojx/generator.py spec.json --update old.uvprojx  # 更新模式（保留未知字段）
```

设备相关字段（FlashDriverDll/RegisterFile/SFDFile/CpuType/调试 DLL 参数）一律来自
规格 `device` 字段（数据驱动，无设备名硬编码）；创建/更新模式均先自校验再写入，
自动备份。**规格缺 IRAM/IROM/CpuType 时会回退 F103C8 默认布局**（`_DEFAULT_MEMORY`），
非 F1 芯片必须在 spec 中显式给出（CSP 流程由 chipfill 派生，不受影响）。
生成结果需在 Keil uVision 中人工验证后再编译。

### chipfill.py — CSP 芯片清单填充（配合 LiteBootLoader）

```bash
# 在固件仓根目录运行；spec 模板默认取 <root>/chips/templates/，sct 模板随本工具
uv run --python 3.12 ../LiteTools/uvprojx/chipfill.py \
    --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
```

占位符 `{{chip.a.b}}`（整值引用：整个字符串恰为一个引用时取回原值整体放入；嵌在更长文本中时
按标量替换）与 `{"$chip": "a.b"}`（容器展开：对象内联、列表中拼接）；
派生 `derived.cpu_bootloader/cpu_app`（Keil Cpu 字符串：地址 8 位十六进制、尺寸去前导零）。
详细字段约定见 LiteBootLoader 仓库 `docs/dev/design.md` ADR-015 与 `docs/porting_guide.md` §2。

## ICO 工具

```bash
uv run --python 3.12 ../LiteTools/ico/parser.py <文件.ico>
uv run --python 3.12 --with pillow ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

解析器校验 ICO 头（`00 00 01 00`）、目录边界与条目数据范围，识别 PNG / BMP-DIB 条目；
生成器优先 Pillow 缩放为 32bpp BMP-DIB，无 Pillow 时回退 PNG 直嵌（要求尺寸一一对应），
写入前自校验、自动备份。

## 许可证

[MIT](LICENSE) © 2026 Qingc。

## 参与贡献

反馈问题、提交 PR 与运行单测见 [CONTRIBUTING.md](CONTRIBUTING.md)。
