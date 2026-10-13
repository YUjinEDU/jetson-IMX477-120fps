#!/usr/bin/env bash
# Automated post-boot regression verification.
# Verifies native 120fps and Argus 60fps; rolls back on-disk module if verification fails.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

log_dir="$REPO_ROOT/build/logs"
mkdir -p "$log_dir"
exec >>"$log_dir/boot-verification.log" 2>&1

date -Is
echo "Boot ID: $(cat /proc/sys/kernel/random/boot_id 2>/dev/null || true)"
uname -a

result=0
systemctl stop nvargus-daemon || result=1
if [[ $result == 0 ]]; then
    runuser -u yujin -- bash "$REPO_ROOT/tests/hardware/verify_native.sh" || result=1
fi

systemctl start nvargus-daemon || result=1
timeout -k 3 20 runuser -u yujin -- gst-launch-1.0 -e \
    nvarguscamerasrc sensor-id=0 sensor-mode=1 num-buffers=120 \
    '!' 'video/x-raw(memory:NVMM),width=1920,height=1080,framerate=60/1' \
    '!' fakesink sync=false || result=1

if [[ $result == 0 ]]; then
    echo 'BOOT_VERIFICATION_PASSED: native 120fps and Argus 60fps verified'
else
    echo 'BOOT_VERIFICATION_FAILED: restoring original module on disk; no hot-unload'
    bash "$REPO_ROOT/scripts/driver/install-for-next-boot.sh" --restore || exit 1
fi

systemctl disable imx477-verify-after-boot.service 2>/dev/null || true
exit "$result"
