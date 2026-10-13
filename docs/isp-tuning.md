# ISP setup and limitations

## Local tuning requirement

The original IMX477 NITO on this device did not supply the mode-2 mapping.
An official NVIDIA Customer IQ Migration Tools conversion added knob set 2
while preserving existing entries. This is **not** a newly measured lens/color
calibration and does not establish high-speed TNR/LSC quality.

Tools were obtained from the official JetPack downloads page:
https://developer.nvidia.com/embedded/jetpack/downloads

Version: R39.2.1 / JetPack 7.2.1. Native ARM64 `nvcfg2nito` merged
`binary-20261013-144021_2.cfg` with the stock NITO using knob ID 2:

```bash
nvcfg2nito -t /path/to/libnvm_cam_tuning_l4t_cfg2nito.so \
  -s /path/to/stock-imx477.nito -i /path/to/mode2.cfg -k 2 \
  -o /path/to/imx477-mode012-candidate.nito
```

Use the converter and legacy CONFIG migration instructions supplied with that
exact official package. The tools, configuration dump and NITO binaries are
not included here. A fresh clone alone cannot regenerate the local CFG:
sensor configuration migration must be repeated using the official tools.

## This device's installed files

- Installed `/var/nvidia/nvcam/settings/imx477.nito`:
  `47589ea55f9407f9b5904fca57be0ad76d5e61ba8c261d686b6ddbd4e56065f1`
- Original `/var/nvidia/nvcam/settings/imx477.nito.stock`:
  `46bd3bf5db07fca555efb3758399253632d52dffdf3dfb766bacac3612dab4a9`

These hashes identify this experiment, not a universally applicable tuning file.
Back up and verify files before replacing them; stop all camera clients and
restart Argus only when idle. Never byte-patch a NITO.

## Direct Argus versus GStreamer

Direct FIFO acquisition now passes a >60-second 120fps test with enabled
metadata. The previous GStreamer pipeline measured approximately 60fps.
Earlier source analysis suggests synchronization/copy overhead, but the exact
cost of each operation and cause of half-rate output have not been isolated.
Do not present the double-blit explanation as a quantitatively proven cause.

AUTO AWB is explicitly requested. Actual scene color, exposure, focus and
calibration must still be inspected; the throughput PASS is not an IQ PASS.
