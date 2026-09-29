#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""配置生成器——渲染与落盘（configgen）。

消费 parser.build_spec 的内部规格，产出 CSP 骨架两件套：
  1) chips/<id>.json        —— 构建侧事实源骨架（ADR-015 schema 同构）；
  2) port/<family>/<id>/board_config.h —— C 侧常量唯一出处骨架（ADR-018 同构）。

骨架约定（重要）：
  - 芯片事实无法派生的字段（flash_driver/register_file/sfd_file、F4 的 startup
    文件名与 c_defines 器件宏）写 TODO 占位——移植者补全前 chipfill 虽可渲染，
    但工程无法烧录；*_note_* 字段强制提示核对。
  - 新芯片一律使用分槽位（chips/<id>/ + linker/<id>/ + proj_rel 前缀），
    与 f103 的根目录 legacy 槽位并存互不影响。

写入前自动备份已存在的目标文件（--no-backup 关闭）；--dry-run 只渲染不落盘。
退出码：0 成功；1 生成/校验失败；2 参数错误。
CLI 与 gui.py 共用本模块函数（GUI 零业务逻辑）。
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import shutil
import sys
import time
from pathlib import Path

# 同目录 parser.py 经 importlib 加载（仓规：无 __init__.py，见 uvprojx/generator.py 同款）
_spec_parser = importlib.util.spec_from_file_location(
    "configgen_parser", Path(__file__).with_name("parser.py"))
_mod = importlib.util.module_from_spec(_spec_parser)
_spec_parser.loader.exec_module(_mod)
ConfigGenError = _mod.ConfigGenError
build_spec = _mod.build_spec
defaults_from = _mod.defaults_from
load_base_chip = _mod.load_base_chip
_hex = _mod._hex
_hex_min = _mod._hex_min

_PORT_PIN_RE_NOTE = "PA9→PORT 0/NUM 9；PB8→1/8；PC13→2/13"


def _pin(name: str):
    """'PA9' -> (port_index, pin_num)。"""
    name = (name or "").strip().upper()
    if len(name) < 3 or name[0] != "P" or name[1] not in "ABCDEF":
        raise ConfigGenError(f"引脚名不合法: {name!r}（形如 PA9/PC13）")
    try:
        return "ABCDEF".index(name[1]), int(name[2:])
    except ValueError:
        raise ConfigGenError(f"引脚号不合法: {name!r}")


# ---- chips/<id>.json 骨架 ----

def _file(name: str, path: str) -> dict:
    return {"name": name, "type": 1, "path": path}


def _port_file_list(family: str, chip_id: str, names: list) -> list:
    d = f"{{{{chip.build.proj_rel}}}}port\\{family}\\{chip_id}"
    return [_file(n, f"{d}\\{n}") for n in names]


def _build_section(spec: dict) -> dict:
    cid, family = spec["id"], spec["family"]
    pr = "{{chip.build.proj_rel}}"
    port_dir = f"port\\{family}\\{cid}"
    with_bt = "uart2_tx" in spec["pins"]
    with_oled = "i2c_scl" in spec["pins"]

    if family == "stm32f1":
        defines = ["STM32F10X_MD"]   # TODO：按容量密度核对（MD/HD/LD）
        startup_name = "startup_stm32f10x_md.s"
        cmsis_sources = [_file("core_cm3.c", f"{pr}third_party\\CMSIS\\core_cm3.c")]
        startup_note = "F1 启动文件按容量密度选 _ld/_md/_hd，默认 MD，按器件核对"
    else:
        defines = ["TODO_DEVICE_DEFINE（形如 STM32F411xE，与器件头一致）"]
        startup_name = "startup_TODO_device.s"
        cmsis_sources = []
        startup_note = "F4 启动文件与器件强相关，必须替换 TODO 文件名；cmsis_sources 置空沿用家族头"

    port_bl = ["main.c", "clock.c", "systick.c", "uart.c", "wifi_stub.c",
               "gpio.c", "wdg.c", "flash.c"]
    port_app = ["clock.c", "systick.c", "uart.c", "gpio.c", "wdg.c", "flash.c"]
    if with_bt:
        port_bl.append("uart2.c")
    if with_oled:
        port_app.append("i2c.c")

    build = {
        "c_defines": defines,
        "cmsis_dir": f"{pr}third_party\\CMSIS",
        "cmsis_sources": cmsis_sources,
        "port_dir": port_dir,
        "startup_files": [_file(startup_name, f"{pr}{port_dir}\\{startup_name}")],
        "bl_jump_file": {"name": "bl_jump.s", "type": 2, "path": f"{pr}{port_dir}\\bl_jump.s"},
        "app_example_dir": f"app\\examples\\{cid}_app",
        "artifact_dir": f"chips\\{cid}",
        "sct_dir": f"linker\\{cid}",
        "proj_rel": "..\\..\\",
        "include_paths_bl": [f"{pr}core", f"{pr}port", f"{pr}{port_dir}",
                             f"{pr}third_party\\CMSIS"],
        "include_paths_app": [f"{pr}core", f"{pr}port", f"{pr}{port_dir}",
                              f"{pr}third_party\\CMSIS", f"{pr}app\\examples\\{cid}_app"],
        "bsp_files_bl": [],
        "bsp_files_app": ([_file("ssd1306.c", f"{pr}bsp\\oled_ssd1306\\ssd1306.c"),
                           _file("ssd1306_font.c", f"{pr}bsp\\oled_ssd1306\\ssd1306_font.c")]
                          if with_oled else []),
        "service_files": [_file("bl_display_led.c", f"{pr}services\\display_led\\bl_display_led.c"),
                          _file("bl_debug_uart.c", f"{pr}services\\debug_uart\\bl_debug_uart.c")],
        "port_files_bl": _port_file_list(family, cid, port_bl),
        "port_files_app": _port_file_list(family, cid, port_app),
        "sign_files_bl": [],
        "_note_sign": "签名验签为可选项（ADR-020）：启用 = 登记 uECC.c/bl_sha256.c/bl_sign.c "
                      "（及 uECC 编译定义与 include 路径，见 third_party/micro-ecc/LICENSES.md）"
                      " + board_config.h 置 BL_SIGN_EN=1 + 本地公钥头（不入库）",
        "_note_startup": startup_note,
        "_note_todo": "flash_driver/register_file/sfd_file 为 TODO 占位：从 Keil DFP 拷贝后再跑 chipfill",
    }
    if with_bt:
        build["_note_bt"] = "蓝牙通道启用后还需 board_config.h 置 BL_TRANSPORT_BT_EN=1（porting_guide §3.1）"
    return build


def render_chip_json(spec: dict) -> str:
    chip = {
        "_note": "CSP 芯片清单骨架（由 LiteTools configgen 生成）：构建侧事实源，"
                 "由 chipfill.py 消费生成 spec/sct。C 侧唯一出处为 port/.../board_config.h。"
                 "TODO 占位字段必须补全后再入构建链。",
        "id": spec["id"],
        "family": spec["family"],
        "device": spec["device"],
        "build": _build_section(spec),
        "memory": spec["memory"],
        "partitions": spec["partitions"],
        "erase_units": spec["erase_units"],
        "clock": spec["clock"],
        "pins": spec["pins"],
        "iwdg": spec["iwdg"],
        "sysmem": spec["sysmem"],
    }
    return json.dumps(chip, ensure_ascii=False, indent=2) + "\n"


# ---- board_config.h 骨架 ----

def render_board_config_h(spec: dict) -> str:
    cid, family = spec["id"], spec["family"]
    mem, parts, eu = spec["memory"], spec["partitions"], spec["erase_units"]
    clk, iwdg, pins = spec["clock"], spec["iwdg"], spec["pins"]
    chip_name = spec.get("chip_name") or cid.upper()
    with_bt = "uart2_tx" in pins
    with_oled = "i2c_scl" in pins

    def pin_block(logical: str, pin_name: str, extra: str = "") -> str:
        p, n = _pin(pin_name)
        return (f"#define BL_PIN_{logical}_PORT    {p}u      /* {pin_name} */\n"
                f"#define BL_PIN_{logical}_NUM     {n}u{extra}")

    if eu["uniform"]:
        erase_section = f"""/* ---- 擦除单元（ADR-015：升级自"页"抽象；core 只经 unit_* 访问） ---- */
#define BL_ERASE_UNITS_UNIFORM  1u
#define BL_ERASE_UNIT_BASE      BL_FLASH_BASE
#define BL_ERASE_UNIT_SIZE      BL_PAGE_SIZE
#define BL_ERASE_UNIT_COUNT     (BL_FLASH_SIZE / BL_PAGE_SIZE)
#define BL_APP_UNITS_MAX        (BL_APP_SIZE / BL_PAGE_SIZE)"""
        page_line = f"#define BL_PAGE_SIZE           {_hex_min(eu['unit_size'])}u   /* 均匀页 */"
    else:
        # 与 port/stm32f4/flash.c 消费格式一致：{基址, 大小} 逐单元展开（count 摊平）
        base_addr = int(mem["flash_base"], 16)
        rows = []
        for u in eu["units"]:
            size = int(u["size"], 16)
            for _ in range(u["count"]):
                rows.append(f"    {{ {_hex(base_addr)}u, {_hex_min(size)}u }}")
                base_addr += size
        body = ", \\\n".join(rows)   # 末行不带续行符
        erase_section = f"""/* ---- 擦除单元（ADR-015：F4 非均匀扇区，显式表 {{基址,大小}}；core 经 bl_flash_ops.unit_* 访问） ----
   典型擦除时间逐扇区核对数据手册；APP 首末边界必须与单元边界重合（bl_storage 自检）。 */
#define BL_ERASE_UNITS_UNIFORM  0u
#define BL_ERASE_UNIT_COUNT     {sum(u['count'] for u in eu['units'])}u
#define BL_ERASE_UNIT_TABLE {{ \\
{body} \\
}}
#define BL_APP_UNITS_MAX        {sum(u['count'] for u in eu['units'])}u     /* APP 覆盖单元数上界（位图容量） */"""
        page_line = "#define BL_PAGE_SIZE           不适用（非均匀家族无该宏）"

    l = []
    a = l.append
    a(f"#ifndef BOARD_CONFIG_H")
    a(f"#define BOARD_CONFIG_H")
    a(f"/* 全项目常量唯一出处（design.md §5 集中配置表）；骨架由 LiteTools configgen 生成，")
    a(f" * TODO 标记处必须人工核对补全。修改需同步 design.md 变更记录与 chips/{cid}.json。 */")
    a("")
    a(f"/* ---- Flash 分区（partition.md §1；数值由 configgen 按清单派生） ---- */")
    a(f"#define BL_FLASH_BASE          {mem['flash_base']}u")
    a(f"#define BL_FLASH_SIZE          {mem['flash_size']}u")
    a(page_line)
    a(f"#define BL_APP_BASE            {parts['app']['base']}u")
    a(f"#define BL_APP_SIZE            {parts['app']['size']}u")
    a(f"#define BL_PARAM_BASE          {parts['params']['base']}u   /* 副本 A */")
    a(f"#define BL_PARAM_SIZE          {parts['params']['size']}u")
    a("")
    a(erase_section)
    a(f"#define BL_PARAM_COPY_SIZE     {_hex_min(int(parts['params']['copies'][0]['size'], 16))}u   /* 副本间隔 = 一个独立擦除单元 */")
    a("")
    a(f"/* ---- SRAM（跳转校验范围） ---- */")
    a(f"#define BL_SRAM_BASE           {mem['sram_base']}u")
    a(f"#define BL_SRAM_SIZE           {mem['sram_size']}u")
    a("")
    a(f"/* ---- 板级标识（显示服务消费；新增 CSP 必须定义） ---- */")
    a(f'#define BL_CHIP_NAME           "{chip_name}"')
    a("")
    a(f"/* ---- 行为常量 ---- */")
    a(f"#define BL_TRANSPORT_BT_EN     {1 if with_bt else 0}       /* 蓝牙通道（ADR-016/019）：1 需 uart2.c 入清单 */")
    a(f"#define BL_SIGN_EN             0       /* 签名验签可选项（ADR-020）：默认关闭；启用见 porting_guide §3.1 */")
    a(f"#define BL_USE_HSE             1       /* TODO：时钟树逐项核对（{clk['hse_mhz']}MHz HSE → {clk['target_hz']}Hz） */")
    a(f"#define BL_BOOT_WAIT_MS        3000u   /* 启动等待窗口（ADR-004） */")
    a(f"#define BL_IWDG_TIMEOUT_MS     {iwdg['normal_ms']}u   /* ADR-011 */")
    a(f"#define BL_IWDG_UPGRADE_TIMEOUT_MS {iwdg['upgrade_relaxed_ms']}u  /* 升级擦写期放宽（ADR-015），覆盖最大单元擦除 */")
    a(f"#define BL_UART_BAUD           115200u")
    a(f"#define BL_PROTOCOL_ACTIVE_MS  10000u  /* 协议活跃判定（ADR-008/009） */")
    a(f"#define BL_UI_REFRESH_MS       200u")
    a(f"#define BL_RX_RING_SIZE        512u    /* architecture.md §6 */")
    a(f"#define BL_FRAME_DATA_MAX      256u    /* protocol.md §4 */")
    a(f"#define BL_FRAME_BYTE_TIMEOUT_MS 2000u /* 帧内字节间超时（protocol.md §4.2） */")
    a(f"#define BL_VERIFY_CHUNK        1024u   /* VERIFY 分块（喂狗粒度） */")
    a(f"#define BL_LOG_HEARTBEAT_MS    500u    /* 空闲心跳日志间隔（0=关闭） */")
    a("")
    a(f"/* ---- 板级引脚（ADR-018：引脚事实唯一出处，gpio.c/i2c.c/uart.c 仅消费） ----")
    a(f"   端口序号：{_PORT_PIN_RE_NOTE} */")
    a(f"#define BL_UART_TX_PORT        {_pin(pins['uart_tx'])[0]}u      /* 升级串口 TX */")
    a(f"#define BL_UART_TX_NUM         {_pin(pins['uart_tx'])[1]}u")
    a(f"#define BL_UART_RX_PORT        {_pin(pins['uart_rx'])[0]}u")
    a(f"#define BL_UART_RX_NUM         {_pin(pins['uart_rx'])[1]}u")
    a(f"#define BL_PIN_LED             0u      /* 逻辑 id：LED */")
    a(pin_block("LED", pins["led"]))
    a(f"#define BL_PIN_LED_ACTIVE_LOW  1")
    if with_oled:
        a(f"#define BL_PIN_I2C_SCL         1u      /* 逻辑 id：OLED 软件 I2C 时线 */")
        a(pin_block("I2C_SCL", pins["i2c_scl"]))
        a(f"#define BL_PIN_I2C_SDA         2u      /* 逻辑 id：OLED 软件 I2C 数据线 */")
        a(pin_block("I2C_SDA", pins["i2c_sda"]))
    if with_bt:
        a(f"#define BL_PIN_BT_STATE        3u      /* 逻辑 id：HC-05 STATE 输入 */")
        a(pin_block("BT_STATE", pins["bt_state"], "   /* 输入下拉 */"))
        a(f"#define BL_PIN_BT_EN           4u      /* 逻辑 id：HC-05 EN 输出，默认低 */")
        a(pin_block("BT_EN", pins["bt_en"]))
    a("")
    a(f"/* ---- 系统内存区（GET_INFO 用；TODO：按芯片手册核对 UID/FlashSize 地址） ---- */")
    a(f"#define BL_UID_ADDR            {spec['sysmem']['uid_addr']}u")
    a(f"#define BL_FLSIZE_ADDR         {spec['sysmem']['flsize_addr']}u")
    a("")
    a(f"#endif /* BOARD_CONFIG_H */")
    return "\n".join(l) + "\n"


# ---- 落盘 ----

def write_outputs(spec: dict, out_json: Path, out_header: Path,
                  backup: bool = True, dry_run: bool = False) -> list:
    """渲染并写入两件套；返回写入（或将写入）的路径列表。"""
    json_text = render_chip_json(spec)
    header_text = render_board_config_h(spec)
    targets = [(out_json, json_text), (out_header, header_text)]
    written = []
    stamps = time.strftime("%Y%m%d-%H%M%S")
    for path, text in targets:
        path = Path(path)
        written.append(str(path))
        if dry_run:
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        if backup and path.exists():
            bak = path.with_name(path.name + f".bak-{stamps}")
            shutil.copy2(path, bak)
            written.append(str(bak))
        path.write_text(text, encoding="utf-8", newline="\n")
    return written


# ---- CLI ----

def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="CSP 配置骨架生成器：从现有芯片派生 chips/<id>.json + board_config.h 骨架")
    ap.add_argument("--from", dest="from_id", required=True, help="基线芯片 id（chips/<id>.json）")
    ap.add_argument("--id", required=True, help="新芯片 id")
    ap.add_argument("--root", default=".", help="固件仓根（读取 chips/<id>.json，默认当前目录）")
    ap.add_argument("--out-json", help="清单输出路径（默认 chips/<id>.json，相对 --root）")
    ap.add_argument("--out-header", help="board_config.h 输出路径（默认 port/<family>/<id>/board_config.h）")
    ap.add_argument("--chip-name", help="BL_CHIP_NAME（默认取 id 大写）")
    ap.add_argument("--family", help="stm32f1|stm32f4（默认继承基线）")
    ap.add_argument("--device-name"); ap.add_argument("--pack-id"); ap.add_argument("--cputype")
    ap.add_argument("--flash-base"); ap.add_argument("--flash-size")
    ap.add_argument("--sram-base"); ap.add_argument("--sram-size")
    ap.add_argument("--bl-base"); ap.add_argument("--bl-size")
    ap.add_argument("--app-base"); ap.add_argument("--app-size")
    ap.add_argument("--param-base"); ap.add_argument("--param-size")
    ap.add_argument("--erase-mode", choices=["uniform", "table"], help="默认继承基线")
    ap.add_argument("--erase-unit-size"); ap.add_argument("--erase-unit-count")
    ap.add_argument("--erase-table", help="显式单元表：JSON 数组 [{'size','count','typical_erase_ms'}] 或文件路径")
    ap.add_argument("--hse-mhz"); ap.add_argument("--target-hz"); ap.add_argument("--hsi-mhz")
    ap.add_argument("--wait-states")
    ap.add_argument("--iwdg-normal-ms"); ap.add_argument("--iwdg-upgrade-ms")
    ap.add_argument("--uart-tx"); ap.add_argument("--uart-rx"); ap.add_argument("--led")
    ap.add_argument("--with-bt", action="store_true")
    ap.add_argument("--uart2-tx", default="PA2"); ap.add_argument("--uart2-rx", default="PA3")
    ap.add_argument("--bt-state", default="PB0"); ap.add_argument("--bt-en", default="PB1")
    ap.add_argument("--with-oled", action="store_true")
    ap.add_argument("--i2c-scl", default="PB8"); ap.add_argument("--i2c-sda", default="PB9")
    ap.add_argument("--uid-addr"); ap.add_argument("--flsize-addr")
    ap.add_argument("--flash-driver"); ap.add_argument("--register-file"); ap.add_argument("--sfd-file")
    ap.add_argument("--no-backup", action="store_true", help="覆盖已有输出前不备份")
    ap.add_argument("--dry-run", action="store_true", help="只渲染打印，不写文件")
    a = ap.parse_args(argv)

    o = {k: getattr(a, k) for k in (
        "family", "device_name", "pack_id", "cputype", "flash_base", "flash_size",
        "sram_base", "sram_size", "bl_base", "bl_size", "app_base", "app_size",
        "param_base", "param_size", "erase_mode", "erase_unit_size", "erase_unit_count",
        "erase_table", "hse_mhz", "target_hz", "hsi_mhz", "wait_states",
        "iwdg_normal_ms", "iwdg_upgrade_ms", "uart_tx", "uart_rx", "led",
        "with_bt", "uart2_tx", "uart2_rx", "bt_state", "bt_en",
        "with_oled", "i2c_scl", "i2c_sda", "uid_addr", "flsize_addr",
        "flash_driver", "register_file", "sfd_file")}
    o["id"] = a.id
    try:
        base = load_base_chip(Path(a.root), a.from_id)
        o = defaults_from(base, o)
        if a.erase_table and not str(a.erase_table).strip().startswith("["):
            p = Path(a.erase_table)
            if not p.is_file():
                raise ConfigGenError(f"--erase-table 文件不存在: {p}")
            a.erase_table = p.read_text(encoding="utf-8")
        if isinstance(o.get("erase_table"), str):
            o["erase_table"] = json.loads(o["erase_table"])
        spec = build_spec(base, o)
        spec["chip_name"] = a.chip_name
        root = Path(a.root)
        out_json = Path(a.out_json) if a.out_json else root / "chips" / f"{a.id}.json"
        out_header = (Path(a.out_header) if a.out_header
                      else root / "port" / spec["family"] / a.id / "board_config.h")
        if a.dry_run:
            print(render_chip_json(spec))
            print(render_board_config_h(spec))
            print(f"== dry-run：将写入 {out_json} 与 {out_header}（未写入） ==")
            return 0
        written = write_outputs(spec, out_json, out_header,
                                backup=not a.no_backup, dry_run=False)
        print("[OK] CSP 骨架已生成：")
        for w in written:
            print(f"  {w}")
        print("[下一步] 1) 补全 *_note_todo 与 startup/c_defines 的 TODO 占位；"
              "2) 实现 port ops；3) 跑固件仓 chips/test_chip.py 一致性测试；"
              "4) chipfill 渲染构建。TODO 未补全前不要入构建链。")
        return 0
    except ConfigGenError as e:
        print(f"[configgen] 错误: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
