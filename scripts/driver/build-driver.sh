#!/usr/bin/env bash
# Build patched nv_imx477.ko out-of-tree for current Jetson kernel.
# Verified on Jetson Orin Nano Super, kernel 6.8.12-1021-tegra (L4T R39.2.1 / JP 7.2.1).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KERNEL="$(uname -r)"
echo "Building nv_imx477.ko for kernel: $KERNEL"
echo "Repository root: $REPO_ROOT"

BUILD_DIR="$REPO_ROOT/build/driver"
mkdir -p "$BUILD_DIR/drivers/media/i2c" \
         "$BUILD_DIR/drivers/media/platform/tegra/camera" \
         "$BUILD_DIR/include/nvidia"

cp "$REPO_ROOT/driver/src/nv_imx477.c" "$BUILD_DIR/drivers/media/i2c/"
cp "$REPO_ROOT/driver/src/imx477_mode_tbls.h" "$BUILD_DIR/drivers/media/i2c/"
cp "$REPO_ROOT/driver/include/platform/tegra/camera/camera_gpio.h" \
   "$BUILD_DIR/drivers/media/platform/tegra/camera/"

# Check PMC power enable backport in kernel headers to guarantee ABI match
if grep -q 'tegra_pmc_io_pad_power_enable(struct tegra_pmc' \
    "/lib/modules/$KERNEL/build/include/soc/tegra/pmc.h" 2>/dev/null; then
    cat >"$BUILD_DIR/include/nvidia/conftest.h" <<'EOF'
#define NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG
#define NV_TEGRA_PMC_IO_PAD_POWER_ENABLE_PRESENT
EOF
else
    cat >"$BUILD_DIR/include/nvidia/conftest.h" <<'EOF'
#define NV_I2C_DRIVER_STRUCT_PROBE_WITHOUT_I2C_DEVICE_ID_ARG
EOF
fi

# Static assertion on camera_common_data offsets to prevent ABI shift bugs
cat >"$BUILD_DIR/drivers/media/i2c/abi_check.h" <<'EOF'
#include <linux/build_bug.h>
static_assert(offsetof(struct camera_common_data, mode) == 612);
static_assert(offsetof(struct camera_common_data, mode_prop_idx) == 616);
EOF

printf '\n#include "abi_check.h"\n' >>"$BUILD_DIR/drivers/media/i2c/nv_imx477.c"

cat >"$BUILD_DIR/drivers/media/i2c/Makefile" <<EOF
obj-m += nv_imx477.o
ccflags-y += -I\$(src)/../../../include -I/usr/src/nvidia/nvidia-public/include
EOF

make -C "/lib/modules/$KERNEL/build" M="$BUILD_DIR/drivers/media/i2c" \
    KBUILD_EXTRA_SYMBOLS=/usr/src/nvidia/nvidia-public/Module.symvers modules

OUTPUT_KO="$BUILD_DIR/nv_imx477.ko"
cp "$BUILD_DIR/drivers/media/i2c/nv_imx477.ko" "$OUTPUT_KO"
echo "Build successful: $OUTPUT_KO"
sha256sum "$OUTPUT_KO"
modinfo "$OUTPUT_KO" | head -12
