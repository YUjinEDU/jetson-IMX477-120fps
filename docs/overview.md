# IMX477 120fps Pipeline Overview & Architecture

## 1. Introduction

This repository provides a production-grade 120fps camera pipeline for the **Sony IMX477** (Raspberry Pi High Quality Camera) on the **NVIDIA Jetson Orin Nano / Orin series** (JetPack 6.x / L4T R39.2.1 / Kernel 5.15).

While stock NVIDIA drivers support 4K @ 30fps (Mode 0) and 1080p @ 60fps (Mode 1), this project introduces **Mode 2 (1332×990 @ 120fps)** across the entire stack:
1. **Kernel Driver & DTS**: High-bandwidth timing configuration (840 MHz pixel clock, 2-lane MIPI CSI-2).
2. **NVIDIA Hardware ISP**: Native Libargus support with verified ISP demosaicing and 3A algorithms via custom NITO tuning.
3. **Downstream High-Speed Processing**: Delivering uncompressed, high-cadence 120fps frames directly to GPU memory for real-time computer vision and machine learning applications.

---

## 2. System Architecture

The project supports two main capture paths depending on the application requirements:

```
[ Sony IMX477 Sensor ] (1332x990 @ 120fps, 2-lane MIPI CSI-2)
          │
          ▼
[ Tegra VI (Video Input) Engine ]
          │
    ┌─────┴────────────────────────────────┐
    │                                      │
    ▼ (Path A: Raw V4L2)                   ▼ (Path B: Hardware ISP)
[ V4L2 Video Node (/dev/video0) ]    [ Tegra ISP Engine & RTCPU ]
    │ (RAW10 Bayer)                        │ (Auto Exposure, WB, Tone Mapping)
    ▼                                      ▼
[ capture120 (C) ]                   [ nvargus-daemon ]
    │ (Direct DMA / mmap)                  │ (EGLStream Producer)
    ▼                                      ▼
[ color_pipeline (C++/OpenCV) ]      [ Direct Libargus EGLStream ]
    │ (Software Debayer/Color Correct)     │ (argus_fps_probe / EGL Consumer)
    └──────────────────┬───────────────────┘
                       │
                       ▼ (NV12 / BGR Frame Buffer)
       [ Zero-Copy / Shared Memory Ingest ]
                       │
                       ▼
    [ Real-Time Computer Vision / Deep Learning Pipeline ]
      (Object Tracking, Robotics, Optical Flow, TensorRT)
```

### Path A: Native Raw V4L2 Pipeline
- **Bypasses ISP completely**: Delivers raw Bayer RG10 frames directly from `/dev/video0`.
- **Latency**: Sub-millisecond sensor-to-host delivery.
- **Color Reconstruction**: Performed via vectorized OpenCV CPU/GPU debayering (`color_pipeline.cpp`) or custom shader/CUDA kernels.
- **Throughput**: Solid 120.165 fps (verified via `tests/hardware/verify_native.sh`).

### Path B: NVIDIA Hardware ISP Pipeline (Libargus)
- **Zero-CPU Demosaicing & 3A**: NVIDIA's dedicated hardware ISP handles demosaicing, bad-pixel correction, temporal noise reduction (TNR), lens shading correction (LSC), and auto-white-balance (AWB).
- **Direct EGLStream Consumer**: Uses native Libargus `EGLStream::FrameConsumer` with FIFO buffer depth 16 to achieve **120.24 fps** with **0 frame drops** (verified via `tests/hardware/verify_isp.sh`).
- **GStreamer Note**: Stock `libgstnvarguscamerasrc.so` element incurs lockstep thread synchronization overhead that throttles delivery to ~60.5fps. High-framerate applications must consume directly via Libargus.

---

## 3. Sensor Mode Specifications

| Mode ID | Resolution | Max FPS | MIPI Lanes | Pixel Clock | Formats Supported |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **0** | 3840×2160 (4K) | 30 fps | 2 | 840 MHz | RAW10, NV12, BGR |
| **1** | 1920×1080 (1080p) | 60 fps | 2 | 840 MHz | RAW10, NV12, BGR |
| **2** | **1332×990 (120fps)** | **120 fps** | **2** | **840 MHz** | **RAW10, NV12, BGR** |

---

## 4. Hardware & Software Requirements

- **Carrier Board & SoC**: NVIDIA Jetson Orin Nano Super / Orin Nano / Orin NX (P3768 carrier + P3767 module).
- **Camera Sensor**: Raspberry Pi High Quality Camera (Sony IMX477 sensor, 12.3MP, C/CS mount).
- **Cable**: Standard 15-pin to 22-pin or 22-pin 0.5mm pitch CSI ribbon cable.
- **Operating System**: Jetson Linux 36.x / 39.2.1 (JetPack 6.x, Ubuntu 22.04 LTS, Kernel 5.15).
- **Dependencies**:
  - GCC / G++ 11+
  - Libargus (`/usr/src/jetson_multimedia_api/include`, `/usr/lib/aarch64-linux-gnu/nvidia/libnvargus.so`)
  - OpenCV 4.x
  - TensorRT 10.x
