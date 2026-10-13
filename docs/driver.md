# Driver and device-tree development

Baseline: L4T R39.2.1 NVIDIA/RidgeRun IMX477 source.
Only the present Orin Nano Super / kernel 6.8.12-1021-tegra is validated.

## Mode 2 timing

Output 1332×990, two MIPI lanes, 2×2 binning/crop.
The driver uses 840,000,000Hz for frame-length/exposure arithmetic,
line length 6667 and nominal frame length 1049:

`6667 × 1049 / 840000000 = 8.3258ms` (about 120.108fps).

The runtime DT advertises `pix_clk_hz=300000000`. This differs from the
driver's internal timing clock. Do not replace either value merely to make
them numerically match. Empirical sensor timestamps are the throughput evidence.

## Building

```bash
bash scripts/driver/build-driver.sh
```

Requires matching kernel headers plus `/usr/src/nvidia/nvidia-public` headers
and Module.symvers. The generated build asserts camera_common_data offsets
mode=612 and mode_prop_idx=616 with the relevant PMC conftest macro.
This intentionally fails on incompatible layouts. A matching vermagic alone
does not establish ABI compatibility.

## Deployment limits

`install-for-next-boot.sh` stages the module under
`/lib/modules/<kernel>/updates/drivers/media/i2c/nv_imx477.ko`.
It never unloads the running module. It **does not install the device tree**.

The DTS in `driver/dts/` must be compiled with its matching NVIDIA DTS includes
and integrated with the carrier's existing boot overlay configuration.
Record and back up the actual DTBO/boot selection before any change.
There is no universal automated DTBO installer in this release; do not assume
installing the module alone enables mode 2 on a fresh Jetson.

```bash
sudo bash scripts/driver/install-for-next-boot.sh --backup-only
sudo bash scripts/driver/install-for-next-boot.sh
# Review DTBO and boot selection, then reboot when authorized.
```

Backups live outside ignored build outputs in
`/var/lib/imx477-120fps/<kernel>/`, with a SHA256 manifest. They preserve the
pre-install module, which may already be patched. No factory backup is claimed.

## Regression

Never hot-unload/unbind this sensor. Run native and ISP tests serially.
`verify-after-boot.sh` remains an opt-in administrative tool; its GStreamer
EOS check alone is not an FPS measurement. Use the timestamp-based ISP probe
for throughput validation, and do not enable boot automation without checking
its installed ExecStart and backup.
