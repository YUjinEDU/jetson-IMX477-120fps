# Troubleshooting & Operational Safety Guide

## 1. Safety Rules & Critical Warnings

### Rule 1: Never Hot-Unload the Kernel Module (`nv_imx477`)
> [!CAUTION]
> **DO NOT run `modprobe -r nv_imx477` or `rmmod nv_imx477` on a running system!**
> The Tegra Camera Platform driver (`tegra-camera`) and VI engine maintain active devnode links to the sensor driver. Forcibly unbinding or removing `nv_imx477` while the kernel host driver is active triggers an immediate kernel NULL pointer dereference / panic, resulting in an unrecoverable freeze or instant reboot.
>
> **Correct procedure**: Always copy new `.ko` modules to `/lib/modules/$(uname -r)/updates/` and reboot the Jetson gracefully using `sudo reboot`.

### Rule 2: Do Not Byte-Patch Stock NITO Files
> [!WARNING]
> Do not attempt to hex-edit or offset-patch `/var/nvidia/nvcam/settings/imx477.nito`. Corrupted NITO files will cause `nvargus-daemon` to segfault or trigger an automated fallback loop. Always use officially generated NITO files containing verified knob sets.

---

## 2. Common Issues & Diagnostics

### Issue A: "Camera device not found" / `/dev/video0` Missing
1. **Check CSI Ribbon Cable**: Ensure the cable is oriented correctly (contacts facing the board on standard Jetson carriers, silver pins facing down/blue backing facing up depending on slot).
2. **Check I2C Communication**:
   ```bash
   sudo i2cdetect -y -r 9
   ```
   Address `0x1a` should be displayed as `UU` (owned by driver) or `1a`.
3. **Check Kernel Messages**:
   ```bash
   dmesg | grep -i imx477
   ```
   Expected output:
   ```
   imx477 9-001a: tegracam sensor driver:imx477_v2.0.6
   imx477 9-001a: Detected IMX477 sensor
   ```

### Issue B: Frame Capture Timeout (`PXL_SOF syncpt timeout`)
- **Symptoms**: `capture120` or `v4l2-ctl` hangs and outputs:
  ```
  tegra-camrtc-capture-vi tegra-capture-vi: PXL_SOF syncpt timeout!
  ```
- **Cause**: MIPI CSI-2 clock or data lane not receiving signal. Typically caused by:
  - Loose CSI ribbon cable.
  - Another process holding the camera open.
- **Fix**:
  1. Kill any existing capture processes: `killall -9 capture120 argus_fps_probe gst-launch-1.0 || true`
  2. Power off Jetson, reseat camera cable firmly at both ends, and power back on.

### Issue C: `nvargus-daemon` Fails or Hangs
- **Symptoms**: `argus_fps_probe` fails to connect or times out waiting for producer.
- **Fix**:
  Restart the daemon:
  ```bash
  sudo systemctl restart nvargus-daemon
  journalctl -u nvargus-daemon -n 50 --no-pager
  ```

---

## 3. Rollback & Recovery Procedures

### Rolling Back to Stock Driver:
If you need to revert to the factory kernel module:
```bash
sudo cp /lib/modules/$(uname -r)/kernel/drivers/media/i2c/nv_imx477.ko.stock \
        /lib/modules/$(uname -r)/updates/nv_imx477.ko || true
sudo depmod -a
sudo reboot
```

### Rolling Back to Stock NITO:
If you need to restore the factory ISP tuning file:
```bash
sudo cp /var/nvidia/nvcam/settings/imx477.nito.stock /var/nvidia/nvcam/settings/imx477.nito
sudo systemctl restart nvargus-daemon
```
