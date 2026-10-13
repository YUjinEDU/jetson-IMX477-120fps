# IMX477 120fps on NVIDIA Jetson

Experimental driver modifications and camera verification tools for the Sony IMX477.
Tested on **Jetson Orin Nano Super 8GB**, Ubuntu 24.04, JetPack 7.2.1 /
L4T R39.2.1, kernel `6.8.12-1021-tegra`. Other Jetsons and JetPack versions
have not been validated. Do not treat this as a production-certified release.

## Verified scope

- Native V4L2: 1332×990 RAW10 at approximately 120fps.
- Direct Libargus ISP: 1332×990 YUV420, over 60 seconds, approximately
  120.16fps, consecutive frame numbers, valid sensor timestamps.
- ISP mode 0: 3840×2160 at 30fps; mode 1: 1920×1080 at 60fps.
- JPEG samples are decoded at the requested resolution. This does not establish
  calibrated color, optical focus, or image-quality equivalence across modes.
- The previous GStreamer mode-2 path measured approximately 60fps. Direct Argus
  is a working alternative; a precise measured GStreamer root cause is not established.

Mode 2 uses binning/cropping, not the sensor's full 12.3MP resolution.
3840×2160 is approximately 8.3MP, not 12.3MP.

## Safety

**Never hot-unload or unbind `nv_imx477`.** This device previously suffered a
kernel panic during module removal. Stage reviewed changes on disk, then reboot.
Only one camera client may run at a time. Stop the exact capture process cleanly.

This repository does not distribute NVIDIA NITO/CFG tuning binaries or IQ tools.
A locally generated mode-2 NITO is required for ISP mode 2.
See [ISP setup](docs/isp-tuning.md) before attempting ISP verification.

## Build and verify

Install build tools, matching kernel/NVIDIA headers, Libargus development files,
OpenCV development libraries and `v4l2-ctl` for your JetPack release.

```bash
make -C src/capture
./tests/hardware/verify_isp.sh 2 7261  # >60 seconds, no display or inference
./tests/hardware/verify_isp.sh 1 600
./tests/hardware/verify_isp.sh 0 300
./tests/hardware/verify_native.sh     # run separately, not concurrently
```

The current Makefile targets system OpenCV 4.6 SONAMEs. Adapt it for other
versions. The Argus probe checks host and sensor FPS, metadata validity, sensor
intervals, frame numbering and image-plane resolution. Summary JSON and
per-frame CSV are saved under `build/test_results/`.

To save three private image samples **after** the timed test:

```bash
build/bin/argus_fps_probe --mode 2 --frames 7261 \
  --output build/test_results/isp120.json \
  --sample-prefix build/test_results/sample
```

Samples are not published. The probe consumes processed ISP frames; it is not
a continuous RGB export, GPU preprocessing, inference or display benchmark.

## Driver development

```bash
bash scripts/driver/build-driver.sh
sudo bash scripts/driver/install-for-next-boot.sh --backup-only
sudo bash scripts/driver/install-for-next-boot.sh
# Reboot only after reviewing the module AND device-tree installation plan.
```

The installer stages only the kernel module, **not** the DTS/DTBO. The driver
requires the matching mode-2 device tree; see [driver notes](docs/driver.md).
Its backup is the pre-install module, not necessarily the factory module.

## Documentation and license

[Architecture](docs/overview.md) · [Driver](docs/driver.md) ·
[ISP](docs/isp-tuning.md) · [Measurements](docs/benchmarks.md) ·
[Recovery](docs/troubleshooting.md)

Driver and DTS retain their upstream NVIDIA/RidgeRun copyright and GPL-2.0
notices. See [LICENSE](LICENSE). The source baseline is L4T R39.2.1; modifications
are also provided as patches under `driver/patches/`. NVIDIA SDK tools and
tuning artifacts remain subject to their own licenses and are not redistributed.
