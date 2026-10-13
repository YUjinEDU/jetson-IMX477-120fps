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
BACKUP_DIR="/var/lib/imx477-120fps/$KERNEL"
BACKUP="$BACKUP_DIR/nv_imx477-pre-install.ko"

validate_module() {
    [[ -s "$1" ]] || { echo "Empty/missing module: $1" >&2; exit 1; }
    [[ "$(modinfo -F vermagic "$1")" == "$KERNEL "* ]] || {
        echo "Vermagic mismatch: $1" >&2; exit 1;
    }
}

save_backup() {
    validate_module "$TARGET"
    install -d -m 700 "$BACKUP_DIR"
    if [[ ! -e "$BACKUP" ]]; then
        install -m 600 "$TARGET" "$BACKUP"
        (cd "$BACKUP_DIR" && sha256sum nv_imx477-pre-install.ko > SHA256SUMS)
    fi
    validate_module "$BACKUP"
    (cd "$BACKUP_DIR" && sha256sum -c SHA256SUMS)
    echo "Pre-install backup: $BACKUP (not necessarily a factory module)"
}

if [[ "${1:-}" == "--backup-only" ]]; then
    save_backup
    exit 0
fi

if [[ "${1:-}" == "--restore" ]]; then
    if [[ ! -s "$BACKUP" ]]; then
        echo "No backup file found at $BACKUP" >&2
        exit 1
    fi
    validate_module "$BACKUP"
    (cd "$BACKUP_DIR" && sha256sum -c SHA256SUMS)
    install -m 644 "$BACKUP" "$TARGET"
    depmod -a "$KERNEL"
    echo "Restored pre-install driver to $TARGET. Reboot required to take effect."
    exit 0
fi

SOURCE="${1:-$REPO_ROOT/build/driver/nv_imx477.ko}"
if [[ ! -s "$SOURCE" ]]; then
    echo "Driver binary not found at $SOURCE. Run build-driver.sh first." >&2
    exit 1
fi

validate_module "$SOURCE"
save_backup

echo "Installing $SOURCE to $TARGET..."
install -m 644 "$SOURCE" "$TARGET"
depmod -a "$KERNEL"
echo "Module staged on disk. Running module unchanged; reboot required to load new driver."
echo "SHA256: $(sha256sum "$TARGET" | cut -d ' ' -f1)"
echo "To restore previous version: sudo bash $0 --restore"
