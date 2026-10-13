#!/usr/bin/env python3
"""Independently audit probe JSON/CSV without accessing the camera."""
import argparse
import csv
import json
import math
from pathlib import Path


def reject_constant(value):
    raise ValueError(f"Non-finite JSON constant: {value}")


def audit(summary, rows):
    n = summary["target_frames"]
    assert n >= 2 and len(rows) == n == summary["frames"], "Incomplete capture"
    numbers = [int(row["frame_number"]) for row in rows]
    sensor = [int(row["sensor_timestamp_ns"]) for row in rows]
    host = [int(row["host_monotonic_ns"]) for row in rows]
    exposures = [int(row["exposure_ns"]) for row in rows]
    fps = summary["target_fps"]
    assert all(v > 0 for v in sensor + host + exposures), "Missing timestamp/exposure"
    assert all(b == a + 1 for a, b in zip(numbers, numbers[1:])), "Frame-number gap"
    for times in (sensor, host):
        assert all(b > a for a, b in zip(times, times[1:])), "Non-monotonic time"
        measured = (n - 1) * 1e9 / (times[-1] - times[0])
        assert math.isfinite(measured) and abs(measured / fps - 1) <= .02, "FPS out of range"
    assert all(.5 * 1e9 / fps <= b - a <= 1.5 * 1e9 / fps
               for a, b in zip(sensor, sensor[1:])), "Invalid sensor interval"
    assert summary["geometry_ok"] and summary["samples_ok"] and summary["passed"], "Probe failed"
    return {"passed": True, "frames": n,
            "sensor_fps": (n - 1) * 1e9 / (sensor[-1] - sensor[0]),
            "host_fps": (n - 1) * 1e9 / (host[-1] - host[0])}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("summary", type=Path)
    args = parser.parse_args()
    with args.summary.open() as stream:
        summary = json.load(stream, parse_constant=reject_constant)
    with Path(str(args.summary) + ".frames.csv").open() as stream:
        rows = list(csv.DictReader(stream))
    print(json.dumps(audit(summary, rows), indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
