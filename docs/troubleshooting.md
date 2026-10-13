# Safety and recovery

## Camera ownership

Only one camera client may run. Inspect exact PIDs before stopping a client:

```bash
pgrep -fa 'argus_fps_probe|capture120|color_pipeline|isp_preview'
```

Prefer SIGTERM to the confirmed process, allowing cleanup. Do not blindly use
killall or SIGKILL. Restart nvargus-daemon only after capture clients exit.

Never hot-unload or unbind nv_imx477: this device previously panicked during
module removal. A reviewed on-disk replacement takes effect after reboot.

## Driver recovery

There is no verified factory .ko.stock at the previously documented location.
Do not copy nonexistent backups or ignore failed recovery commands.

The installer now preserves a non-empty, vermagic-matched pre-install module
and SHA256 manifest under /var/lib/imx477-120fps/<kernel>/:

```bash
sudo bash scripts/driver/install-for-next-boot.sh --backup-only
sudo bash scripts/driver/install-for-next-boot.sh --restore
# Only reboot after verifying the restored module and matching boot device tree.
```

The backup may be the already-patched known-good module, not factory stock.
Driver recovery does not restore DTBO/boot configuration or ISP tuning.
Keep separate checked backups for those components.

## NITO recovery

This device's original NITO is /var/nvidia/nvcam/settings/imx477.nito.stock,
SHA256 46bd3bf5db07fca555efb3758399253632d52dffdf3dfb766bacac3612dab4a9.
Verify this backup before use, stop capture clients, then restore it and
restart the daemon. The original NITO removes mode-2 ISP support.

## Failed probes

Missing/zero timestamps, non-monotonic sensor times, frame-number gaps,
unexpected image size, out-of-range FPS, or failed acquisition are FAIL.
Host-arrival jitter is reported separately from sensor intervals.
Zero sensor timestamps must not be replaced with host times and labeled sensor FPS.

For timeouts inspect daemon/kernel logs; do not immediately modify PLL/DT values.
Do not scan/write the I2C bus or change trace settings without a specific need.

A blue tint or soft focus is an image-quality issue even if throughput passes.
Check AUTO WB metadata, known neutral/color targets, lens focus, aperture,
lighting and mode-specific tuning. Do not equate an encoded JPEG with calibrated IQ.
