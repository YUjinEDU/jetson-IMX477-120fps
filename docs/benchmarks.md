# Camera validation — 2026-10-13

Target: Orin Nano Super 8GB, Ubuntu 24.04, JetPack 7.2.1 / L4T R39.2.1,
kernel 6.8.12-1021-tegra. Sequential tests; no inference or monitor rendering.
No driver, system library or NITO replacement was performed during this review.

| Path | Resolution | Frames | Sensor FPS | Host FPS | Gaps | Result |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Native V4L2 RAW10 | 1332×990 | 600 | 120.165* | not separately measured | 0 | PASS |
| Direct ISP mode 2 | 1332×990 | 7,261 | 120.162 | 120.161 | 0 | PASS |
| Direct ISP mode 1 | 1920×1080 | 600 | 59.999 | 60.001 | 0 | PASS |
| Direct ISP mode 0 | 3840×2160 | 300 | 30.000 | 29.996 | 0 | PASS |

*Native FPS is calculated from V4L2 buffer timestamps, not Argus metadata.
The native test also verified sensor output registers and no error-flagged buffers.

The reviewed mode-2 ISP test lasted 60.4183 seconds by sensor timestamps.
All 7,261 metadata timestamps were valid. Sensor intervals ranged approximately
7.538–9.114ms; frame numbers were consecutive. Host scheduling jitter is
recorded separately; it is not evidence of missing sensor frames.

Machine-readable summaries:
- [ISP120](../results/summary/isp120-reviewed.json)
- [ISP60](../results/summary/isp60-reviewed.json)
- [ISP30](../results/summary/isp30-reviewed.json)
- [Native120](../results/summary/native120-reviewed.json)

Full CSV and JPEG samples remain under ignored build/test_results/.
An independent CSV audit recalculated throughput and checked nonzero, increasing
timestamps, sensor intervals and frame sequence. Five offline unit tests passed,
including rejection of missing metadata, half-rate timing and sequence gaps.

## Corrections to the earlier probe

The old probe did not enable EGLStream metadata. Its sensor timestamps were
zero, sensor_fps was inf (invalid JSON), and PASS checked only host average FPS
and frame numbering. Those old PASS records should not be used for sensor timing.

The probe now enables metadata and requires valid sensor and host throughput.
It verifies output geometry and bounds connection/acquisition/idle waits.
AUTO AWB is explicitly requested. Three optional JPEGs are captured and encoded
after the timed measurement, so JPEG encoding is not benchmarked at 120fps.

## Image quality and scope

Saved JPEGs decoded at 1332×990, 1920×1080 and 3840×2160. The samples contain
scene detail and changing pixels, but scene color/focus is not calibrated.
A blue/cyan appearance and soft focus were observed. Without a known neutral
target and lens/lighting control, the cause is not determined.
AUTO WB metadata was active (mode-2 sample CCT approximately 2894K at test end).

These are frame acquisition tests, not continuous RGB export, display, video
encoding, pixel-by-pixel integrity, inference or long-term production qualification.
No sensor-to-host latency, CPU percentage or RSS claim is made: clock offset and
resource overhead were not independently measured here.

## Reproduce

```bash
make -C src/capture
./tests/hardware/verify_isp.sh 2 7261
./tests/hardware/verify_isp.sh 1 600
./tests/hardware/verify_isp.sh 0 300
./tests/hardware/verify_native.sh
python3 -m unittest discover -s tests/unit -v
```

Run camera tests serially. The earlier GStreamer mode-2 result was about 60fps;
the exact bottleneck remains an investigation, not a proven per-operation profile.
