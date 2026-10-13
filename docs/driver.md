# IMX477 Kernel Driver & Device Tree Guide

## 1. Overview

The stock L4T IMX477 driver (`nv_imx477.c`) supports only two sensor modes:
- Mode 0: 3840×2160 @ 30fps
- Mode 1: 1920×1080 @ 60fps

To support **Mode 2: 1332×990 @ 120fps**, changes were implemented across three key areas:
1. **Sensor Register Initialization**: Register sequence for 2×2 on-sensor binning / analog crop.
2. **Timing Arithmetic**: Frame length line (FLL), line length pixel (LLP), and pixel clock calculation.
3. **Device Tree Overlay (DTS)**: Node properties, pixel clock advertisements, and VI/CSI endpoint routing.

---

## 2. Sensor Timing Mathematics

The IMX477 internal timing operates with an on-chip PLL fed by an external 24 MHz oscillator, generating an internal system clock of **840 MHz**.

The frame period $T_{\text{frame}}$ is given by:

$$T_{\text{frame}} = \frac{\text{Line Length (LLP)} \times \text{Frame Length (FLL)}}{\text{Pixel Clock Frequency}}$$

For Mode 2 (1332×990 @ 120fps):
- **Pixel Clock**: $840,000,000\text{ Hz}$
- **Line Length (LLP / `0x0342` & `0x0343`)**: $6667\text{ pixels}$ (`0x1a0b`)
- **Frame Length (FLL / `0x0340` & `0x0341`)**: $1049\text{ lines}$ (`0x0419`)

Calculating the resulting frame time:
$$T_{\text{frame}} = \frac{6667 \times 1049}{840,000,000} \approx \frac{6,993,683}{840,000,000} \approx 0.0083258\text{ seconds} \approx 8.326\text{ ms}$$

$$\text{Frame Rate} = \frac{1}{0.0083258} \approx 120.108\text{ fps}$$

This matches our empirical measurement of **120.165 fps** within $0.05\%$.

---

## 3. Register Configuration (`imx477_mode_tbls.h`)

Mode 2 register settings configure the sensor into a 2×2 binned readout mode:
- Output width: $1332$ (`0x034c: 0x05`, `0x034d: 0x34`)
- Output height: $990$ (`0x034e: 0x03`, `0x034f: 0xde`)
- Binning mode: 2×2 digital/analog binning enabled (`0x0900: 0x01`, `0x0901: 0x22`)
- Exposure range: Min 1 line ($13\,\mu\text{s}$), Max FLL - 4 lines ($8.3\,\text{ms}$ at 120fps).

---

## 4. Device Tree (`tegra234-p3767-camera-p3768-imx477-A.dts`)

The device tree overlay defines the mode for the Tegra Video Input (VI) driver:
```dts
mode2 {
    mclk_khz = "24000";
    num_lanes = "2";
    tegra_sinterface = "serial_a";
    phy_mode = "DPHY";
    discontinuous_clk = "yes";
    dpcm_enable = "false";
    cil_settletime = "0";

    active_w = "1332";
    active_h = "990";
    mode_type = "bayer";
    pixel_phase = "rggb";
    csi_pixel_bit_depth = "10";
    readout_orientation = "0";
    line_length = "6667";
    inherent_gain = "1";
    mclk_multiplier = "35";
    pix_clk_hz = "300000000";

    min_gain_val = "1.0";
    max_gain_val = "22.25";
    min_exp_time = "13";
    max_exp_time = "683709";
    min_hdr_ratio = "1";
    max_hdr_ratio = "1";
    min_framerate = "1.5";
    max_framerate = "120.0";
};
```

---

## 5. Out-of-Tree Driver Build

Instead of compiling the entire 200MB+ Jetson kernel source tree, this repository provides a standalone out-of-tree build script:

```bash
./scripts/driver/build-driver.sh
```

### How it works:
1. Validates that running kernel headers exist in `/lib/modules/$(uname -r)/build`.
2. Generates a standalone Kbuild Makefile.
3. Compiles `driver/src/nv_imx477.c` against running kernel ABI.
4. Performs verification assertions on the resulting `build/driver/nv_imx477.ko`:
   - Checks kernel module version string (`vermagic`).
   - Verifies CRC checksums of key V4L2/Tegra exported symbols (`tegra_camera_device_register`, `v4l2_subdev_init`, etc.).

---

## 6. Safe Driver Deployment & Reboot Requirement

> [!CAUTION]
> **NEVER attempt to hot-unload `nv_imx477` with `modprobe -r` or `rmmod`!**
> The Tegra camera host driver retains active bus bindings; unloading the module while bound causes an immediate kernel panic and system reboot.

To install a freshly built kernel module safely:
```bash
./scripts/driver/install-for-next-boot.sh
sudo reboot
```

After rebooting, verify that the module loaded correctly:
```bash
./scripts/driver/verify-after-boot.sh
```
