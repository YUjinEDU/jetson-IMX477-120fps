#!/usr/bin/env bash
set -euo pipefail

if [[ "${EUID}" -ne 0 ]]; then
    echo "실행 방법: sudo $0" >&2
    exit 1
fi

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
KERNEL_VERSION="$(uname -r)"
MODULE_DST="/lib/modules/${KERNEL_VERSION}/updates/drivers/media/i2c/nv_imx477.ko"
DTBO_DST="/boot/tegra234-p3767-camera-p3768-imx477-A.dtbo"
STAMP="$(date +%Y%m%d-%H%M%S)"
BACKUP_DIR="/root/imx477-120fps-backup-${STAMP}"

[[ -f "${ROOT_DIR}/artifacts/nv_imx477.ko" ]] || { echo "nv_imx477.ko가 없습니다." >&2; exit 1; }
[[ -f "${ROOT_DIR}/artifacts/tegra234-p3767-camera-p3768-imx477-A.dtbo" ]] || {
    echo "IMX477 120fps DTBO가 없습니다." >&2
    exit 1
}

mkdir -p "${BACKUP_DIR}"
if [[ -f "${MODULE_DST}" ]]; then
    cp -a "${MODULE_DST}" "${BACKUP_DIR}/nv_imx477.ko"
fi
if [[ -f "${DTBO_DST}" ]]; then
    cp -a "${DTBO_DST}" "${BACKUP_DIR}/tegra234-p3767-camera-p3768-imx477-A.dtbo"
fi

install -o root -g root -m 0644 \
    "${ROOT_DIR}/artifacts/nv_imx477.ko" "${MODULE_DST}"
install -o root -g root -m 0644 \
    "${ROOT_DIR}/artifacts/tegra234-p3767-camera-p3768-imx477-A.dtbo" "${DTBO_DST}"
depmod -a "${KERNEL_VERSION}"

echo "설치 완료"
echo "백업 위치: ${BACKUP_DIR}"
echo "다음 부팅부터 IMX477 mode2(1332x990, 120fps)가 추가됩니다."
echo "적용 명령: sudo reboot"
