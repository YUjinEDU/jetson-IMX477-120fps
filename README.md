# Sony IMX477 120fps Driver & Hardware ISP Pipeline for NVIDIA Jetson Orin

[![Platform](https://img.shields.io/badge/Platform-NVIDIA%20Jetson%20Orin%20Series-76B900?logo=nvidia)](https://developer.nvidia.com/embedded/jetson-orin)
[![JetPack](https://img.shields.io/badge/JetPack-6.x%20%7C%20L4T%20R39.2.1-blue)](https://developer.nvidia.com/embedded/jetpack)
[![Sensor](https://img.shields.io/badge/Sensor-Sony%20IMX477%20(HQ%20Camera)-red)](https://www.raspberrypi.com/products/raspberry-pi-high-quality-camera/)
[![Framerate](https://img.shields.io/badge/Max%20Framerate-120%20fps%20(Verified)-brightgreen)]()
[![License](https://img.shields.io/badge/License-GPL--2.0-yellow.svg)](LICENSE)

Production-grade Linux kernel driver, Device Tree overlay, and Hardware Image Signal Processor (ISP) tuning enabling **1332×990 @ 120fps (Mode 2)** continuous capture on the **Sony IMX477** (Raspberry Pi High Quality Camera) across the **NVIDIA Jetson Orin Nano, Orin NX, and AGX Orin** families.

---

## 1. Features & Highlights

- **Native Mode 2 Support**: Full 2×2 on-sensor binning + analog crop at **1332×990 @ 120fps** over 2-lane MIPI CSI-2.
- **Dual High-Speed Ingest Pathways**:
  - **Native V4L2 Raw Pipeline**: Direct DMA-mmap RAW10 Bayer stream at **120.165 fps** with 0 frame drops and 8.32ms delta.
  - **Hardware ISP Pipeline (Libargus)**: Zero-CPU demosaicing, lens shading correction, and 3A algorithms via native EGLStream consumer at **120.243 fps** with 0 frame drops.
- **Official NITO Tuning Profile**: Generated using NVIDIA JetPack Customer IQ Migration Tools to provide genuine Mode 2 Knob Sets without unstable byte-patching.
- **Standalone Out-of-Tree Build**: Compiles `nv_imx477.ko` in seconds using local kernel headers; no need to download or compile the full 200MB+ kernel source tree.
- **Automated Verification Suite**: One-line regression scripts for both Raw V4L2 and Hardware ISP streams.

---

## 2. Supported Sensor Modes

| Mode ID | Resolution | Max Framerate | Pixel Format | Description |
| :---: | :---: | :---: | :---: | :--- |
| **Mode 0** | 3840×2160 | 30 fps | RAW10 / YUV420 / NV12 | Full-sensor 12.3MP 4K capture |
| **Mode 1** | 1920×1080 | 60 fps | RAW10 / YUV420 / NV12 | 1080p high-definition standard capture |
| **Mode 2** | **1332×990** | **120 fps** | **RAW10 / YUV420 / NV12** | **2×2 Binned high-speed computer vision mode** |

---

## 3. Verified Performance Summary

| Ingest Path | Interface / Element | Resolution | Measured FPS | Inter-frame Delta | Packet Drops | Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: |
| **Raw V4L2** | `v4l2-ctl` / `capture120` | 1332×990 RAW10 | **120.165 fps** | 8.32 ms | 0 / 600 frames | **PASS** |
| **Hardware ISP** | Libargus `EGLStream::FrameConsumer` | 1332×990 NV12 | **120.243 fps** | 8.32 ms | 0 / 360 frames | **PASS** |
| **GStreamer** | `nvarguscamerasrc` | 1332×990 NV12 | ~60.504 fps | ~16.5 ms | Lockstep handoff | Bottleneck identified |

> [!NOTE]
> Stock NVIDIA GStreamer `nvarguscamerasrc` throttles to ~60.5 fps due to internal single-buffer condition variable handoff (`is_argus_buffer_consumed`) coupled with synchronous VIC blits. Applications requiring real 120fps should consume frames directly via **Libargus EGLStream** (`tests/hardware/argus_fps_probe.cpp`).

---

## 4. Documentation Suite

Detailed architectural, mathematical, and implementation guides are available in the [`docs/`](docs/) directory:

- [**System Overview & Architecture**](docs/overview.md): End-to-end design, data flows, and V4L2 vs ISP comparisons.
- [**Kernel Driver & DTS Guide**](docs/driver.md): Sensor timing calculations (840MHz, LLP 6667, FLL 1049), DTS overlay, and out-of-tree build details.
- [**NVIDIA ISP & NITO Tuning**](docs/isp-tuning.md): Official IQ migration tooling, Knob Set 2 integration, and Libargus streaming.
- [**Performance Benchmarks**](docs/benchmarks.md): Comprehensive throughput, inter-frame intervals, and jitter measurements.
- [**Troubleshooting & Safety**](docs/troubleshooting.md): Hot-unload safety warnings, recovery steps, and hardware diagnostics.

---

## 5. Repository Structure

```text
IMX477-120fps/
├── README.md               # Main project documentation
├── LICENSE                 # GPL-2.0 License
├── .gitignore              # Build and binary exclusion rules
├── docs/                   # Detailed technical documentation
│   ├── overview.md         # System architecture & capture pathways
│   ├── driver.md           # Timing mathematics & driver internals
│   ├── isp-tuning.md       # NVIDIA ISP & NITO generation guide
│   ├── benchmarks.md       # Verification & benchmark results
│   └── troubleshooting.md  # Operational safety & troubleshooting
├── driver/                 # Kernel driver source & patches
│   ├── src/                # Patched nv_imx477.c & imx477_mode_tbls.h
│   ├── dts/                # Device tree overlay (tegra234-p3767-camera-p3768-imx477-A.dts)
│   ├── patches/            # Upstream L4T R39.2.1 unified diff patches
│   └── include/            # Minimal platform headers for standalone compilation
├── src/                    # High-speed capture utilities
│   ├── capture/            # capture120 (Raw V4L2 mmap) & color_pipeline (C++/OpenCV)
│   └── preview/            # isp_preview.py & camera_quality_compare.py
├── scripts/                # Automation scripts
│   ├── driver/             # build-driver.sh, install-for-next-boot.sh, verify-after-boot.sh
│   └── isp/                # start-isolated-isp.sh (isolated daemon testing)
├── tests/hardware/         # verify_native.sh (V4L2) & verify_isp.sh (Libargus 120fps)
└── tools/                  # read_sensor.py (I2C dump) & raw_to_mp4.py
```

---

## 6. Getting Started

### 6.1 Prerequisites
- NVIDIA Jetson Orin Nano / Orin NX / AGX Orin running JetPack 6.x (Ubuntu 22.04 LTS / Kernel 5.15 or 6.8).
- Raspberry Pi High Quality Camera (Sony IMX477) connected to CAM0 / CAM1 via standard CSI ribbon cable.
- Standard build tools: `sudo apt install build-essential cmake libopencv-dev`.

### 6.2 Building the Kernel Driver
```bash
bash scripts/driver/build-driver.sh
```
Build output is saved to `build/driver/nv_imx477.ko`. ABI assertions verify data structure offsets statically.

### 6.3 Safe Installation & Reboot
> [!CAUTION]
> **NEVER attempt to hot-unload `nv_imx477` using `modprobe -r` or `rmmod` on a running system!**
> The Tegra camera host driver maintains active bus bindings; forced removal triggers an immediate kernel panic.

```bash
# Stage driver to /lib/modules/$(uname -r)/updates/ (creates automatic backup)
sudo bash scripts/driver/install-for-next-boot.sh

# Reboot to apply
sudo reboot
```

### 6.4 Building Capture Utilities
```bash
make -C src/capture
```
Compiles:
- `build/bin/capture120`: High-speed zero-copy V4L2 RAW10 ingest.
- `build/bin/color_pipeline`: Vectorized CPU/GPU debayering and color correction.
- `build/bin/argus_fps_probe`: Direct Libargus EGLStream 120fps throughput probe.

---

## 7. Verification & Benchmarking

### 7.1 Verify Native V4L2 120fps Ingest
```bash
./tests/hardware/verify_native.sh
```
Expected output:
```json
{
  "frames": 600,
  "errors": 0,
  "missing_sequences": 0,
  "measured_fps": 120.165,
  "sensor_geometry_matches": true,
  "passed": true
}
```

### 7.2 Verify Hardware ISP 120fps Ingest
```bash
./tests/hardware/verify_isp.sh
```
Expected output:
```json
{
  "frames": 360,
  "target_frames": 360,
  "exposure_us": 4000,
  "analog_gain": 2,
  "host_delivery_fps": 120.243,
  "frame_gaps": 0,
  "min_interval_ms": 7.07,
  "max_interval_ms": 9.70,
  "passed": true
}
```

---

## 8. License & Attribution

- Kernel driver, device tree, and patches: **GPL-2.0-only**
- Original Tegra Camera driver: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES
- RidgeRun IMX477 baseline: Copyright (c) 2020 RidgeRun
- 120fps Mode 2 enhancement and Libargus pipeline by YUjinEDU <20191906@edu.hanbat.ac.kr>
