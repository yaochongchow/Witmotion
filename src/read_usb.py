#!/usr/bin/env python3
"""
RTT capture helper using pynrfjprog.

Why this exists:
- Some SEGGER CLI versions fail RTT auto-detection for this target even when
  firmware is running correctly.
- pynrfjprog RTT APIs are stable in this environment and provide direct access.
"""

import csv
import os
import re
import signal
import sys
import time
from datetime import datetime

from pynrfjprog import LowLevel
from pynrfjprog.APIError import APIError

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")
ROW_RE = re.compile(
    r"^\s*(\d+)\s*\|\s*Seq:(\d+)\s*\|\s*Acc\((-?\d+),(-?\d+),(-?\d+)\)\s*"
    r"\|\s*Gyro\((-?\d+),(-?\d+),(-?\d+)\)\s*"
    r"\|\s*Q\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)\s*"
    r"\|\s*RSSI:(-?\d+)\s*\|\s*Hz:(\d+)"
    r"(?:\s*\|\s*CRC:([0-9A-Fa-f]{4}))?\s*$"
)

RUNNING = True


def _sig_handler(_sig, _frame):
    global RUNNING
    RUNNING = False


def now_ms():
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]


def to_u8(v):
    return v & 0xFF


def crc16_ccitt_false(data):
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8) & 0xFFFF
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def choose_snr(api, snr_env):
    emus = api.enum_emu_snr()
    if not emus:
        raise RuntimeError("No J-Link probe found")

    if snr_env:
        snr = int(snr_env)
        if snr not in emus:
            raise RuntimeError(f"Requested J-Link S/N {snr} not found. Available: {emus}")
        return snr

    return emus[0]


def print_banner(csv_file, auto_reset, startup_delay, snr):
    print("Connecting to NRF52 via RTT (pynrfjprog)...")
    print("Press Ctrl+C to stop")
    print("Display mode: every sample (no averaging)")
    print(f"Auto reset target: {auto_reset}")
    print(f"Startup delay: {startup_delay}s")
    print(f"J-Link S/N: {snr}")
    print(f"CSV log: {csv_file}")
    print("")
    print("DateTime                | Time(ms) | Seq  | Acc(x,y,z) | Gyro(x,y,z) | Q(w,x,y,z) | RSSI | Hz | CRC16")
    print("------------------------+----------+------+------------+-------------+-------------+------+----+------")


def main():
    signal.signal(signal.SIGINT, _sig_handler)
    signal.signal(signal.SIGTERM, _sig_handler)

    csv_file = os.environ.get("CSV_FILE", f"witmotion_rtt_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv")
    auto_reset = os.environ.get("AUTO_RESET", "0") == "1"
    startup_delay = float(os.environ.get("STARTUP_DELAY_SEC", "0.25"))
    snr_env = os.environ.get("JLINK_SN") or os.environ.get("NRF_SN")
    rtt_retry = int(os.environ.get("RTT_START_RETRIES", "20"))

    api = LowLevel.API("NRF52")
    buf = ""

    with open(csv_file, "w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "datetime",
            "fw_time_ms",
            "seq",
            "acc_x",
            "acc_y",
            "acc_z",
            "gyro_x",
            "gyro_y",
            "gyro_z",
            "qw",
            "qx",
            "qy",
            "qz",
            "rssi_dbm",
            "hz",
            "crc16",
            "crc_ok",
        ])
        f.flush()

        try:
            api.open()
            snr = choose_snr(api, snr_env)
            print_banner(csv_file, int(auto_reset), startup_delay, snr)

            api.connect_to_emu_with_snr(snr)
            api.connect_to_device()

            if auto_reset:
                api.sys_reset()
            api.go()
            time.sleep(startup_delay)

            cb_found = False
            for _ in range(max(rtt_retry, 1)):
                try:
                    api.rtt_start()
                except APIError:
                    pass

                for _ in range(5):
                    try:
                        cb_found = api.rtt_is_control_block_found()
                    except APIError:
                        cb_found = False
                    if cb_found:
                        break
                    time.sleep(0.1)

                if cb_found:
                    break

                try:
                    api.rtt_stop()
                except APIError:
                    pass
                api.go()
                time.sleep(0.1)

            if not cb_found:
                print("[WRN] RTT control block not found yet; continuing to poll")

            while RUNNING:
                try:
                    data = api.rtt_read(0, 4096)
                except APIError:
                    # Transient RTT errors can happen during reconnect windows.
                    time.sleep(0.1)
                    continue

                if data:
                    if isinstance(data, str):
                        chunk = data
                    elif isinstance(data, (bytes, bytearray)):
                        chunk = bytes(data).decode("utf-8", errors="replace")
                    else:
                        chunk = bytes(data).decode("utf-8", errors="replace")
                    buf += chunk

                while "\n" in buf:
                    raw_line, buf = buf.split("\n", 1)
                    line = ANSI_RE.sub("", raw_line).strip("\r")
                    if not line:
                        continue

                    if "[INF] witmotion_relay:" in line:
                        print(line)
                        continue

                    if line.startswith("Time(ms)") or line.startswith("---------"):
                        continue

                    m = ROW_RE.match(line)
                    if not m:
                        if "Seq:" in line and "Acc(" in line and "Gyro(" in line:
                            print(f"{now_ms()} | RAW | {line}")
                        continue

                    dt = now_ms()
                    (
                        fw_time,
                        seq,
                        ax,
                        ay,
                        az,
                        gx,
                        gy,
                        gz,
                        qw,
                        qx,
                        qy,
                        qz,
                        rssi,
                        hz,
                        rx_crc_hex,
                    ) = m.groups()

                    i_seq = int(seq)
                    i_ax = int(ax)
                    i_ay = int(ay)
                    i_az = int(az)
                    i_gx = int(gx)
                    i_gy = int(gy)
                    i_gz = int(gz)
                    i_qw = int(qw)
                    i_qx = int(qx)
                    i_qy = int(qy)
                    i_qz = int(qz)
                    i_rssi = int(rssi)
                    i_hz = int(hz)
                    i_fw_time = int(fw_time)

                    crc_payload = [
                        to_u8(i_seq),
                        to_u8(i_ax),
                        to_u8(i_ay),
                        to_u8(i_az),
                        to_u8(i_gx),
                        to_u8(i_gy),
                        to_u8(i_gz),
                        to_u8(i_qw),
                        to_u8(i_qx),
                        to_u8(i_qy),
                        to_u8(i_qz),
                        to_u8(i_rssi),
                        to_u8(i_hz),
                    ]
                    calc_crc = crc16_ccitt_false(crc_payload)
                    rx_crc = int(rx_crc_hex, 16) if rx_crc_hex else None
                    crc_ok = (rx_crc == calc_crc) if rx_crc is not None else True

                    print(
                        f"{dt} | {i_fw_time} | Seq:{i_seq} | "
                        f"Acc({i_ax},{i_ay},{i_az}) | "
                        f"Gyro({i_gx},{i_gy},{i_gz}) | "
                        f"Q({i_qw},{i_qx},{i_qy},{i_qz}) | "
                        f"RSSI:{i_rssi} | Hz:{i_hz} | "
                        f"CRC:{calc_crc:04X}" + ("" if crc_ok else " MISMATCH")
                    )

                    writer.writerow([
                        dt,
                        i_fw_time,
                        i_seq,
                        i_ax,
                        i_ay,
                        i_az,
                        i_gx,
                        i_gy,
                        i_gz,
                        i_qw,
                        i_qx,
                        i_qy,
                        i_qz,
                        i_rssi,
                        i_hz,
                        f"{calc_crc:04X}",
                        1 if crc_ok else 0,
                    ])
                    f.flush()

                time.sleep(0.02)

        finally:
            try:
                api.rtt_stop()
            except Exception:
                pass
            try:
                api.disconnect_from_device()
            except Exception:
                pass
            try:
                api.disconnect_from_emu()
            except Exception:
                pass
            try:
                api.close()
            except Exception:
                pass


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print(f"[ERR] {exc}", file=sys.stderr)
        sys.exit(1)
