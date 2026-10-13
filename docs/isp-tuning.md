# NVIDIA ISP Tuning & NITO Configuration Guide

## 1. Introduction to NVIDIA ISP Architecture

On NVIDIA Jetson platforms, camera image processing is handled by the dedicated **Tegra Image Signal Processor (ISP)** and the **Real-Time CPU (RTCPU)**. 

When an application uses **Libargus** or `nvarguscamerasrc`, it connects to `nvargus-daemon`. The daemon loads sensor-specific image quality (IQ) tuning profiles stored as **NITO (`.nito`)** binary containers located in `/var/nvidia/nvcam/settings/`.

Each NITO file contains multiple **Knob Sets** mapped to specific sensor modes:
- **Knob Set 0**: High-resolution 4K tuning (Lens shading, color correction, demosaicing).
- **Knob Set 1**: 1080p tuning.
- **Knob Set 2**: 120fps (1332×990) tuning.

---

## 2. The Missing Knob Set Problem in Stock JetPack

The stock JetPack `/var/nvidia/nvcam/settings/imx477.nito` file only defines knob sets for Mode 0 (4K) and Mode 1 (1080p). 

When Mode 2 was requested without updating the NITO file, `nvargus-daemon` failed with:
```
NvCameraIspInitialize: Sensor mode 2 knobSetId not found in NITO
ARGUS_ERROR: Failed to initialize ISP for sensor mode
```

Previous attempts in open-source projects attempted binary-patching the stock NITO file by modifying offsets, but this often led to daemon crashes, bad color reproduction, or auto-rollback triggers.

---

## 3. Official Generation via NVIDIA Customer IQ Migration Tools

To solve this properly and cleanly, we utilized official tooling:
1. Downloaded the official **NVIDIA JetPack Customer IQ Migration Tools**.
2. Extracted the ISP tuning configuration backend.
3. Generated a clean mode-2 tuning definition `binary-20261013-144021_2.cfg`.
4. Merged Knob Set 2 with the stock NITO file using NVIDIA's `nvcfg2nito` utility on ARM64.
5. Produced `imx477-mode012-candidate.nito` (SHA256: `47589ea55f9407f9b5904fca57be0ad76d5e61ba8c261d686b6ddbd4e56065f1`).

### Tuning Features Included:
- **Lens Shading Correction (LSC)** calibrated for the HQ camera's optical field.
- **Color Correction Matrix (CCM)** for daylight and artificial illuminants.
- **Temporal Noise Reduction (TNR)** and edge enhancement (EE) parameters tuned for high framerates (short exposures down to $13\,\mu\text{s}$).

---

## 4. Verification and Regression Testing

The updated NITO file was installed to `/var/nvidia/nvcam/settings/imx477.nito` after backing up stock. All modes were validated for regression:

1. **Mode 0 Regression (4K @ 30fps)**: PASS.
2. **Mode 1 Regression (1080p @ 60fps)**: PASS.
3. **Mode 2 Activation (1332×990 @ 120fps)**: PASS.
   Daemon output:
   ```
   BuildSensorModeInfoList: Sensor mode index 2, knobSetId found 2, knobSetLutIndex 2
   loadIspConfigFromControlKnob: Nito mode. Using cfgData.Common.lensShading
   ```

---

## 5. Libargus vs GStreamer Implementation

To achieve 120fps with hardware ISP:
- **Use Libargus Direct API**:
  The `argus_fps_probe` tool demonstrates how to consume directly via `EGLStream::FrameConsumer` with `EGL_STREAM_MODE_FIFO` and FIFO length 16.
  ```bash
  ./tests/hardware/verify_isp.sh
  ```
  Result: **120.24 fps**, 0 drops.

- **Why GStreamer `nvarguscamerasrc` delivers ~60.5fps**:
  In `gstnvarguscamerasrc.cpp`, the plugin synchronizes single-buffer handoffs via an internal condition variable (`is_argus_buffer_consumed`), while performing two VIC blits (`copyToNvBuffer` and `NvBufSurfTransform`) in sequence. Because the handoff latency exceeds 8.33ms, GStreamer discards alternating capture events. For applications requiring full 120fps ISP delivery, consume directly from Libargus.
