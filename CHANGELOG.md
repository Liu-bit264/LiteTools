# 更新日志（Changelog）

本项目的所有显著变更记录于此。格式基于 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，版本号遵循 [SemVer 2.0.0](https://semver.org/lang/zh-CN/)。

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
