# 更新日志（Changelog）

本项目的所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

## [1.2.0] - 2026-10-03

### Added

- **pinout/ 开发板引脚定义图制作工具（C++/Qt6 Widgets，仓内首个大工具）**：
  工程化管理——每个任务一个 `.pinout.json` 工程，新建时导入开发板图片，进入工程后板图
  居中显示于蓝图网格画布（滚轮缩放/中键平移）。五个标注工具：单pin引脚（圆形标记，
  十字虚线跟随鼠标、交点即圆心）、多pin引脚（单击锚点后向上/下等距延伸，Ctrl+滚轮
  实时调间距，确认后批量命名）、单pin排针（方形）、多pin排针、追加复用定义（项目级
  复用定义库自动收录输入过的定义，点击引脚内联补全追加；支持框选多引脚后在属性面板
  批量追加）。右侧属性面板：名称/侧别/名称分类/定义链编辑，名称按 PA/PB/PC/GND/电源
  自动猜分类配色；分类色板与复用库可在管理对话框增删改色。Ctrl+E 导出高清 PNG 海报
  （板图居中 + 名称/定义芯片链虚线连接 + 底部两行图例，2x 超采样）。工程 JSON 经
  QSaveFile 原子写入，覆盖前自动 `.bak-时间戳` 备份。Qt Test 离屏单测
  （`tests/test_project.cpp` / `test_layout.cpp` / `test_exporter.cpp`），
  经 MSYS2 ucrt64 工具链 CMake+Ninja 构建即可复现。

### Notes

- 仓规修订：工具默认纯标准库 Python；**大工具允许独立技术栈与第三方 UI 依赖**
  （`pinout/` 使用 C++20 + Qt6，运行依赖见 README 构建一节）。

## [1.1.0] - 2026-09-30

### Added

- **configgen/ 配置生成器（CLI + GUI）**：CSP 引导工具——从现有芯片派生新芯片
  `chips/<id>.json` + `port/<family>/<id>/board_config.h` 骨架。基线派生 + 差异项覆盖
  （分区/擦除单元/时钟/IWDG/引脚/服务开关）；几何校验与固件 `bl_storage` 运行期自检
  同源（分区不出界不重叠、均匀表整除拼满、显式表求和、APP 首末边界与擦除单元重合）；
  芯片事实不可派生字段（DFP flash_driver/register/sfd、F4 startup 与器件宏）写 TODO
  占位强制人工核对，补全前不入构建链。写入前自动备份（`--no-backup`）、`--dry-run`
  预览；CLI 与 tkinter GUI 同源业务函数（GUI 零业务逻辑）。单测 15 项
  （`configgen/test_configgen.py`，含与 chipfill.load_chip 的跨仓闭环）。

## [1.0.0] - 2026-09-27

首个公开发布版本。

### Added

- **uvprojx/parser.py**：Keil `.uvprojx` → JSON 规格解析（target/工具链/device/输出/
  宏定义/include/scatter/源文件分组；安全约束：拒绝含 DTD 的 XML、限制输入大小）
- **uvprojx/generator.py**：JSON 规格 → `.uvprojx`（创建/更新双模式；设备字段数据驱动、
  无设备名硬编码；写入前自校验、自动备份、更新模式保留未知字段）
- **uvprojx/chipfill.py**：CSP 芯片清单填充（`chip.json` + spec/sct 模板 → spec 与
  scatter，配合 LiteBootLoader 多芯片基础设施 ADR-015）
- **ico/parser.py**：ICO 结构校验解析（头 `00 00 01 00`、目录边界、PNG / BMP-DIB 识别）
- **ico/generator.py**：PNG → ICO 生成（Pillow 可选缩放为 32bpp BMP-DIB，无 Pillow 回退
  PNG 直嵌；写入前自校验、自动备份）
- **单测**：`test_uvprojx.py`（9 项）、`test_ico.py`（9 项），纯标准库直接运行

### Notes

- 服务于 [LiteBootLoader](https://github.com/Liu-bit264/LiteBootLoader) 的 CSP 构建流程；
  chip.json 字段约定见其 `docs/dev/design.md` ADR-015 与 `docs/porting_guide.md`
