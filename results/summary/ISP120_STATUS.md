# Reviewed ISP120 status

Updated 2026-10-13. See [benchmarks](../../docs/benchmarks.md) and the
machine-readable summaries in this directory.

Direct Libargus FIFO mode 2 was verified for 7,261 consecutive frames over
60.4183 seconds, with enabled metadata: sensor 120.162fps, host 120.161fps,
zero frame-number gaps and zero missing timestamps. Modes 0 and 1 also passed.

The previous 360-frame host-only tests omitted metadata enablement and produced
invalid sensor_fps=inf JSON. They are superseded for sensor-timing claims.
The working Direct Argus path does not establish continuous RGB/display/encoding
throughput or calibrated image quality.

Historical source analysis and ftrace logs remain in local build/logs/isp.
Source synchronization/VIC-copy behavior suggests possible GStreamer overhead;
the exact reason for the approximately 60fps GStreamer delivery is not isolated.
Earlier claims of measured 10–12ms handoff, production certification, calibrated
LSC/TNR and absolute sensor-to-host latency are not validated by this review.

The installed mode-2 NITO and checked stock backup are documented in
[ISP setup](../../docs/isp-tuning.md). Proprietary tuning/tools and camera images
are not distributed with this repository.
