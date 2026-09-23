#!/usr/bin/env bash
# Static checks for a Doodlebound Android APK. This does not replace phone QA.
# Usage: ANDROID_HOME=/path/to/sdk scripts/android-demo-verify-apk.sh path/to/app.apk
set -euo pipefail

apk=${1:-}
if [ -z "$apk" ] || [ ! -f "$apk" ]; then
    echo "Usage: ANDROID_HOME=/path/to/sdk $0 path/to/app.apk" >&2
    exit 2
fi
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$sdk" ]; then echo "ANDROID_HOME or ANDROID_SDK_ROOT is required" >&2; exit 2; fi
tools="$sdk/build-tools/35.0.0"
for tool in apksigner zipalign aapt; do
    if [ ! -x "$tools/$tool" ]; then echo "Missing $tools/$tool" >&2; exit 2; fi
done

echo "APK: $apk"
sha256sum "$apk"
echo "Size: $(stat -c %s "$apk") bytes"
"$tools/apksigner" verify --verbose --print-certs "$apk"
"$tools/zipalign" -c -P 16 4 "$apk"
"$tools/aapt" dump badging "$apk" | awk '/^package:|^sdkVersion:|^targetSdkVersion:|^native-code:/'
if ! unzip -Z -1 "$apk" | grep -Fx 'lib/arm64-v8a/libdoodlebound.so' >/dev/null; then
    echo "Missing arm64 Doodlebound library" >&2
    exit 1
fi
if ! unzip -Z -1 "$apk" | grep -Fx 'lib/x86_64/libdoodlebound.so' >/dev/null; then
    echo "Missing x86_64 Doodlebound library" >&2
    exit 1
fi
echo "Both demo ABIs and APK signature/alignment are present. Device behavior is still pending."
