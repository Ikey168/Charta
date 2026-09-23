#!/usr/bin/env bash
# Print the device and artifact identity needed for the Android demo QA record.
# Usage: scripts/android-demo-device-report.sh [path/to/app.apk]
set -euo pipefail

adb_bin=${ADB:-adb}
if ! command -v "$adb_bin" >/dev/null 2>&1; then
    echo "adb is required; install Android platform-tools and set ADB if needed" >&2
    exit 2
fi

device_count=$($adb_bin devices | awk 'NR>1 && $2=="device" {n++} END {print n+0}')
if [ "$device_count" -ne 1 ]; then
    echo "Exactly one authorized device is required; adb reports $device_count" >&2
    $adb_bin devices -l >&2
    exit 2
fi

prop() { $adb_bin shell getprop "$1" | tr -d '\r'; }
shell() { $adb_bin shell "$@" | tr -d '\r'; }

echo "# Doodlebound Android device report"
echo
echo "- Date (UTC): $(date -u +'%Y-%m-%d %H:%M:%S UTC')"
echo "- Commit: $(git -C "$(dirname "$0")/.." rev-parse HEAD)"
echo "- Model: $(prop ro.product.model)"
echo "- Manufacturer: $(prop ro.product.manufacturer)"
echo "- Android: $(prop ro.build.version.release) (API $(prop ro.build.version.sdk))"
echo "- Build fingerprint: $(prop ro.build.fingerprint)"
echo "- ABIs: $(prop ro.product.cpu.abilist)"
echo "- Page size: $(shell getconf PAGE_SIZE) bytes"
echo "- GPU hint: $(prop ro.hardware.egl)"
echo "- Display: $(shell wm size | head -1)"
echo "- Density: $(shell wm density | head -1)"
if [ "${1:-}" != "" ]; then
    if [ ! -f "$1" ]; then echo "APK not found: $1" >&2; exit 2; fi
    echo "- APK: $1"
    echo "- APK SHA-256: $(sha256sum "$1" | awk '{print $1}')"
    echo "- APK bytes: $(stat -c %s "$1")"
fi
