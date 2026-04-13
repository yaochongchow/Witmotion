# Run

Step-by-step instructions for running the Witmotion BLE Relay tools after setup. See [SETUP.md](SETUP.md) for prerequisites and installation.

---

## Option A: USB/RTT with read_usb.sh (easiest)

The simplest way to read sensor data. The script wraps JLinkExe and JLinkRTTClient, prepends the current date/time (with milliseconds) to each data line, and logs raw samples to CSV.

Fast-start defaults:
- `AUTO_RESET=0` (do not hard-reset the target when attaching)
- `STARTUP_DELAY_SEC=0.25`

This minimizes startup delay when the firmware is already running.

**Requirements:** NRF52832 DK flashed and connected via USB. nRF Command Line Tools installed.

```bash
cd src
./read_usb.sh
```

Optional: force a clean reset on startup (slower startup, useful after flashing):
```bash
cd src
AUTO_RESET=1 STARTUP_DELAY_SEC=0.5 ./read_usb.sh
```

**Expected output:**

```
Connecting to NRF52 via RTT...
Press Ctrl+C to stop
Display mode: every sample (no averaging)
Auto reset target: 0
Startup delay: 0.25s
CSV log: witmotion_rtt_20260318_190000.csv

[INF] witmotion_relay: Advertising as WT_RELAY, scanning for Witmotion...
[INF] witmotion_relay: Auto-detected Witmotion: FE:64:4D:CE:D2:BF (random)
[INF] witmotion_relay: Witmotion connected, discovering services...
[INF] witmotion_relay: Subscribed to Witmotion notifications
DateTime            | Time(ms) | Seq  | Acc(x,y,z)     | Gyro(x,y,z)    | Q(w,x,y,z)     | RSSI  | Hz
--------------------|----------|------|----------------|----------------|-----------------|-------|---
2026-03-18 00:02:21.103 | 2828     | Seq:6 | Acc(-15,-7,-4) | Gyro(-2,2,-21) | Q(127,0,0,0)   | RSSI:-65 | Hz:10
2026-03-18 00:02:21.203 | 2930     | Seq:7 | Acc(-14,-8,-3) | Gyro(-1,3,-20) | Q(127,0,0,0)   | RSSI:-65 | Hz:10
```

Press Ctrl+C to stop.

---

## Option B: USB/RTT Manual (two terminals)

Same as Option A but running the J-Link tools manually. Useful if you need more control or `read_usb.sh` does not work on your system.

**Requirements:** NRF52832 DK flashed and connected via USB. nRF Command Line Tools installed.

**Terminal 1** -- start the J-Link debug server:
```bash
JLinkExe -Device NRF52832_XXAA -If SWD -Speed 4000 -AutoConnect 1
```

**Terminal 2** -- read RTT output:
```bash
JLinkRTTClient
```

**Expected output (Terminal 2):**

```
[INF] witmotion_relay: Advertising as WT_RELAY, scanning for Witmotion...
[INF] witmotion_relay: Auto-detected Witmotion: FE:64:4D:CE:D2:BF (random)
[INF] witmotion_relay: Witmotion connected, discovering services...
[INF] witmotion_relay: Subscribed to Witmotion notifications
Time(ms) | Seq  | Acc(x,y,z)     | Gyro(x,y,z)    | Q(w,x,y,z)     | RSSI  | Hz
---------|------|----------------|----------------|-----------------|-------|---
2828     | Seq:20 | Acc(-15,-7,-4) | Gyro(-2,2,-21) | Q(127,0,0,0)   | RSSI:-65 | Hz:10
4829     | Seq:40 | Acc(-14,-8,-3) | Gyro(-1,3,-20) | Q(127,0,0,0)   | RSSI:-65 | Hz:10
```

Note: without `read_usb.sh`, no date/time is prepended. The firmware outputs timestamps in milliseconds since boot.

---

## Option C: BLE Receiver (wireless)

Receives data wirelessly from the NRF52 DK over BLE. The NRF52 advertises as `WT_RELAY` and sends 18-byte payloads.

**Requirements:** NRF52832 DK flashed and powered. SimpleBLE C bindings installed. BLE adapter on the PC.

```bash
cd src

# Build with CMake
cmake -B build
cmake --build build

# Run (needs sudo or bluetooth group on Linux)
sudo ./build/nrf52_receiver
```

**Expected output:**

```
Scanning for WT_RELAY...
Found: WT_RELAY (XX:XX:XX:XX:XX:XX)
Connecting to XX:XX:XX:XX:XX:XX...
Connected to WT_RELAY
CSV logging enabled: witmotion_log_20260318_170102.csv
Receiving 18-byte payloads... (Ctrl+C to stop)

Seq:  0 | Acc(  0.00,   0.00,   1.01) | Gyro(    0.0,     0.0,     0.0) | Q(1.00, 0.00, 0.00, 0.00)
Seq:  1 | Acc(  0.01,   0.05,   1.00) | Gyro(   -0.5,     1.2,    -3.1) | Q(0.89, 0.43, -0.07, 0.00)
```

CSV log file details:
- Filename: `witmotion_log_YYYYMMDD_HHMMSS.csv`
- Created in: the directory where you launch `nrf52_receiver`
- Timestamp format in the CSV: `YYYY-MM-DD HH:MM:SS.mmm`

**Linux BLE permissions:** BLE scanning requires root or membership in the `bluetooth` group:
```bash
sudo usermod -aG bluetooth $USER
# Log out and log back in for the change to take effect
```

---

## Option D: Direct USB Serial (no NRF52)

Connect directly to the Witmotion sensor via Bluetooth serial or USB, bypassing the NRF52 relay entirely.

**Requirements:** Sensor paired via Bluetooth (rfcomm) or connected via USB. No NRF52 needed.

### WT9011DCL

```bash
cd src
gcc -Wall -o witmotion_wt9011dcl witmotion_wt9011dcl.c
sudo ./witmotion_wt9011dcl
```

Default: `/dev/rfcomm0` at 115200 baud.

### WT901CTTL

```bash
cd src
gcc -Wall -o witmotion_wt901cttl witmotion_wt901cttl.c -lm
./witmotion_wt901cttl
```

Default: `/dev/ttyUSB0` at 9600 baud.

**Serial port permissions:** If you get `Permission denied`, add yourself to the `dialout` group:
```bash
sudo usermod -aG dialout $USER
# Log out and log back in for the change to take effect
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| **BUS FAULT at boot** | Wrong board target. The chip label must read N52832. Use `-b nrf52dk/nrf52832` with `--no-sysbuild`. |
| Witmotion not auto-detected | 1. Ensure sensor is powered on. 2. Unpair it from your PC's Bluetooth first (`bluetoothctl remove FE:64:4D:CE:D2:BF`). 3. Ensure within ~10m range. 4. Check RTT logs for status. |
| "No FFE4 characteristic" in logs | The detected device is not a Witmotion sensor. It will be blacklisted automatically. If your sensor uses a different UUID, update `witmotion_notify_uuid` in `witmotion_central.c`. |
| No data after "Subscribed" | The unlock command may not match your sensor model. Check RTT for data lines. Try moving the sensor. |
| `WT_RELAY` not found (BLE receiver) | Ensure NRF52 is powered and flashed. BLE scanning on Linux needs `sudo` or `bluetooth` group membership. |
| SimpleBLE finds 0 devices | Run with `sudo`, or add user to `bluetooth` group and re-login. |
| Permission denied on serial port | `sudo usermod -aG dialout $USER`, then log out and log back in. |
| `JLinkExe: command not found` | Install nRF Command Line Tools and ensure they are on your PATH. |
| `read_usb.sh` shows no output | Wait 3-5 seconds for RTT connection to establish. Ensure sensor is powered on. |
| `west: unknown command "build"` | Run from inside the nRF Connect SDK workspace (`~/ncs/v3.0.2`). |
| `west: command not found` | Export toolchain PATH from SETUP.md (`TOOLCHAIN_DIR` + `export PATH=...`) in the same shell before running `west`. |
| Connection timeout / RF noise | The JLink debug probe can interfere with BLE. Normal -- firmware retries automatically. |
