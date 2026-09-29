#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""配置生成器——输入解析与派生（configgen，ADR-020 迭代新增工具）。

职责：从现有芯片清单（chips/<id>.json）派生新芯片支持包（CSP）骨架的输入侧——
加载基线芯片、应用差异覆盖项、做几何/一致性校验（与固件仓 chips/test_chip.py
及 bl_storage 运行期几何自检同源的规则），产出内部规格 dict 供 generator.py 渲染。

与 chipfill.py 的分工：chipfill 消费**成品**清单做模板填充；本工具生成**骨架**清单
（device 的 flash_driver/register/sfd、F4 的 startup 文件名与 c_defines 等芯片事实
无法派生，以 TODO 占位并在 _note 中标注，移植者补全前 chipfill 会拒绝——强制人工
核对，杜绝静默错误）。

本模块不含 IO 写入；渲染与落盘见 generator.py，GUI 见 gui.py（共用本模块，零业务重复）。
"""

from __future__ import annotations

import copy
import json
from pathlib import Path


class ConfigGenError(Exception):
    """输入不合法或派生结果自相矛盾。"""


SUPPORTED_FAMILIES = ("stm32f1", "stm32f4")
_CPUTYPE_DEBUG_ARGS = {
    "Cortex-M3": "-pCM3",
    "Cortex-M0": "-pCM0",
    "Cortex-M0+": "-pCM0P",
    "Cortex-M4": "-pCM4",
    "Cortex-M7": "-pCM7",
}


def _hex(value, width=8) -> str:
    """接受 int / '0x…' / 十进制字符串，归一化为 0x 前缀十六进制。"""
    if isinstance(value, int):
        v = value
    else:
        s = str(value).strip().replace("_", "")
        v = int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    if v < 0:
        raise ConfigGenError(f"数值不可为负: {value}")
    return f"0x{v:0{width}X}"


def _hex_min(value) -> str:
    return _hex(value, width=1)


def _to_int(value, what: str) -> int:
    try:
        if isinstance(value, int):
            return value
        s = str(value).strip().replace("_", "")
        return int(s, 16) if s.lower().startswith("0x") else int(s, 10)
    except (TypeError, ValueError):
        raise ConfigGenError(f"{what} 不是合法数值: {value!r}")


def load_base_chip(root: Path, from_id: str) -> dict:
    """读取基线芯片清单（浅校验），失败抛 ConfigGenError。"""
    path = Path(root) / "chips" / f"{from_id}.json"
    if not path.is_file():
        raise ConfigGenError(f"基线芯片清单不存在: {path}")
    try:
        chip = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        raise ConfigGenError(f"基线清单不是合法 JSON: {path}（{e}）")
    for key in ("id", "family", "device", "build", "memory", "partitions",
                "erase_units", "clock", "iwdg", "sysmem"):
        if key not in chip:
            raise ConfigGenError(f"基线清单缺少必要段 '{key}': {path}")
    return chip


def _derive_partitions(base_parts, o) -> dict:
    """分区段：基线为底、覆盖项生效；校验范围/无重叠/BL 位于 Flash 基址/参数双副本。"""
    flash_base = _to_int(o["flash_base"], "flash_base")
    flash_size = _to_int(o["flash_size"], "flash_size")
    bl_base = _to_int(o["bl_base"], "bl_base")
    bl_size = _to_int(o["bl_size"], "bl_size")
    app_base = _to_int(o["app_base"], "app_base")
    app_size = _to_int(o["app_size"], "app_size")
    param_base = _to_int(o["param_base"], "param_base")
    param_size = _to_int(o["param_size"], "param_size")

    for name, (a, s) in {
        "bootloader": (bl_base, bl_size), "app": (app_base, app_size),
        "params": (param_base, param_size),
    }.items():
        if s <= 0:
            raise ConfigGenError(f"{name} 分区大小必须为正: {s}")
        if a < flash_base or a + s > flash_base + flash_size:
            raise ConfigGenError(f"{name} 分区 [{_hex(a)},{_hex(a + s)}) 超出 "
                                 f"Flash [{_hex(flash_base)},{_hex(flash_base + flash_size)})")
    if bl_base != flash_base:
        raise ConfigGenError(f"bootloader 必须从 Flash 基址开始（{_hex(bl_base)} != {_hex(flash_base)}）")
    regions = [("bootloader", bl_base, bl_size), ("app", app_base, app_size),
               ("params", param_base, param_size)]
    for i in range(len(regions)):
        for j in range(i + 1, len(regions)):
            _, a1, s1 = regions[i]
            _, a2, s2 = regions[j]
            if a1 < a2 + s2 and a2 < a1 + s1:
                raise ConfigGenError(f"分区重叠: {regions[i][0]} 与 {regions[j][0]}")
    if param_size % 2:
        raise ConfigGenError(f"参数区大小必须能平分为双副本: {_hex(param_size)}")
    copy_size = param_size // 2
    return {
        "bootloader": {"base": _hex(bl_base), "size": _hex(bl_size)},
        "app": {"base": _hex(app_base), "size": _hex(app_size)},
        "params": {
            "base": _hex(param_base), "size": _hex(param_size),
            "copies": [
                {"base": _hex(param_base), "size": _hex(copy_size)},
                {"base": _hex(param_base + copy_size), "size": _hex(copy_size)},
            ],
        },
    }


def _check_app_on_unit_bounds(flash_base: int, app_base: int, app_size: int, units) -> None:
    """APP 首末边界必须与擦除单元边界精确重合（与 bl_storage_init 运行期自检同源）。
    单元边界自 Flash 基址起累计，与传入的 APP 绝对地址对齐比较。"""
    cursor = 0
    bounds = {cursor}
    for size, count in units:
        for _ in range(count):
            cursor += size
            bounds.add(cursor)
    rel_base = app_base - flash_base
    rel_end = app_base + app_size - flash_base
    if rel_base not in bounds or rel_end not in bounds:
        raise ConfigGenError(
            f"APP 边界（{_hex(app_base)}/{_hex(app_base + app_size)}）未与擦除单元边界重合——"
            f"启用签名的 F4 整单元擦除会波及邻区，固件几何自检会拒绝该清单")


def _derive_erase_units(base_eu, o) -> dict:
    """擦除单元段：uniform 由 unit_size/count 派生并要求整除；table 逐项求和校验。"""
    flash_size = _to_int(o["flash_size"], "flash_size")
    if o["erase_mode"] == "uniform":
        unit_size = _to_int(o["erase_unit_size"], "erase_unit_size")
        count = _to_int(o["erase_unit_count"], "erase_unit_count")
        if unit_size <= 0 or count <= 0:
            raise ConfigGenError("uniform 擦除单元 size/count 必须为正")
        if unit_size * count != flash_size:
            raise ConfigGenError(f"uniform 单元表未拼满 Flash：{_hex_min(unit_size)}×{count}"
                                 f" != {_hex(flash_size)}")
        units = [(unit_size, count)]
        return {"uniform": True, "base": _hex(o["flash_base"]),
                "unit_size": _hex_min(unit_size), "count": count}
    # 显式表
    table = o["erase_table"]
    if not table:
        raise ConfigGenError("erase-mode=table 需要 --erase-table（JSON 数组或文件路径）")
    units = []
    covered = 0
    for i, item in enumerate(table):
        size = _to_int(item.get("size"), f"erase_table[{i}].size")
        count = _to_int(item.get("count"), f"erase_table[{i}].count")
        ms = _to_int(item.get("typical_erase_ms", 0), f"erase_table[{i}].typical_erase_ms")
        if size <= 0 or count <= 0:
            raise ConfigGenError(f"erase_table[{i}] size/count 必须为正")
        covered += size * count
        units.append((size, count))
        out_item = {"size": _hex_min(size), "count": count}
        if ms:
            out_item["typical_erase_ms"] = ms
        table[i] = out_item
    if covered != flash_size:
        raise ConfigGenError(f"显式单元表未拼满 Flash：合计 {_hex(covered)} != {_hex(flash_size)}")
    return {"uniform": False, "base": _hex(o["flash_base"]), "units": table}


def build_spec(base: dict, o: dict) -> dict:
    """基线 + 覆盖项 → 内部规格（不落盘）。o 的键见 generator.main 的参数表。"""
    spec = {}
    spec["id"] = o["id"]
    spec["family"] = o["family"]
    if spec["family"] not in SUPPORTED_FAMILIES:
        raise ConfigGenError(f"暂不支持的家族: {spec['family']}（{SUPPORTED_FAMILIES}）")
    cputype = o["cputype"]
    if cputype not in _CPUTYPE_DEBUG_ARGS:
        raise ConfigGenError(f"未知内核 {cputype}（支持: {sorted(_CPUTYPE_DEBUG_ARGS)}）")

    spec["device"] = {
        "name": o["device_name"],
        "vendor": "STMicroelectronics",
        "pack_id": o["pack_id"],
        "cputype": cputype,
        "cpu_clock": f"CLOCK({_to_int(o['hse_mhz'], 'hse_mhz') * 1_000_000})",
        "endianness": "ELITTLE",
        "debug_dll_args": _CPUTYPE_DEBUG_ARGS[cputype],
        # 芯片事实无法派生：TODO 占位（chipfill 对占位值同样可用，但生成的工程
        # 无法烧录——_note 中强制提示，移植者必须替换）
        "flash_driver": "TODO_FLASH_DRIVER（从 Keil DFP 的 FLM 描述拷贝）",
        "register_file": "TODO_REGISTER_FILE（$$Device:<名>$Device\\Include\\<头>）",
        "sfd_file": "TODO_SFD_FILE（$$Device:<名>$SVD\\<svd>）",
    }
    if o.get("flash_driver"):
        spec["device"]["flash_driver"] = o["flash_driver"]
    if o.get("register_file"):
        spec["device"]["register_file"] = o["register_file"]
    if o.get("sfd_file"):
        spec["device"]["sfd_file"] = o["sfd_file"]

    parts = _derive_partitions(base["partitions"], o)
    spec["memory"] = {
        "flash_base": _hex(o["flash_base"]), "flash_size": _hex(o["flash_size"]),
        "sram_base": _hex(o["sram_base"]), "sram_size": _hex(o["sram_size"]),
    }
    spec["partitions"] = parts

    eu = _derive_erase_units(base["erase_units"], o)
    units = ([( _to_int(eu["unit_size"], "unit_size"), eu["count"])] if eu["uniform"]
             else [(_to_int(u["size"], "unit size"), u["count"]) for u in eu["units"]])
    _check_app_on_unit_bounds(_to_int(o["flash_base"], "flash_base"),
                              _to_int(o["app_base"], "app_base"),
                              _to_int(o["app_size"], "app_size"), units)
    spec["erase_units"] = eu

    spec["clock"] = {"hse_mhz": _to_int(o["hse_mhz"], "hse_mhz"),
                     "target_hz": _to_int(o["target_hz"], "target_hz"),
                     "hsi_mhz": _to_int(o["hsi_mhz"], "hsi_mhz"),
                     "wait_states": _to_int(o["wait_states"], "wait_states")}
    spec["iwdg"] = {"normal_ms": _to_int(o["iwdg_normal_ms"], "iwdg_normal_ms"),
                    "upgrade_relaxed_ms": _to_int(o["iwdg_upgrade_ms"], "iwdg_upgrade_ms")}
    pins = {"uart_tx": o["uart_tx"], "uart_rx": o["uart_rx"],
            "led": o["led"], "led_active_low": True}
    if o.get("with_bt"):
        pins.update({"uart2_tx": o.get("uart2_tx", "PA2"), "uart2_rx": o.get("uart2_rx", "PA3"),
                     "bt_state": o.get("bt_state", "PB0"), "bt_en": o.get("bt_en", "PB1")})
    if o.get("with_oled"):
        pins.update({"i2c_scl": o.get("i2c_scl", "PB8"), "i2c_sda": o.get("i2c_sda", "PB9")})
    spec["pins"] = pins
    spec["sysmem"] = {"uid_addr": _hex(o["uid_addr"]), "flsize_addr": _hex(o["flsize_addr"])}
    return spec


def defaults_from(base: dict, o: dict) -> dict:
    """把基线芯片的现值填进覆盖项字典中仍为 None 的键（--from 派生的语义核心），
    并换掉必须变化的身份字段（id/分区地址保持基线值由用户显式改）。"""
    o = dict(o)
    mem, parts, clk, iwdg = base["memory"], base["partitions"], base["clock"], base["iwdg"]
    dev = base["device"]
    fill = {
        "family": base["family"], "device_name": dev["name"], "pack_id": dev["pack_id"],
        "cputype": dev["cputype"],
        "flash_base": mem["flash_base"], "flash_size": mem["flash_size"],
        "sram_base": mem["sram_base"], "sram_size": mem["sram_size"],
        "bl_base": parts["bootloader"]["base"], "bl_size": parts["bootloader"]["size"],
        "app_base": parts["app"]["base"], "app_size": parts["app"]["size"],
        "param_base": parts["params"]["base"], "param_size": parts["params"]["size"],
        "hse_mhz": clk["hse_mhz"], "target_hz": clk["target_hz"],
        "hsi_mhz": clk["hsi_mhz"], "wait_states": clk["wait_states"],
        "iwdg_normal_ms": iwdg["normal_ms"], "iwdg_upgrade_ms": iwdg["upgrade_relaxed_ms"],
        "uart_tx": base["pins"].get("uart_tx"), "uart_rx": base["pins"].get("uart_rx"),
        "led": base["pins"].get("led"),
        "uid_addr": base["sysmem"]["uid_addr"], "flsize_addr": base["sysmem"]["flsize_addr"],
    }
    for k, v in fill.items():
        if o.get(k) in (None, ""):
            o[k] = v
    if not o.get("erase_mode"):
        o["erase_mode"] = "uniform" if base["erase_units"].get("uniform") else "table"
    if o["erase_mode"] == "uniform":
        if o.get("erase_unit_size") in (None, ""):
            o["erase_unit_size"] = base["erase_units"].get("unit_size")
        if o.get("erase_unit_count") in (None, ""):
            o["erase_unit_count"] = base["erase_units"].get("count")
    else:
        if o.get("erase_table") in (None, ""):
            o["erase_table"] = copy.deepcopy(base["erase_units"].get("units", []))
    o["with_bt"] = bool(o.get("with_bt"))
    o["with_oled"] = bool(o.get("with_oled"))
    return o
