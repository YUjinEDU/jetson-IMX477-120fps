# Camera pipeline scope

Validated target: Jetson Orin Nano Super 8GB, Ubuntu 24.04, L4T R39.2.1 /
JetPack 7.2.1, kernel 6.8.12-1021-tegra.

## Native RAW path

IMX477 → CSI/VI → V4L2 RG10 → mmap capture.
`color_pipeline.cpp` copies mmap data into cached RAM, applies a software
RAW-to-color transform and uses OpenCV CPU demosaicing. It is not GPU
debayering, calibrated ISP output or a zero-copy end-to-end pipeline.

## ISP path

IMX477 → CSI/VI → NVIDIA ISP → Libargus EGLStream FIFO → FrameConsumer.
`argus_fps_probe.cpp` enables capture metadata, requests fixed 4ms exposure /
2x sensor analog gain with AUTO white balance, checks timestamps/numbering and
image resolution, then releases frames. Optional JPEG encoding is outside the
timed measurement.

This establishes processed frame acquisition, not downstream RGB conversion,
display, recording or GPU inference throughput. Sensor and host timestamps
are kept in separate clock domains; no sensor-to-host latency is claimed.

## Image quality

Mode 0: 3840×2160 at 30fps; mode 1: 1920×1080 at 60fps; mode 2:
1332×990 at 120fps. Resolution, crop, readout, exposure and tuning differ.
Binning sacrifices spatial resolution for speed. This sensor/lens assembly
does not provide software-controlled optical focus through this application.
Focus/aperture/lighting and calibrated color need separate assessment.

The hardware tests do not require a monitor. Images are local-only by default.
