# Witmotion WT9011DCL BLE Relay

Interface tools for the Witmotion WT9011DCL-BT50 IMU sensor (BLE 5.0). Includes an nRF52 firmware relay that bridges BLE to USB/RTT, and PC-side tools to read and log sensor data.

Sensor data: accelerometer, gyroscope, quaternion, RSSI, sample rate.

---

## Repository Layout

```
nrf52_firmware/       nRF52 Zephyr firmware (BLE central relay)
src/                  PC-side tools (C programs, shell scripts)
WT9011DCL_Documents/  Datasheets and communication protocol PDFs
RECOVERED_FROM_RESET/ Sensor state recovered after factory reset
```

---

## Hardware

- **NRF52832 DK** (chip label N52832 on the board)
  - Board target: `nrf52dk/nrf52832`
  - JLink device: `NRF52832_XXAA`
  - **Important:** Using the wrong board target will cause a BUS FAULT at boot.
- **Witmotion WT9011DCL-BT50** IMU sensor (BLE 5.0)

---

## Setup

### C Compiler

```bash
sudo apt install gcc cmake build-essential
```

### nRF Command Line Tools

Required for flashing firmware and reading USB/RTT output. Provides `JLinkExe` and `JLinkRTTClient`.

Download from: https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools

### nRF Connect SDK (for building firmware)

Install via Nordic's [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop) toolchain manager.

Expected install location: `~/ncs/v3.0.2`

### SimpleBLE (only for wireless BLE receiver — Option C)

```bash
sudo apt install libdbus-1-dev

git clone https://github.com/simpleble/simpleble
cd simpleble/simpleble && cmake -B build && cmake --build build && sudo cmake --install build
cd ../simplecble && cmake -B build && cmake --build build && sudo cmake --install build
sudo cp build/export/simplecble/export.h /usr/local/include/simplecble/
```

### BlueZ

Usually pre-installed. If not:

```bash
sudo apt install bluez
```

---

## Build

### Firmware

```bash
export PATH="$HOME/ncs/toolchains/*/usr/local/bin:/usr/bin:/bin:$PATH"
export ZEPHYR_BASE="$HOME/ncs/v3.0.2/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/ncs/toolchains/*/opt/zephyr-sdk"

cd ~/ncs/v3.0.2
west build -b nrf52dk/nrf52832 --no-sysbuild /path/to/this/repo/nrf52_firmware

# Connect NRF52 DK via USB, then flash
west flash
```

### PC-Side Tools (serial only, no dependencies)

```bash
cd src
gcc -Wall -o witmotion_wt9011dcl witmotion_wt9011dcl.c
gcc -Wall -o witmotion_wt901cttl witmotion_wt901cttl.c -lm
```

### PC-Side Tools (all, with CMake — requires SimpleBLE for BLE receiver)

```bash
cd src
cmake -B build
cmake --build build
```

---

## Prepare the Sensor

Unpair the Witmotion sensor from your PC's Bluetooth before use — the sensor can only connect to one device at a time.

```bash
bluetoothctl remove FE:64:4D:CE:D2:BF
```

Power the sensor on. The nRF52 firmware will auto-detect it by name (`"WT*"`) or UUID (`0xFFE0`).

---

## Running

### Option A: USB/RTT via read_usb.sh (easiest)

Wraps JLinkExe and JLinkRTTClient, prepends timestamps, and logs to CSV.

**Requirements:** NRF52832 DK flashed and connected via USB.

```bash
cd src
./read_usb.sh
```

Force a clean reset on startup (useful after flashing):

```bash
AUTO_RESET=1 STARTUP_DELAY_SEC=0.5 ./read_usb.sh
```

Expected output:

```
Connecting to NRF52 via RTT...
Press Ctrl+C to stop
CSV log: witmotion_rtt_20260318_190000.csv

[INF] witmotion_relay: Advertising as WT_RELAY, scanning for Witmotion...
[INF] witmotion_relay: Auto-detected Witmotion: FE:64:4D:CE:D2:BF (random)
[INF] witmotion_relay: Subscribed to Witmotion notifications
DateTime            | Time(ms) | Seq  | Acc(x,y,z)     | Gyro(x,y,z)    | Q(w,x,y,z)     | RSSI  | Hz
2026-03-18 00:02:21.103 | 2828 | Seq:6 | Acc(-15,-7,-4) | Gyro(-2,2,-21) | Q(127,0,0,0) | RSSI:-65 | Hz:10
```

---

### Option B: USB/RTT Manual (two terminals)

**Requirements:** NRF52832 DK flashed and connected via USB.

**Terminal 1:**

```bash
JLinkExe -Device NRF52832_XXAA -If SWD -Speed 4000 -AutoConnect 1
```

**Terminal 2:**

```bash
JLinkRTTClient
```

---

### Option C: BLE Receiver (wireless)

Receives data from the NRF52 DK over BLE. The DK advertises as `WT_RELAY`.

**Requirements:** NRF52832 DK flashed and powered. SimpleBLE installed. BLE adapter on PC.

```bash
cd src
cmake -B build && cmake --build build
sudo ./build/nrf52_receiver
```

BLE permissions:

```bash
sudo usermod -aG bluetooth $USER
# Log out and back in for the change to take effect
```

---

### Option D: Direct USB Serial (no NRF52)

Connect directly to the sensor via Bluetooth serial (rfcomm) or USB, bypassing the NRF52 relay entirely.

**WT9011DCL:**

```bash
cd src
gcc -Wall -o witmotion_wt9011dcl witmotion_wt9011dcl.c
sudo ./witmotion_wt9011dcl
# Default: /dev/rfcomm0 at 115200 baud
```

**WT901CTTL:**

```bash
cd src
gcc -Wall -o witmotion_wt901cttl witmotion_wt901cttl.c -lm
./witmotion_wt901cttl
# Default: /dev/ttyUSB0 at 9600 baud
```

Serial port permissions:

```bash
sudo usermod -aG dialout $USER
# Log out and back in for the change to take effect
```

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| BUS FAULT at boot | Wrong board target. Chip label must read N52832. Use `-b nrf52dk/nrf52832` with `--no-sysbuild`. |
| Witmotion not auto-detected | Ensure sensor is powered on. Unpair from PC Bluetooth first. Check within ~10m range. Check RTT logs. |
| "No FFE4 characteristic" in logs | Detected device is not a Witmotion sensor. If your sensor uses a different UUID, update `witmotion_notify_uuid` in `nrf52_firmware/src/witmotion_central.c`. |
| No data after "Subscribed" | Unlock command may not match sensor model. Check RTT logs. Try moving the sensor. |
| `WT_RELAY` not found (BLE receiver) | Ensure NRF52 is powered and flashed. BLE scanning needs `sudo` or `bluetooth` group membership. |
| Permission denied on serial port | `sudo usermod -aG dialout $USER`, then log out and back in. |
| `JLinkExe: command not found` | Install nRF Command Line Tools and ensure they are on your PATH. |
| `read_usb.sh` shows no output | Wait 3-5 seconds for RTT connection. Ensure sensor is powered on. |
| `west: command not found` | Export toolchain PATH from the Build section above in the same shell before running `west`. |
| Connection timeout / RF noise | JLink debug probe can interfere with BLE. Normal — firmware retries automatically. |

---

## Factory Reset

If the sensor is unresponsive, send the factory reset command:

```bash
echo -n -e '\xFF\xAA\x00\x01\x00' > /dev/ttyUSB0
sudo usermod -aG dialout $USER
sudo chmod a+rw /dev/ttyUSB0
```

Adjust `/dev/ttyUSB0` to match your device enumeration.
