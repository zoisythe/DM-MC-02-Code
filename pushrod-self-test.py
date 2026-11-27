"""Use STM32CubeProgrammer HOTPLUG reads to record a 10-second pushrod test."""

import argparse
import csv
import ctypes as ct
from collections import Counter, deque
from datetime import datetime, timedelta, timezone
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parent
PROGRAMMER_BIN = Path(r"D:\app\STM32CubeProgrammer\bin")
NM = Path(r"D:\app\arm-gnu-toolchain-15.2.rel1\bin\arm-none-eabi-nm.exe")
FLASH_BASE = 0x08000000
GPIOC_ODR = 0x58020814
GPIOE_MODER = 0x58021000
GPIOE_DIRECTION_PINS = (7, 8, 2, 3)  # IN1, IN2, IN3, IN4
DURATION = 10.0
INTERVAL = 0.05


# Layout from the installed STM32CubeProgrammer 2.23 CubeProgrammer_API.h.
class Frequencies(ct.Structure):
    _fields_ = [
        ("jtag", ct.c_uint * 12), ("jtag_count", ct.c_uint),
        ("swd", ct.c_uint * 12), ("swd_count", ct.c_uint),
    ]


class Probe(ct.Structure):
    _fields_ = [
        ("port", ct.c_int), ("index", ct.c_int), ("serial", ct.c_char * 33),
        ("firmware", ct.c_char * 20), ("voltage", ct.c_char * 5),
        ("access_port_count", ct.c_int), ("access_port", ct.c_int),
        ("mode", ct.c_int), ("reset", ct.c_int), ("old_firmware", ct.c_int),
        ("frequencies", Frequencies), ("frequency", ct.c_int),
        ("bridge", ct.c_int), ("shared", ct.c_int), ("board", ct.c_char * 100),
        ("debug_sleep", ct.c_int), ("speed", ct.c_int),
    ]


InitProgress = ct.CFUNCTYPE(None)
LogMessage = ct.CFUNCTYPE(None, ct.c_int, ct.c_wchar_p)
LoadProgress = ct.CFUNCTYPE(None, ct.c_int, ct.c_int)


class DisplayCallbacks(ct.Structure):
    _fields_ = [("init", InitProgress), ("log", LogMessage), ("load", LoadProgress)]


class StLink:
    def __init__(self, bin_dir):
        self.dll_directory = os.add_dll_directory(str(bin_dir))
        self.api = ct.CDLL(str(bin_dir / "CubeProgrammer_API.dll"))
        self.messages = deque(maxlen=8)
        self.callbacks = DisplayCallbacks(
            InitProgress(lambda: None),
            LogMessage(lambda _level, message: self.messages.append((message or "").strip())),
            LoadProgress(lambda _current, _total: None),
        )
        signatures = (
            ("setLoadersPath", [ct.c_char_p], None),
            ("setDisplayCallbacks", [DisplayCallbacks], None),
            ("setVerbosityLevel", [ct.c_int], None),
            ("getStLinkEnumerationList", [ct.POINTER(ct.POINTER(Probe)), ct.c_int], ct.c_int),
            ("deleteInterfaceList", [], None),
            ("connectStLink", [Probe], ct.c_int),
            ("disconnect", [], None),
            ("readMemory", [ct.c_uint, ct.POINTER(ct.c_void_p), ct.c_uint], ct.c_int),
            ("freeLibraryMemory", [ct.c_void_p], None),
        )
        for name, argument_types, result_type in signatures:
            function = getattr(self.api, name)
            function.argtypes = argument_types
            function.restype = result_type
        self.api.setLoadersPath(os.fsencode(bin_dir))
        self.api.setDisplayCallbacks(self.callbacks)
        self.api.setVerbosityLevel(0)
        self.connected = False

    def connect(self):
        probes = ct.POINTER(Probe)()
        count = self.api.getStLinkEnumerationList(ct.byref(probes), 0)
        if count != 1:
            raise RuntimeError(f"需要恰好一个 ST-Link，枚举结果为 {count}。")
        probe = Probe.from_buffer_copy(probes[0])
        probe.port = 1  # SWD
        probe.mode = 1  # HOTPLUG, without halting or resetting the CPU
        probe.frequency = 1000  # kHz
        self.messages.clear()
        error = self.api.connectStLink(probe)
        if error:
            details = " / ".join(filter(None, self.messages))
            self.api.disconnect()
            raise RuntimeError(f"SWD 热插拔连接失败（{error}）。{details}")
        self.connected = True

    def read(self, address, size):
        data = ct.c_void_p()
        error = self.api.readMemory(address, ct.byref(data), size)
        try:
            if error or not data.value:
                raise RuntimeError(f"SWD 读取 0x{address:08X} 失败（{error}）。")
            return ct.string_at(data, size)
        finally:
            if data.value:
                self.api.freeLibraryMemory(data)

    def close(self):
        if self.connected:
            self.api.disconnect()
        self.api.deleteInterfaceList()
        self.dll_directory.close()


def verified_symbols(link, build_dir):
    candidates = [build_dir / "firmware.elf"]
    candidates.extend(sorted(build_dir.glob("last-flashed-*/firmware.elf"), reverse=True))
    for elf in candidates:
        binary = elf.with_suffix(".bin")
        if not elf.is_file() or not binary.is_file():
            continue
        image = binary.read_bytes()
        if not image or link.read(FLASH_BASE, len(image)) != image:
            continue
        table = subprocess.run(
            [str(NM), "-S", str(elf)], check=True, capture_output=True, text=True
        ).stdout
        symbols = {
            name: (int(address, 16), int(size, 16))
            for address, size, name in re.findall(
                r"^([0-9a-fA-F]+) ([0-9a-fA-F]+) [bB] (sbus_status|armed)$",
                table, re.MULTILINE,
            )
        }
        if symbols.get("sbus_status", (0, 0))[1] == 88:
            print(f"板上程序匹配 {elf}，将记录 SBUS 状态。", flush=True)
            return symbols
    print("没有找到匹配的固件符号，只记录 GPIO 电平。", flush=True)
    return {}


def snapshot(link, symbols):
    power, = struct.unpack("<I", link.read(GPIOC_ODR, 4))
    mode, _, _, _, input_levels, output_levels = struct.unpack(
        "<6I", link.read(GPIOE_MODER, 24)
    )
    bits = tuple((output_levels >> pin) & 1 for pin in GPIOE_DIRECTION_PINS)
    actions = {
        (0, 0, 0, 0): "stop",
        (1, 0, 0, 0): "A_extend", (0, 1, 0, 0): "A_retract",
        (0, 0, 1, 0): "B_extend", (0, 0, 0, 1): "B_retract",
    }
    gpio_output_mode = all((mode >> (2 * pin)) & 3 == 1 for pin in GPIOE_DIRECTION_PINS)
    sample = {
        "PC13_P8_EN": (power >> 13) & 1,
        "PC14_P7_EN": (power >> 14) & 1,
        "PC15_5V_EN": (power >> 15) & 1,
        "direction_gpio_output_mode": int(gpio_output_mode),
        "output": actions.get(bits, "unexpected_combination") if gpio_output_mode else "not_gpio_output",
    }
    for index, (pin, bit) in enumerate(zip(GPIOE_DIRECTION_PINS, bits), start=1):
        sample[f"IN{index}_command"] = bit
        sample[f"IN{index}_pad_readback"] = (input_levels >> pin) & 1
    if "sbus_status" in symbols:
        sbus = link.read(symbols["sbus_status"][0], 60)
        sample["SWB_CH8"], sample["SWC_CH9"] = struct.unpack_from("<HH", sbus, 38)
        sample["sbus_good_frames"], sample["sbus_interruptions"] = struct.unpack_from("<II", sbus, 16)
        sample["sbus_failsafe"] = sbus[58]
        if "armed" in symbols:
            address, size = symbols["armed"]
            if size == 1:
                sample["armed"] = link.read(address & ~3, 4)[address & 3]
    return sample


def capture(link, symbols, output_dir):
    baseline = snapshot(link, symbols)
    output_dir.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone(timedelta(hours=8))).strftime("%Y%m%d_%H%M%S_%f")
    report = output_dir / f"pushrod_{started}.csv"
    print("请将 SWC 回中。3 秒后开始，随后依次测试两根推杆的伸出和缩回。", flush=True)
    time.sleep(3)
    counts = Counter()
    last_output = None
    with report.open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=["elapsed_s", *baseline])
        writer.writeheader()
        print("开始，采样 10 秒。", flush=True)
        start = time.monotonic()
        next_sample = start
        try:
            while (now := time.monotonic()) < start + DURATION:
                if now < next_sample:
                    time.sleep(min(next_sample, start + DURATION) - now)
                    continue
                data = snapshot(link, symbols)
                if time.monotonic() >= start + DURATION:
                    break
                elapsed = now - start
                writer.writerow({"elapsed_s": f"{elapsed:.3f}", **data})
                counts[data["output"]] += 1
                if data["output"] != last_output:
                    print(f"{elapsed:5.2f}s {data['output']}", flush=True)
                    last_output = data["output"]
                next_sample = max(now + INTERVAL, time.monotonic())
        finally:
            print(f"记录文件：{report}", flush=True)
    print(f"结束，共 {sum(counts.values())} 个样本。输出次数：{dict(counts)}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--programmer-bin", type=Path, default=PROGRAMMER_BIN)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    args = parser.parse_args()
    link = StLink(args.programmer_bin.resolve())
    try:
        link.connect()
        symbols = verified_symbols(link, args.build_dir)
        capture(link, symbols, args.build_dir / "pushrod-self-test")
    finally:
        link.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(error, file=sys.stderr)
        sys.exit(1)
