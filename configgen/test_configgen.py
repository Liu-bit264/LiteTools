#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""configgen 单元测试（unittest，直接 `python test_configgen.py` 运行）。

覆盖：基线派生、分区/擦除单元几何校验（与固件 bl_storage 自检同源规则）、
双产物渲染、备份与 dry-run 行为、CLI 退出码、与 chipfill 的跨仓闭环、GUI 可导入。
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(name: str):
    spec = importlib.util.spec_from_file_location(f"configgen_{name}", _HERE / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


parser_mod = _load("parser")
generator_mod = _load("generator")
ConfigGenError = parser_mod.ConfigGenError

# 与 LiteBootLoader/chips/f103c8t6.json 同构的最小基线（单测不跨仓依赖）
BASELINE_F103 = {
    "id": "f103c8t6", "family": "stm32f1",
    "device": {"name": "STM32F103C8", "vendor": "STMicroelectronics",
               "pack_id": "Keil.STM32F1xx_DFP.2.4.1", "cputype": "Cortex-M3",
               "cpu_clock": "CLOCK(8000000)", "endianness": "ELITTLE",
               "debug_dll_args": "-pCM3"},
    "build": {"c_defines": ["STM32F10X_MD"], "port_dir": "port\\stm32f1\\f103c8t6"},
    "memory": {"flash_base": "0x08000000", "flash_size": "0x00010000",
               "sram_base": "0x20000000", "sram_size": "0x00005000"},
    "partitions": {
        "bootloader": {"base": "0x08000000", "size": "0x00004000"},
        "app": {"base": "0x08004000", "size": "0x0000B800"},
        "params": {"base": "0x0800F800", "size": "0x00000800",
                   "copies": [{"base": "0x0800F800", "size": "0x00000400"},
                              {"base": "0x0800FC00", "size": "0x00000400"}]}},
    "erase_units": {"uniform": True, "base": "0x08000000",
                    "unit_size": "0x400", "count": 64, "typical_erase_ms": 4},
    "clock": {"hse_mhz": 8, "target_hz": 72000000, "hsi_mhz": 8, "wait_states": 2},
    "pins": {"uart_tx": "PA9", "uart_rx": "PA10", "led": "PC13", "led_active_low": True},
    "iwdg": {"normal_ms": 2000, "upgrade_relaxed_ms": 2000},
    "sysmem": {"uid_addr": "0x1FFFF7E8", "flsize_addr": "0x1FFFF7E0"},
}

_LBL_ROOT = Path(__file__).resolve().parents[2] / "LiteBootLoader"


def _fixture_root(tmp: Path) -> Path:
    (tmp / "chips").mkdir(parents=True)
    (tmp / "chips" / "f103c8t6.json").write_text(
        json.dumps(BASELINE_F103), encoding="utf-8")
    return tmp


def _overrides(tmp: Path, **kw) -> dict:
    base = parser_mod.load_base_chip(tmp, "f103c8t6")
    o = parser_mod.defaults_from(base, {"id": "f103rc", **kw})
    return base, o


class DeriveTest(unittest.TestCase):
    def test_derive_only_id_change_roundtrip(self):
        """--from 派生：只改 id，其余继承基线 → 规格与基线数值一致。"""
        with tempfile.TemporaryDirectory() as td:
            tmp = _fixture_root(Path(td))
            base, o = _overrides(tmp, id="f103rc")
            spec = parser_mod.build_spec(base, o)
            self.assertEqual(spec["id"], "f103rc")
            self.assertEqual(spec["family"], "stm32f1")
            self.assertEqual(spec["partitions"], BASELINE_F103["partitions"])
            self.assertEqual(spec["erase_units"]["uniform"], True)

    def test_partitions_out_of_flash_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)), app_size="0x0000C800")
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, o)

    def test_partitions_overlap_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)), app_base="0x0800F000")
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, o)

    def test_bl_not_at_flash_base_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)), bl_base="0x08000100")
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, o)

    def test_uniform_must_tile_flash(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)), erase_unit_count=63)
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, o)

    def test_table_sum_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)),
                                 erase_mode="table",
                                 erase_table=[{"size": "0x400", "count": 10}])
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, o)

    def test_app_bounds_on_unit_boundary_f4_style(self):
        """F4 风格：APP 基址落在 16K 扇区 4 边界 → 通过；落到扇区中段 → 拒绝
        （与 bl_storage 几何自检同源）。"""
        f4_table = [{"size": "0x4000", "count": 4}, {"size": "0x10000", "count": 1},
                    {"size": "0x20000", "count": 3}]
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)),
                                 family="stm32f4", cputype="Cortex-M4",
                                 flash_size="0x00080000", sram_size="0x00020000",
                                 bl_size="0x00008000",
                                 param_base="0x08008000", param_size="0x00008000",
                                 app_base="0x08010000", app_size="0x00070000",
                                 erase_mode="table", erase_table=f4_table)
            spec = parser_mod.build_spec(base, o)          # 边界重合：通过
            self.assertEqual(spec["partitions"]["app"]["base"], "0x08010000")
            bad = dict(o, app_base="0x08018000")
            with self.assertRaises(ConfigGenError):
                parser_mod.build_spec(base, bad)           # 扇区中段：拒绝


class RenderTest(unittest.TestCase):
    def test_render_board_config_uniform_contains_derived_macros(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)))
            spec = parser_mod.build_spec(base, o)
            text = generator_mod.render_board_config_h(spec)
            self.assertIn("#define BL_APP_SIZE            0x0000B800u", text)
            self.assertIn("#define BL_ERASE_UNITS_UNIFORM  1u", text)
            self.assertIn('#define BL_CHIP_NAME           "F103RC"', text)

    def test_render_board_config_f4_table_expanded(self):
        f4_table = [{"size": "0x4000", "count": 4}, {"size": "0x10000", "count": 1},
                    {"size": "0x20000", "count": 3}]
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)),
                                 family="stm32f4", cputype="Cortex-M4",
                                 flash_size="0x00080000", sram_size="0x00020000",
                                 bl_size="0x00008000",
                                 param_base="0x08008000", param_size="0x00008000",
                                 app_base="0x08010000", app_size="0x00070000",
                                 erase_mode="table", erase_table=f4_table)
            spec = parser_mod.build_spec(base, o)
            text = generator_mod.render_board_config_h(spec)
            self.assertIn("#define BL_ERASE_UNITS_UNIFORM  0u", text)
            self.assertIn("{ 0x08010000u, 0x10000u }", text)   # {基址,大小} 展开形态
            self.assertIn("{ 0x08060000u, 0x20000u }", text)
            self.assertIn("8u     /* APP 覆盖单元数上界", text)

    def test_render_chip_json_schema_shape(self):
        with tempfile.TemporaryDirectory() as td:
            base, o = _overrides(_fixture_root(Path(td)), with_bt=True, with_oled=True)
            spec = parser_mod.build_spec(base, o)
            chip = json.loads(generator_mod.render_chip_json(spec))
            for key in ("id", "family", "device", "build", "memory", "partitions",
                        "erase_units", "clock", "pins", "iwdg", "sysmem"):
                self.assertIn(key, chip)
            build = chip["build"]
            self.assertIn("uart2.c", [f["name"] for f in build["port_files_bl"]])
            self.assertIn("i2c.c", [f["name"] for f in build["port_files_app"]])
            self.assertEqual(build["artifact_dir"], "chips\\f103rc")   # 分槽位
            self.assertIn("TODO", build["device"]["flash_driver"] if "device" in build
                          else json.dumps(build))


class WriteAndCliTest(unittest.TestCase):
    def test_write_outputs_backup(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = _fixture_root(Path(td))
            base, o = _overrides(tmp)
            spec = parser_mod.build_spec(base, o)
            out_json = tmp / "chips" / "f103rc.json"
            out_json.write_text("OLD", encoding="utf-8")
            generator_mod.write_outputs(spec, out_json, tmp / "h" / "board_config.h")
            text = out_json.read_text(encoding="utf-8")
            self.assertIn('"f103rc"', text)
            baks = list((tmp / "chips").glob("f103rc.json.bak-*"))
            self.assertEqual(len(baks), 1)
            self.assertEqual(baks[0].read_text(encoding="utf-8"), "OLD")

    def test_cli_dry_run_writes_nothing(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = _fixture_root(Path(td))
            rc = generator_mod.main(["--from", "f103c8t6", "--id", "f103rc",
                                     "--root", str(tmp), "--dry-run"])
            self.assertEqual(rc, 0)
            self.assertFalse((tmp / "chips" / "f103rc.json").exists())

    def test_cli_ok_and_exit_codes(self):
        with tempfile.TemporaryDirectory() as td:
            tmp = _fixture_root(Path(td))
            rc = generator_mod.main(["--from", "f103c8t6", "--id", "f103rc",
                                     "--root", str(tmp)])
            self.assertEqual(rc, 0)
            self.assertTrue((tmp / "chips" / "f103rc.json").exists())
            self.assertTrue((tmp / "port" / "stm32f1" / "f103rc" / "board_config.h").exists())
            rc = generator_mod.main(["--from", "no_such_chip", "--id", "x",
                                     "--root", str(tmp)])
            self.assertEqual(rc, 1)
            with self.assertRaises(SystemExit) as cm:
                generator_mod.main(["--from", "f103c8t6"])   # 缺 --id
            self.assertEqual(cm.exception.code, 2)

    def test_gui_module_importable(self):
        """tkinter 表单模块可导入（含依赖面）；不启动 mainloop。"""
        gui = _load("gui")
        self.assertTrue(hasattr(gui, "App"))
        self.assertTrue(callable(gui.main))


class ChipfillIntegrationTest(unittest.TestCase):
    """跨仓闭环：生成的清单通过 chipfill.load_chip（LiteBootLoader 在邻位时执行）。"""

    def test_generated_json_passes_chipfill(self):
        if not (_LBL_ROOT / "chips" / "f103c8t6.json").is_file():
            self.skipTest("LiteBootLoader 不在邻位")
        spec_path = _HERE.parent / "uvprojx" / "chipfill.py"
        cf = importlib.util.spec_from_file_location("cf_chipfill", spec_path)
        chipfill = importlib.util.module_from_spec(cf)
        cf.loader.exec_module(chipfill)
        with tempfile.TemporaryDirectory() as td:
            tmp = _fixture_root(Path(td))
            rc = generator_mod.main(["--from", "f103c8t6", "--id", "f103rc",
                                     "--root", str(tmp)])
            self.assertEqual(rc, 0)
            chip = chipfill.load_chip(tmp / "chips" / "f103rc.json")
            self.assertEqual(chip["id"], "f103rc")
            self.assertIn("derived", chip)


def main():
    unittest.main(verbosity=2)


if __name__ == "__main__":
    main()
