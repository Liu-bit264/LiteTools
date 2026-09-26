# LiteTools — 嵌入式工程工具集

LiteBootLoader 家族的通用工程工具，独立建仓（与固件仓解耦的项目卫生要求）：

- [LiteBootLoader](../LiteBootLoader) —— STM32 BootLoader 框架（工具的主要使用方）
- [LiteBootUpgrader](../LiteBootUpgrader) —— 串口升级上位机

全部工具无第三方硬依赖（Pillow 可选，供 ICO 自动缩放），纯标准库 + uv 隔离运行约定。

## 仓库结构

```text
LiteTools/
├── uvprojx/
│   ├── parser.py          Keil .uvprojx 解析器（工程 → JSON 规格）
│   ├── generator.py       Keil .uvprojx 生成器（JSON 规格 → 工程，含更新模式）
│   ├── chipfill.py        CSP 芯片清单填充器（chip.json + 模板 → spec/sct，ADR-015）
│   ├── test_uvprojx.py    解析/生成往返等单测
│   └── templates/         sct 通用模板（纯占位符，随工具分发）
└── ico/
    ├── parser.py          ICO 解析器（结构校验 + 格式识别）
    ├── generator.py       ICO 生成器（PNG → ICO，Pillow 可选）
    └── test_ico.py        解析/生成单测
```

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
自动备份。生成结果需在 Keil uVision 中人工验证后再编译。

### chipfill.py — CSP 芯片清单填充（配合 LiteBootLoader）

```bash
# 在固件仓根目录运行；spec 模板默认取 <root>/chips/templates/，sct 模板随本工具
uv run --python 3.12 ../LiteTools/uvprojx/chipfill.py \
    --chip chips/f103c8t6.json --target bootloader \
    --spec-out bootloader.spec.json --sct-out linker/bootloader.sct
```

占位符 `{{chip.a.b}}`（整值引用，列表/对象展开）与 `{"$chip": "a.b"}`（容器展开）；
派生 `derived.cpu_bootloader/cpu_app`（Keil Cpu 字符串：地址 8 位十六进制、尺寸去前导零）。
详细字段约定见主仓 `docs/design.md` ADR-015 与 `docs/porting_guide.md` §2。

## ICO 工具

```bash
uv run --python 3.12 --with pillow ../LiteTools/ico/parser.py <文件.ico>
uv run --python 3.12 --with pillow ../LiteTools/ico/generator.py --sizes 16,32,48,256 -o icon.ico icon.png
```

解析器校验 ICO 头（`00 00 01 00`）、目录边界与条目数据范围，识别 PNG / BMP-DIB 条目；
生成器优先 Pillow 缩放为 32bpp BMP-DIB，无 Pillow 时回退 PNG 直嵌（要求尺寸一一对应），
写入前自校验、自动备份。

## 测试

```bash
cd uvprojx && uv run --python 3.12 python test_uvprojx.py
cd ico && uv run --python 3.12 python test_ico.py
```

## 许可证

[MIT](LICENSE) © 2026 Qingc。
