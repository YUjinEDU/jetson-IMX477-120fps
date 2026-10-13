#!/usr/bin/env python3
"""Convert captured Jetson RAW16 Bayer frames to a 120 fps H.264 MP4.

This is an offline conversion. Original RAW samples and sensor timestamps
remain available for later ISP processing and frame timing verification.
"""
import argparse
import csv
import json
from pathlib import Path
import time
import cv2
import numpy as np

parser = argparse.ArgumentParser()
parser.add_argument('raw', type=Path)
parser.add_argument('mp4', type=Path)
parser.add_argument('--stride', type=int, default=2688)
parser.add_argument('--black-level', type=int, default=64)
parser.add_argument('--gamma', type=float, default=2.2)
args = parser.parse_args()
width, height, fps = 1332, 990, 120
if args.mp4.exists():
    raise SystemExit(f'Refusing to overwrite {args.mp4}')
frame_bytes = args.stride * height
size = args.raw.stat().st_size
if size % frame_bytes or not size:
    raise SystemExit('RAW file size does not contain complete frames')
if args.stride < width * 2 or args.stride % 2:
    raise SystemExit('Invalid row stride')
if not 0 <= args.black_level < 1023 or args.gamma <= 0:
    raise SystemExit('Invalid black level/gamma')
count = size // frame_bytes
with Path(str(args.raw) + '.csv').open(newline='') as f:
    rows = list(csv.DictReader(f))
if len(rows) != count:
    raise SystemExit('Timestamp count differs from RAW frame count')
timestamps = np.array([int(row['timestamp_us']) for row in rows], dtype=np.int64)
sequences = np.array([int(row['sequence']) for row in rows], dtype=np.int64)
if np.any(np.diff(timestamps) <= 0) or np.any(np.diff(sequences) != 1):
    raise SystemExit('Non-monotonic timestamps or missing capture sequences')
sensor_fps = (count - 1) * 1e6 / (timestamps[-1] - timestamps[0])
pipeline = ('appsrc ! videoconvert ! video/x-raw,format=I420 ! '
            'x264enc speed-preset=ultrafast tune=zerolatency bitrate=12000 ! '
            'h264parse ! qtmux ! filesink location="' + str(args.mp4.resolve()) + '"')
writer = cv2.VideoWriter(pipeline, cv2.CAP_GSTREAMER, 0, fps, (width, height))
if not writer.isOpened():
    raise SystemExit('Could not open H.264 writer')
# Jetson expands RAW10 to 16 bits. Recover its upper 10 bits, subtract
# the sensor black offset, and apply a fixed display gamma.
levels = np.arange(1024, dtype=np.float32)
lut = np.round(np.clip((levels - args.black_level) / (1023 - args.black_level),
                       0, 1) ** (1 / args.gamma) * 255).astype(np.uint8)
started = time.monotonic()
try:
    with args.raw.open('rb') as f:
        for index in range(count):
            data = f.read(frame_bytes)
            if len(data) != frame_bytes:
                raise RuntimeError('Truncated frame')
            raw = np.frombuffer(data, dtype='<u2').reshape(height, args.stride // 2)[:, :width]
            bayer = lut[raw >> 6]
            # COLOR_BayerBG2BGR corresponds to an RGGB array in OpenCV's
            # historical two-letter naming; sensor solid-red test verifies it.
            frame = cv2.cvtColor(bayer, cv2.COLOR_BayerBG2BGR)
            writer.write(frame)
            if index == 0:
                cv2.imwrite(str(args.mp4.with_suffix('.jpg')), frame)
            if (index + 1) % 120 == 0:
                print(f'Converted {index + 1}/{count}', flush=True)
finally:
    writer.release()
cap = cv2.VideoCapture(str(args.mp4))
decoded = 0
while True:
    ok, frame = cap.read()
    if not ok:
        break
    if frame.shape[:2] != (height, width):
        raise RuntimeError('Unexpected decoded resolution')
    decoded += 1
file_fps = cap.get(cv2.CAP_PROP_FPS)
cap.release()
if decoded != count or abs(file_fps - fps) > 0.001:
    raise RuntimeError(f'Encoded video verification failed: {decoded=}, {file_fps=}')
report = dict(frames=count, sensor_fps=sensor_fps, video_fps=file_fps,
              decoded_frames=decoded, width=width, height=height,
              raw_stride=args.stride, nominal_duration_seconds=count/fps,
              timestamp_delta_us=dict(min=int(np.diff(timestamps).min()),
                                      max=int(np.diff(timestamps).max())),
              conversion_seconds=time.monotonic()-started,
              processing='offline fixed black level, RGGB demosaic, fixed gamma; no calibrated ISP')
args.mp4.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
