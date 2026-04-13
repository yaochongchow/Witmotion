# Setup

Prerequisites and installation for the Witmotion BLE Relay toolkit. For running instructions, see [RUN.md](RUN.md).

## Hardware

- **NRF52832 DK** (chip label N52832 on the board)
  - Board target: `nrf52dk/nrf52832`
  - JLink device: `NRF52832_XXAA`
  - **Important:** Using the wrong board target will cause a BUS FAULT at boot.
- **Witmotion WT901BLE67** sensor (BLE 5.0, address `FE:64:4D:CE:D2:BF`)

## Prerequisites

### C Compiler

A C compiler is required for building the PC-side tools:
```bash
sudo apt install gcc cmake build-essential    # Linux
```

### nRF Command Line Tools

Required for flashing firmware and reading USB/RTT output.

Download from: https://www.nordicsemi.com/Products/Development-tools/nrf-command-line-tools

This provides `JLinkExe` and `JLinkRTTClient`.

### nRF Connect SDK (for building firmware)

Install via Nordic's [nRF Connect for Desktop](https://www.nordicsemi.com/Products/Development-tools/nrf-connect-for-desktop) toolchain manager. This handles all dependencies automatically (Zephyr RTOS, west, CMake, etc.).

Expected install location: `~/ncs/v3.0.2`

### SimpleBLE (only for wireless BLE receiver)

Only needed if you want to use `nrf52_receiver.c` (Option C in RUN.md). Not needed for USB/RTT or direct serial.

```bash
# Linux dependencies
sudo apt install libdbus-1-dev

# Build and install SimpleBLE C bindings
git clone https://github.com/simpleble/simpleble
cd simpleble/simpleble && cmake -B build && cmake --build build && sudo cmake --install build
cd ../simplecble && cmake -B build && cmake --build build && sudo cmake --install build
sudo cp build/export/simplecble/export.h /usr/local/include/simplecble/
```

The C bindings use the header `simplecble/simplecble.h`.

### BlueZ (Linux only)

Usually pre-installed. If not:
```bash
sudo apt install bluez
```

### macOS / Windows

- **macOS** -- Uses CoreBluetooth (built-in, no extra install needed)
- **Windows** -- Requires Windows 10+ with Bluetooth LE support (built-in WinRT backend)

---

## Build the Firmware

```bash
# Set up environment
export PATH="$HOME/ncs/toolchains/*/usr/local/bin:/usr/bin:/bin:$PATH"
export ZEPHYR_BASE="$HOME/ncs/v3.0.2/zephyr"
export ZEPHYR_SDK_INSTALL_DIR="$HOME/ncs/toolchains/*/opt/zephyr-sdk"

# Build
cd ~/ncs/v3.0.2
west build -b nrf52dk/nrf52832 --no-sysbuild /path/to/witmotion_python_wt9011dcl/nrf52_firmware

# Connect the NRF52 DK via USB, then flash
west flash
```

---

## Build the PC-Side Tools

### Serial tools (no dependencies)

```bash
cd src
gcc -Wall -o witmotion_wt9011dcl witmotion_wt9011dcl.c
gcc -Wall -o witmotion_wt901cttl witmotion_wt901cttl.c -lm
```

### All tools with CMake (requires SimpleBLE for BLE receiver)

```bash
cd src
cmake -B build
cmake --build build
```

---

## Prepare the Sensor

Disconnect/unpair the Witmotion sensor from your PC's Bluetooth before use. The sensor can only connect to one device at a time.

```bash
# Linux: remove the sensor from your PC's Bluetooth
bluetoothctl remove FE:64:4D:CE:D2:BF
```

Power on the sensor. The NRF52 will auto-detect it by name (`"WT*"`) or UUID (`0xFFE0`).

---

## Next Steps

See [RUN.md](RUN.md) for step-by-step instructions on running the tools.
