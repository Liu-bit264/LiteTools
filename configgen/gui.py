#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""配置生成器 GUI（tkinter，仓内首个图形工具；ADR-020 迭代新增）。

与 CLI（generator.py main）共用 parser/generator 全部业务逻辑——本文件只做
表单布局与事件转发，零业务逻辑（分层先例：LiteBootUpgrader GUI）。

用法：
  uv run --python 3.12 ../LiteTools/configgen/gui.py
退出码：0 正常退出；1 生成失败（错误展示在日志区，不弹栈回溯）。
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

_HERE = Path(__file__).resolve().parent


def _load(name: str):
    spec = importlib.util.spec_from_file_location(f"configgen_{name}", _HERE / f"{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


parser_mod = _load("parser")
generator_mod = _load("generator")

# 表单字段定义：(键, 标签, 默认提示)——与 CLI 参数一一对应
_FIELDS = [
    ("family", "家族 family", "stm32f1 | stm32f4"),
    ("device_name", "器件名 device.name", "STM32F103RC"),
    ("pack_id", "DFP pack_id", "Keil.STM32F1xx_DFP.2.4.1"),
    ("cputype", "内核 cputype", "Cortex-M3"),
    ("flash_base", "Flash 基址", "0x08000000"),
    ("flash_size", "Flash 大小", "0x00010000"),
    ("sram_base", "SRAM 基址", "0x20000000"),
    ("sram_size", "SRAM 大小", "0x00005000"),
    ("bl_base", "BL 分区基址", "0x08000000"),
    ("bl_size", "BL 分区大小", "0x00004000"),
    ("app_base", "APP 分区基址", "0x08004000"),
    ("app_size", "APP 分区大小", "0x0000B800"),
    ("param_base", "参数区基址", "0x0800F800"),
    ("param_size", "参数区大小（=2×副本）", "0x00000800"),
    ("erase_mode", "擦除单元模式", "uniform | table"),
    ("erase_unit_size", "均匀单元大小", "0x400"),
    ("erase_unit_count", "均匀单元数量", "64"),
    ("hse_mhz", "HSE MHz", "8"),
    ("target_hz", "目标主频 Hz", "72000000"),
    ("hsi_mhz", "HSI MHz", "8"),
    ("wait_states", "等待周期", "2"),
    ("iwdg_normal_ms", "IWDG 常规 ms", "2000"),
    ("iwdg_upgrade_ms", "IWDG 放宽 ms", "2000"),
    ("uart_tx", "升级串口 TX", "PA9"),
    ("uart_rx", "升级串口 RX", "PA10"),
    ("led", "LED 引脚", "PC13"),
    ("uid_addr", "UID 地址", "0x1FFFF7E8"),
    ("flsize_addr", "FlashSize 地址", "0x1FFFF7E0"),
]
_BOOL_FIELDS = [("with_bt", "含蓝牙通道（uart2/STATE/EN）"),
                ("with_oled", "含 OLED（i2c/SSD1306）")]


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("LiteTools 配置生成器（CSP 骨架）")
        self.vars: dict = {}
        self.bools: dict = {}

        top = ttk.Frame(root, padding=6)
        top.pack(fill="x")
        ttk.Label(top, text="固件仓根(--root)").pack(side="left")
        self.root_var = tk.StringVar(value=".")
        ttk.Entry(top, textvariable=self.root_var, width=48).pack(side="left", padx=4)
        ttk.Button(top, text="扫描基线", command=self.reload_bases).pack(side="left")
        ttk.Label(top, text="基线芯片(--from)").pack(side="left", padx=(8, 0))
        self.base_var = tk.StringVar()
        self.base_combo = ttk.Combobox(top, textvariable=self.base_var, width=18,
                                       state="readonly")
        self.base_combo.pack(side="left", padx=4)
        self.base_combo.bind("<<ComboboxSelected>>", lambda _e: self.prefill())
        ttk.Label(top, text="新芯片 id(--id)").pack(side="left", padx=(8, 0))
        self.id_var = tk.StringVar()
        ttk.Entry(top, textvariable=self.id_var, width=16).pack(side="left", padx=4)

        body = ttk.Frame(root, padding=(6, 0))
        body.pack(fill="both", expand=True)
        left = ttk.Frame(body)
        left.pack(side="left", fill="both", expand=True)
        grid = ttk.Frame(left)
        grid.pack(fill="both", expand=True)
        for i, (key, label, hint) in enumerate(_FIELDS):
            ttk.Label(grid, text=label).grid(row=i, column=0, sticky="e", pady=1)
            var = tk.StringVar()
            ttk.Entry(grid, textvariable=var, width=34).grid(row=i, column=1,
                                                             sticky="w", pady=1)
            ttk.Label(grid, text=hint, foreground="#888").grid(row=i, column=2,
                                                               sticky="w", padx=4)
            self.vars[key] = var
        for i, (key, label) in enumerate(_BOOL_FIELDS):
            self.bools[key] = tk.BooleanVar(value=False)
            ttk.Checkbutton(grid, text=label, variable=self.bools[key]).grid(
                row=len(_FIELDS) + i, column=0, columnspan=2, sticky="w", pady=2)

        right = ttk.Frame(body, padding=(8, 0))
        right.pack(side="left", fill="y")
        self.dry_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(right, text="dry-run（只预览不写文件）",
                        variable=self.dry_var).pack(anchor="w")
        self.no_backup_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(right, text="覆盖前不备份（--no-backup）",
                        variable=self.no_backup_var).pack(anchor="w")
        ttk.Button(right, text="生成骨架", command=self.generate).pack(pady=6, fill="x")
        self.log = tk.Text(right, width=52, height=30, state="disabled")
        self.log.pack(fill="both", expand=True)

        self.status = tk.StringVar(value="先扫描基线 → 选基线芯片 → 填新 id（差异项可留空继承）")
        ttk.Label(root, textvariable=self.status, padding=4, relief="sunken",
                  anchor="w").pack(fill="x")

    def reload_bases(self):
        chips_dir = Path(self.root_var.get()) / "chips"
        ids = sorted(p.stem for p in chips_dir.glob("*.json")
                     if not p.name.startswith("_"))
        self.base_combo["values"] = ids
        if ids:
            self.base_combo.set(ids[0])
            self.prefill()
        self.status.set(f"发现 {len(ids)} 个基线清单（{chips_dir}）")

    def prefill(self):
        """把基线芯片现值填入表单（= defaults_from 的 GUI 形态）。"""
        try:
            base = parser_mod.load_base_chip(Path(self.root_var.get()), self.base_var.get())
        except parser_mod.ConfigGenError as e:
            messagebox.showerror("读取失败", str(e))
            return
        o = parser_mod.defaults_from(base, {k: None for k, _l, _h in _FIELDS})
        for key, _label, _hint in _FIELDS:
            if o.get(key) is not None:
                self.vars[key].set(str(o[key]))

    def log_write(self, text: str):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def generate(self):
        cid = self.id_var.get().strip()
        if not cid:
            messagebox.showerror("参数缺失", "新芯片 id 不能为空")
            return
        o = {k: self.vars[k].get().strip() or None for k, _l, _h in _FIELDS}
        o["id"] = cid
        o["with_bt"] = self.bools["with_bt"].get()
        o["with_oled"] = self.bools["with_oled"].get()
        try:
            base = parser_mod.load_base_chip(Path(self.root_var.get()), self.base_var.get())
            o = parser_mod.defaults_from(base, o)
            spec = parser_mod.build_spec(base, o)
            spec["chip_name"] = cid.upper()
            root = Path(self.root_var.get())
            out_json = root / "chips" / f"{cid}.json"
            out_header = root / "port" / spec["family"] / cid / "board_config.h"
            if self.dry_var.get():
                self.log_write(generator_mod.render_chip_json(spec))
                self.log_write(generator_mod.render_board_config_h(spec))
                self.log_write(f"== dry-run：将写入 {out_json} 与 {out_header}（未写入） ==")
                return
            written = generator_mod.write_outputs(spec, out_json, out_header,
                                                  backup=not self.no_backup_var.get())
            self.log_write("[OK] 已生成：\n  " + "\n  ".join(written))
            self.log_write("[下一步] 补全 TODO 占位 → 实现 port ops → 固件仓 chips/test_chip.py")
            self.status.set("生成完成")
        except parser_mod.ConfigGenError as e:
            self.log_write(f"[X] {e}")
            self.status.set("生成失败（见日志）")


def main() -> int:
    root = tk.Tk()
    App(root)
    root.mainloop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
