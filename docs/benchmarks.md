# Camera Throughput & Timing Benchmark Results

## 1. Test Environment

- **Target Hardware**: NVIDIA Jetson Orin Nano Super (8GB Unified Memory, 25W Mode)
- **Host OS**: Ubuntu 22.04 LTS (JetPack 6.x / L4T R39.2.1 / Kernel 5.15)
- **Sensor**: Sony IMX477 (Raspberry Pi High Quality Camera, 12.3MP)
- **Interface**: 2-lane MIPI CSI-2 via CAM0
- **Power & Clock Profile**: Standard headless mode (no artificial thermal or clock overrides)

---

## 2. Ingest Throughput & Frame Delivery Matrix

| Ingest Pathway | Interface / Element | Resolution & Mode | Target FPS | Measured Delivered FPS | Inter-frame Delta | Packet Drops / Gaps | Validation Status |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Native Raw V4L2** | `v4l2-ctl` / `capture120.c` | 1332×990 RAW10 (Mode 2) | 120.0 fps | **120.165 fps** | 8.32 ms avg | **0 drops** (600 frames) | **PASS** |
| **Hardware ISP (Direct)** | Libargus `EGLStream::FrameConsumer` | 1332×990 NV12 (Mode 2) | 120.0 fps | **120.243 fps** | 8.32 ms (min 7.07, max 9.70) | **0 frame gaps** (360 frames) | **PASS** |
| **Hardware ISP (GStreamer)** | `nvarguscamerasrc` | 1332×990 NV12 (Mode 2) | 120.0 fps | ~60.504 fps | ~16.5 ms | Lockstep handoff skips | Bottleneck Identified |
| **Mode 1 Regression** | Libargus / GStreamer | 1920×1080 (Mode 1) | 60.0 fps | **60.000 fps** | 16.67 ms | 0 drops | **PASS** |
| **Mode 0 Regression** | Libargus / GStreamer | 3840×2160 (Mode 0) | 30.0 fps | **29.999 fps** | 33.33 ms | 0 drops | **PASS** |

---

## 3. Inter-Frame Arrival Cadence & Jitter Analysis

### Direct Libargus EGLStream Cadence (Mode 2, 120fps)
Across 360 measured frames at fixed 4ms exposure and 2x analog gain:
- **Total captured frames**: 360
- **Sensor / VI Task Submission Cadence**: 8.325 ms (verified via kernel `ftrace` on `vi_task_submit` and `isp_task_submit`)
- **Host Delivery FPS**: **120.243 fps**
- **Consecutive frame gaps != 1**: **0** (strictly sequential frame numbers)
- **Minimum inter-frame interval**: 7.07 ms
- **Maximum inter-frame interval**: 9.70 ms
- **Standard Jitter**: < 1.2 ms

### Native V4L2 Raw DMA Cadence (Mode 2, 120fps)
Across 600 measured frames using zero-copy `mmap` buffers:
- **Total captured frames**: 600
- **Elapsed time**: 4.993 seconds
- **Measured Ingest FPS**: **120.165 fps**
- **Corrupted buffers (`V4L2_BUF_FLAG_ERROR`)**: **0**
- **Missing sequence numbers**: **0**

---

## 4. Hardware ISP vs Raw Ingest CPU Overhead

| Pipeline Configuration | Core Utilization | Memory RSS | Latency (Sensor to App) |
| :--- | :---: | :---: | :---: |
| **Hardware ISP (Libargus EGLStream)** | ~12% (1 core) | ~45 MiB | ~8.5 ms |
| **Native Raw V4L2 (Direct DMA mmap)** | ~8% (1 core) | ~18 MiB | < 1.0 ms |
| **Raw V4L2 + Software OpenCV Debayer** | ~84% (1 core) | ~78 MiB | ~8.3 ms |

### Summary
The Hardware ISP offloads 100% of the demosaicing, bad-pixel correction, and lens shading computation to the Tegra ISP hardware engine, allowing host CPU cores to remain almost entirely idle while receiving fully processed, tone-mapped YUV420/NV12 images at 120fps.
