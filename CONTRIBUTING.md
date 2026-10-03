# 贡献指南（CONTRIBUTING）

感谢关注 LiteTools！本仓是
[LiteBootLoader](https://github.com/Liu-bit264/LiteBootLoader)（固件仓）家族的通用
工程工具集（Keil 工程解析/生成、CSP 清单填充、ICO 解析/生成、开发板引脚定义图制作）。
工具用法见 [README.md](README.md)。

## 反馈 Bug

提交 Issue 时请尽量附上：

1. 触发问题的输入文件（`.uvprojx` / `.ico` / `chip.json`，脱敏后）
2. 完整命令行与输出
3. 期望行为与实际行为

## 仓库结构

```text
LiteTools/
├── uvprojx/
│   ├── parser.py          Keil .uvprojx 解析器（工程 → JSON 规格）
│   ├── generator.py       Keil .uvprojx 生成器（JSON 规格 → 工程，含更新模式）
│   ├── chipfill.py        CSP 芯片清单填充器（chip.json + 模板 → spec/sct，ADR-015）
│   ├── test_uvprojx.py    解析/生成往返等单测
│   └── templates/         sct 通用模板（纯占位符，随工具分发）
├── ico/
    ├── parser.py          ICO 解析器（结构校验 + 格式识别）
    ├── generator.py       ICO 生成器（PNG → ICO，Pillow 可选）
    └── test_ico.py        解析/生成单测
└── configgen/
    ├── parser.py          CSP 派生输入解析（基线派生 + 差异覆盖 + 几何校验）
    ├── generator.py       CSP 骨架渲染（chips/<id>.json + board_config.h，备份/dry-run）
    ├── gui.py             tkinter 表单（与 CLI 同源业务函数，零业务逻辑）
    └── test_configgen.py  派生/渲染/备份/退出码 + chipfill 闭环单测
└── pinout/                C++/Qt6 大工具（CMake 构建，build/ 已忽略）
    ├── CMakeLists.txt     qt_add_executable + qt_standard_project_setup + ctest 目标
    ├── src/               工程模型(project)/布局(layout)/导出(exporter)/
    │                      起始页(startpage)/主窗口(mainwindow)/画布(boardscene,
    │                      boardview)/图元(pinitem)/属性面板(propspanel)/库管理(librarydlg)
    └── tests/             Qt Test 离屏单测（project/layout/exporter）
```

## 开发环境与测试

- 工具默认纯标准库 Python 实现；Pillow 为 ICO 生成器的可选依赖（自动缩放用）。
  **大工具允许独立技术栈与第三方 UI 依赖**：`pinout/` 使用 C++20 + Qt6（≥6.8），
  经 MSYS2 ucrt64 工具链构建（`pacman -S --needed mingw-w64-ucrt-x86_64-qt6-base
  mingw-w64-ucrt-x86_64-qt6-tools mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja`）
- Python 单测直接运行（无需 pytest）：

  ```bash
  cd uvprojx && uv run --python 3.12 python test_uvprojx.py
  cd ico && uv run --python 3.12 python test_ico.py
  cd configgen && uv run --python 3.12 python test_configgen.py
  ```

- pinout 单测（CMake + ctest，离屏）：

  ```bash
  cd pinout && cmake -B build -G Ninja -DCMAKE_PREFIX_PATH=<ucrt64 前缀> \
      && cmake --build build && ctest --test-dir build
  ```

## 提交 PR

1. 提交信息遵循 **Conventional Commits 1.0.0**；版本遵循 **SemVer 2.0.0**，
   发版时更新 CHANGELOG.md
2. 改动解析/生成行为必须补对应单测（往返一致性、边界校验、恶意输入拒绝）
3. 涉及 `chipfill.py` 的 chip.json schema 或模板约定变更时，需与固件仓联动回归：
   chipfill 的契约回归由固件仓 `chips/test_chip.py` 承担（内含 chipfill 往返与异常用例，
   覆盖 `chips/*.json` 与已提交 spec/sct 的逐字节比对）；本仓 `test_uvprojx.py` 覆盖
   parser / generator，改动这两者时须通过
4. 生成器输出（`.uvprojx` / `.sct`）的格式变更请在 PR 中说明对既有产物的影响；
   生成结果需在 Keil uVision 中人工验证后才能作为交付依据

## 与固件仓的联动

- `chipfill.py` 消费固件仓 `chips/<id>.json`（schema 见
  [LiteBootLoader docs/dev/design.md](https://github.com/Liu-bit264/LiteBootLoader/blob/main/docs/dev/design.md)
  ADR-015）与 `chips/templates/` 模板
- 固件仓 schema/模板变更后，本仓与固件仓的一致性测试需双向通过
