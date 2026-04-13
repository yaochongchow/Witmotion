# Witmotion IMU Sensor Interface

---

## System Architecture

```
Witmotion IMU Sensor
↓
UART / Serial Communication
↓
Binary Packet Parser
↓
Structured Data (Accel / Gyro / Orientation)
↓
Logging / Visualization / Robotics Integration
```

---

## Features

- Real-time IMU data streaming (20–100 Hz depending on configuration)
- Binary protocol parsing for Witmotion sensors
- Extraction of:
  - Acceleration (X, Y, Z)
  - Angular velocity (Gyroscope)
  - Orientation (Roll, Pitch, Yaw)
- Modular parser design for extensibility

---

## Engineering Highlights

- Handles incomplete or misaligned binary packets
- Built for continuous streaming under real-time constraints
- Structured output for robotics frameworks (ROS2-ready)
- Easily extendable to BLE / dashboards / multi-sensor setups

---

## Hardware Requirements

- Witmotion IMU sensor (WT9011DCL-BT50)
- NRF52832 DK (for BLE relay)
- USB-to-Serial adapter
- Linux PC with GCC and nRF Command Line Tools

---

## Installation

```bash
git clone https://github.com/yaochongchow/Witmotion.git
cd Witmotion
```

See [SETUP.md](SETUP.md) for full build and dependency instructions.

---

## Usage

```bash
cd src
./read_usb.sh
```

See [RUN.md](RUN.md) for all run options (USB/RTT, BLE receiver, direct serial).

---

## Example Output

```
Roll: 12.3°
Pitch: -3.2°
Yaw: 180.1°

Acceleration:
X: 0.01 g
Y: 9.81 g
Z: 0.02 g

Gyroscope:
X: 0.02 °/s
Y: -0.01 °/s
Z: 0.00 °/s
```

---

## Use Cases

- Robotics navigation
- Real-time telemetry pipelines
- Motion tracking systems
- Sensor fusion experiments

---

## Future Improvements

- Web dashboard (React + WebSockets)
- ROS2 integration
- BLE support
- Database logging (Postgres / time-series DB)

---

## Project Structure

```
Witmotion/
├── nrf52_firmware/      Zephyr BLE central firmware (C)
├── src/                 PC-side tools and scripts
├── WT9011DCL_Documents/ Datasheets and protocol PDFs
├── SETUP.md
├── RUN.md
└── README.md
```

---

## Why This Project

This project emphasizes real-world system behavior:

- Real-time data handling
- Reliability under continuous streaming
- Clean, extensible architecture
