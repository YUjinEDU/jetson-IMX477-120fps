# ISP Mode 2 (1332×990 @ 120fps) Status & Resolution Report

**Date**: 2026-10-13  
**Platform**: Jetson Orin Nano Super 8GB, JetPack 6.x (L4T R39.2.1 / R36.x kernel 5.15)  
**Sensor**: Sony IMX477 (Raspberry Pi High Quality Camera, 2-lane MIPI CSI-2)

---

## 1. Executive Summary

- **Sensor Hardware & MIPI CSI-2**: Native 120fps verified (120.165 fps, 0 dropped frames).
- **ISP Hardware & RTCPU**: Submitting and processing VI/ISP tasks every **8.32ms (120 fps)** confirmed via ftrace.
- **Direct Libargus EGLStream Client**: **120.243 fps verified with 0 frame number skips** using `argus_fps_probe`.
- **NITO Tuning Profile**: Candidate NITO (`imx477-mode012-candidate.nito` with official Mode 2 knob set generated via NVIDIA Customer IQ Migration Tools) permanently installed to `/var/nvidia/nvcam/settings/imx477.nito`. Stock backed up. Modes 0 (30fps), 1 (60fps), and 2 (120fps) load cleanly without timeouts or missing knob set errors.
- **GStreamer `nvarguscamerasrc` Bottleneck Explained**: The stock `libgstnvarguscamerasrc.so` element suffers from an architectural lockstep single-buffer condition variable handoff (`is_argus_buffer_consumed`) coupled with double VIC blits (`copyToNvBuffer` + `NvBufSurfTransform`). It drops frames down to ~60.5fps when downstream consumption cannot complete within 8.3ms. Direct Argus EGLStream bypasses this overhead and achieves full 120fps.

---

## 2. Complete Performance Verification Matrix

| Capture Path | Method / Element | Commanded / Native | Measured FPS | Inter-frame Delta | Drops / Jumps |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Native Raw V4L2** | `v4l2-ctl` / `capture120.c` | 1332×990 @ 120fps | **120.165 fps** | 8.32 ms | 0 drops / 0 errors |
| **Hardware ISP (Direct)** | Libargus `FrameConsumer` | 1332×990 @ 120fps | **120.243 fps** | 8.32 ms (min 7.07, max 9.70) | **0 frame gaps** |
| **Hardware ISP (GStreamer)** | `nvarguscamerasrc` | 1332×990 @ 120fps | ~60.504 fps | ~16.5 ms | Lockstep handoff bottleneck |

---

## 3. Root Cause Analysis: The 60fps GStreamer Ceiling

Investigation of `gst-nvarguscamera` source code (`gstnvarguscamerasrc.cpp` from upstream `jetson_39.2.1`) revealed why GStreamer measured ~60fps while the sensor and ISP were actually streaming at 120fps:

1. **Lock-step Single-Buffer Rendition**:
   ```cpp
   // Consumer thread
   g_queue_push_tail (src->argus_buffers, (src->frameInfo));
   g_cond_signal (&src->argus_buffers_queue_cond);
   while (!src->is_argus_buffer_consumed) {
       g_cond_wait_until (&src->argus_buffer_consumed_cond, &src->argus_buffer_consumed_lock, until);
   }
   ```
2. **Double VIC Blit Overhead**:
   - The consumer thread executes `iNativeBuffer->copyToNvBuffer(src->frameInfo->fd)` (DMA/VIC copy).
   - Downstream `gst_nv_argus_camera_src_fill` performs `NvBufSurfTransform` before signaling `is_argus_buffer_consumed`.
3. **Queue Tail Drop**:
   - While waiting for `is_argus_buffer_consumed`, incoming events accumulate in `iEventQueue`.
   - The consumer loop only reads `getEvent(getSize() - 1)`, discarding intervening frames!
   - Because the round-trip handoff takes ~10–12ms (exceeding the 8.33ms 120fps budget), every alternate frame is discarded, resulting in the observed ~60.5 fps ceiling.
4. **Resolution**:
   - For 120fps computer vision and high-speed robotics, applications should consume frames directly via **Libargus `EGLStream::FrameConsumer`** (`build/bin/argus_fps_probe` or C++ capture nodes), completely bypassing GStreamer's synchronization bottleneck.

---

## 4. Verification Reproduction

### Native V4L2 120fps Regression
```bash
./tests/hardware/verify_native.sh
```

### Direct ISP 120fps Verification
```bash
./tests/hardware/verify_isp.sh
```
Output:
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
