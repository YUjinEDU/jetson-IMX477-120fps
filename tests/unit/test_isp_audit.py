import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    "audit", Path(__file__).parents[1] / "hardware" / "audit_isp_results.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class AuditTest(unittest.TestCase):
    def setUp(self):
        self.summary = dict(target_frames=3, frames=3, target_fps=120,
                            geometry_ok=True, samples_ok=True, passed=True)
        self.rows = [dict(frame_number=i, sensor_timestamp_ns=1000000000 + i * 8333333,
                         host_monotonic_ns=2000000000 + i * 8333333, exposure_ns=4000000)
                     for i in range(3)]

    def test_valid(self):
        self.assertTrue(module.audit(self.summary, self.rows)["passed"])

    def test_missing_metadata(self):
        self.rows[1]["sensor_timestamp_ns"] = 0
        with self.assertRaises(AssertionError):
            module.audit(self.summary, self.rows)

    def test_sequence_gap(self):
        self.rows[1]["frame_number"] = 5
        with self.assertRaises(AssertionError):
            module.audit(self.summary, self.rows)

    def test_half_rate(self):
        for i, row in enumerate(self.rows):
            row["sensor_timestamp_ns"] = 1000000000 + i * 16666667
        with self.assertRaises(AssertionError):
            module.audit(self.summary, self.rows)

    def test_nonfinite_json(self):
        with self.assertRaises(ValueError):
            module.reject_constant("Infinity")


if __name__ == "__main__":
    unittest.main()
