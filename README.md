# LiteTools — 嵌入式工程工具集

简体中文 | [English](README.en.md)

LiteBootLoader 家族的通用工程工具：

- [LiteBootLoader](../LiteBootLoader) —— STM32 BootLoader 框架（工具的主要使用方）
- [LiteBootUpgrader](../LiteBootUpgrader) —— 串口升级上位机

工具分四组：`uvprojx/` 面向 Keil 工程（`parser.py` 工程 → JSON 规格、`generator.py`
规格 → 工程、`chipfill.py` 芯片清单 + 模板 → spec/scatter，另有 `templates/` 通用 sct
模板与 `test_uvprojx.py` 往返单测）；`ico/` 面向图标（`parser.py` 解析校验、
`generator.py` PNG → ICO，另有 `test_ico.py` 单测）；`configgen/` 面向芯片支持包
（CSP）引导——从现有芯片派生 `chips/<id>.json` + `board_config.h` 骨架，CLI 与
tkinter GUI 同源同构；`pinout/` 面向开发板引脚定义图制作（C++/Qt6 GUI，工程化管理 +
五工具标注 + 复用定义库 + 高清 PNG 导出）。每个工具一个目录实现；小工具保持纯标准库
Python（Pillow 仅 ICO 生成器可选使用），大型 GUI 工具允许独立技术栈与第三方 UI 依赖
（`pinout/` 使用 C++20 + Qt6）。Python 工具的两个 generator 运行时会读取同目录的
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

## configgen 配置生成器（CSP 引导，CLI + GUI）

从现有芯片派生新芯片支持包（CSP）骨架，产出 `chips/<id>.json` + 
`port/<family>/<id>/board_config.h` 两件套；分区/擦除单元几何校验与固件
`bl_storage` 运行期自检同源（APP 首末边界必须与单元边界重合）。芯片事实无法
派生的字段（DFP flash_driver、startup 文件名等）写 TODO 占位并强制提示。

```bash
# CLI：从 f103c8t6 派生（在 LiteBootLoader 仓库根运行）
uv run --python 3.12 ../LiteTools/configgen/generator.py \
    --from f103c8t6 --id f103rc --dry-run          # 预览，不写文件
uv run --python 3.12 ../LiteTools/configgen/generator.py \
    --from f411ceu6 --id f411ceu6x --app-base 0x08020000 \
    --flash-size 0x000C0000 --erase-mode table     # 差异项覆盖，覆盖前自动备份
# GUI（tkinter，标准库）
uv run --python 3.12 ../LiteTools/configgen/gui.py
```

生成后流程：补全 TODO 占位 → 实现 port ops → 跑固件仓 `chips/test_chip.py` 一致性
测试 → chipfill 渲染构建。单测：`uv run --python 3.12 python configgen/test_configgen.py`。

## pinout 开发板引脚定义图工具（C++/Qt6，大工具）

工程化管理引脚标注：每个任务一个 `.pinout.json` 工程，新建时导入开发板图片，进入工程后
板图居中显示于蓝图网格画布。五个工具——**单pin引脚**（圆形标记，十字虚线跟随鼠标、
交点即圆心）、**多pin引脚**（单击锚点后向上/下等距延伸，Ctrl+滚轮实时调间距，确认后
批量命名）、**单pin排针**（方形）、**多pin排针**、**追加复用定义**（输入过的定义自动
进入工程复用库，点击引脚内联补全追加；框选多引脚后可在属性面板批量追加）；右侧属性
面板编辑名称/侧别/名称分类/定义链（名称按 PA/PB/PC/GND/电源自动猜分类配色）；Ctrl+E
导出高清 PNG 海报（板图居中 + 名称/定义芯片链虚线连接 + 底部两行图例）。

### 构建（MSYS2 ucrt64 工具链，Qt ≥ 6.8）

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-tools \
    mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja
cd pinout
cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=E:/dev-tools/compilers/msy2/ucrt64
cmake --build build
ctest --test-dir build    # 离屏单测（project/layout/exporter 三组）
```

### 运行

```bash
PATH="E:/dev-tools/compilers/msy2/ucrt64/bin:$PATH" ./build/pinout.exe [工程.pinout.json]
# 无参数先弹起始页（新建/打开/最近工程）
```

**双击直启（独立分发）**：需先把运行库部署到 exe 旁，否则会从 PATH 翻到不兼容副本报
「无法定位程序输入点」：

```bash
<ucrt64>/bin/windeployqt --release --compiler-runtime build/pinout.exe
# MinGW 版 windeployqt 不复制编译器运行库，需再手动拷贝 ucrt64/bin 下的：
#   libstdc++-6.dll  libgcc_s_seh-1.dll  libwinpthread-1.dll   （zlib1.dll 已随 Qt 部署）
```

### CLI 导出

```bash
./build/pinout.exe --export 工程.pinout.json [输出.png] [倍率]   # 与 GUI 共用 exporter，默认 <工程名>.png、2x
```

## 许可证

[MIT](LICENSE) © 2026 Qingc。

## 参与贡献

反馈问题、提交 PR 与运行单测见 [CONTRIBUTING.md](CONTRIBUTING.md)。
