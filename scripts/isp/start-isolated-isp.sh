#!/usr/bin/env bash
# Root-only, temporary Argus experiment; never changes system libraries, boot, or driver.
# Runs an isolated, transient nvargus-daemon service with custom NITO and 600s automatic timeout.
set -euo pipefail
[[ $EUID == 0 ]] || { echo 'Run with sudo' >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

mode="${1:-$REPO_ROOT/artifacts/local/isp/imx477-mode012-candidate.nito}"
backend="${2:-stock}"

if [[ "$mode" != "CONFIG" && ! -s "$mode" ]]; then
    echo "Error: NITO file not found or empty: $mode" >&2
    exit 2
fi

if [[ "$backend" != "hotfix" && "$backend" != "stock" ]]; then
    echo "Error: backend must be 'hotfix' or 'stock'" >&2
    exit 2
fi

if systemctl is-active --quiet imx477-isp-isolated.service; then
    echo "Isolated service already running. Stop it with: sudo systemctl stop imx477-isp-isolated.service" >&2
    exit 2
fi

log_dir="$REPO_ROOT/build/logs/isp"
mkdir -p "$log_dir"
stamp=$(date +%Y%m%d-%H%M%S)
log="$log_dir/isolated-$backend-$stamp.log"
dump="$log_dir/binary-$stamp.cfg"

envs=(--setenv="NVCAMERA_NITO_PATH=$mode" --setenv="NVCAMERA_NITO_DUMP_PATH=$dump")
if [[ "$backend" == "hotfix" ]]; then
    hotfix_lib="$REPO_ROOT/third_party/local/hotfix-extracted/usr/lib/aarch64-linux-gnu/nvidia"
    if [[ -d "$hotfix_lib" ]]; then
        envs+=(--setenv="LD_LIBRARY_PATH=$hotfix_lib")
    fi
fi

systemctl stop nvargus-daemon.service
if ! systemd-run --unit=imx477-isp-isolated --collect --property=Type=exec \
    --property=RuntimeMaxSec=600 --property=TimeoutStopSec=20 \
    --property='ExecStopPost=/usr/bin/systemctl start nvargus-daemon.service' \
    --property="StandardOutput=append:$log" --property="StandardError=append:$log" \
    "${envs[@]}" /usr/sbin/nvargus-daemon; then
    systemctl start nvargus-daemon.service
    exit 1
fi

echo "ISOLATED_LOG=$log"
echo "BINARY_DUMP=$dump"
echo "Stop with: sudo systemctl stop imx477-isp-isolated.service"
