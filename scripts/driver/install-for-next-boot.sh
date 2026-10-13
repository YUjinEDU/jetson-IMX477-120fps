#!/usr/bin/env bash
# Install compiled nv_imx477.ko to disk for the next boot.
# WARNING: NEVER hot-unload nv_imx477 (modprobe -r caused kernel panic).
# Changes only take effect after a clean system reboot.
set -euo pipefail
[[ $EUID == 0 ]] || { echo 'Run with sudo.' >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

KERNEL="$(uname -r)"
TARGET="/lib/modules/$KERNEL/updates/drivers/media/i2c/nv_imx477.ko"
BACKUP_DIR="$REPO_ROOT/build/backup"
BACKUP="$BACKUP_DIR/nv_imx477-stock.ko"

if [[ "${1:-}" == "--restore" ]]; then
    if [[ ! -f "$BACKUP" ]]; then
        echo "No backup file found at $BACKUP" >&2
        exit 1
    fi
    install -m 644 "$BACKUP" "$TARGET"
    depmod -a "$KERNEL"
    echo "Restored original driver to $TARGET. Reboot required to take effect."
    exit 0
fi

SOURCE="${1:-$REPO_ROOT/build/driver/nv_imx477.ko}"
if [[ ! -f "$SOURCE" ]]; then
    echo "Driver binary not found at $SOURCE. Run build-driver.sh first." >&2
    exit 1
fi

# Verify vermagic matches running kernel
if [[ "$(modinfo -F vermagic "$SOURCE")" != "$KERNEL "* ]]; then
    echo "Vermagic mismatch: driver built for different kernel than $KERNEL." >&2
    exit 1
fi

mkdir -p "$BACKUP_DIR"
if [[ -f "$TARGET" && ! -f "$BACKUP" ]]; then
    echo "Creating initial backup of currently installed module..."
    cp -a "$TARGET" "$BACKUP"
    echo "Backup saved to $BACKUP (SHA256: $(sha256sum "$BACKUP" | cut -d ' ' -f1))"
fi

echo "Installing $SOURCE to $TARGET..."
install -m 644 "$SOURCE" "$TARGET"
depmod -a "$KERNEL"
echo "Module staged on disk. Running module unchanged; reboot required to load new driver."
echo "SHA256: $(sha256sum "$TARGET" | cut -d ' ' -f1)"
echo "To restore previous version: sudo bash $0 --restore"
