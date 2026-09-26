#!/usr/bin/env bash
# Build and archive a signed Android demo from the current commit.
# Signing credentials must be supplied through the four DOODLEBOUND_* variables.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
sdk=${ANDROID_HOME:-${ANDROID_SDK_ROOT:-}}
if [ -z "$sdk" ]; then echo "Set ANDROID_HOME to the pinned Android SDK" >&2; exit 2; fi
for name in DOODLEBOUND_KEYSTORE DOODLEBOUND_KEY_ALIAS DOODLEBOUND_STORE_PASSWORD DOODLEBOUND_KEY_PASSWORD; do
    if [ -z "${!name:-}" ]; then echo "Set $name for release signing" >&2; exit 2; fi
done
if [ ! -f "$DOODLEBOUND_KEYSTORE" ]; then echo "Release keystore was not found" >&2; exit 2; fi
if [ -n "$(git -C "$repo" status --porcelain --untracked-files=normal)" ]; then
    echo "Commit source changes before packaging so the artifact has an exact source revision" >&2
    exit 2
fi
commit=$(git -C "$repo" rev-parse HEAD)

(
    cd "$repo/android"
    ./gradlew --no-daemon :app:assembleRelease
)
if [ "$(git -C "$repo" rev-parse HEAD)" != "$commit" ] ||
   [ -n "$(git -C "$repo" status --porcelain --untracked-files=normal)" ]; then
    echo "Source changed during release build; rerun after committing a stable revision" >&2
    exit 2
fi

apk="$repo/android/app/build/outputs/apk/release/app-release.apk"
if [ ! -f "$apk" ]; then echo "Signed release APK was not produced" >&2; exit 1; fi
version=$($sdk/build-tools/35.0.0/aapt dump badging "$apk" | sed -n "s/^package:.*versionCode='\([^']*\)'.*/\1/p" | head -1)
if [ -z "$version" ]; then echo "Could not read APK version code" >&2; exit 1; fi
output="$repo/android/build/demo-artifacts/v${version}-${commit:0:12}"
if [ -e "$output" ]; then echo "Artifact directory already exists: $output" >&2; exit 2; fi
declare -A native_paths
for abi in arm64-v8a x86_64; do
    native=$(find "$repo/android/app/build/intermediates/cxx/RelWithDebInfo" -type f -path "*/obj/$abi/libdoodlebound.so" -print -quit)
    if [ -z "$native" ]; then echo "Missing unstripped $abi native library" >&2; exit 1; fi
    if ! "$sdk/ndk/28.2.13676358/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf" -S "$native" | grep -F '.debug_info' >/dev/null; then
        echo "$abi native library lacks debug symbols; check Release CMake flags" >&2
        exit 1
    fi
    native_paths[$abi]=$native
done
mkdir -p "$output/symbols"
cp "$apk" "$output/doodlebound-demo.apk"
"$repo/scripts/android-demo-verify-apk.sh" "$output/doodlebound-demo.apk" > "$output/apk-verification.txt"
for abi in arm64-v8a x86_64; do
    mkdir -p "$output/symbols/$abi"
    cp "${native_paths[$abi]}" "$output/symbols/$abi/libdoodlebound.so"
done
(
    cd "$output"
    sha256sum doodlebound-demo.apk symbols/*/libdoodlebound.so > SHA256SUMS
)
{
    echo "Commit: $commit"
    echo "Version code: $version"
    echo "Built (UTC): $(date -u +'%Y-%m-%d %H:%M:%S UTC')"
    echo "APK and native symbols: see SHA256SUMS"
    echo "Device acceptance: pending until the exact APK hash passes the matrix"
} > "$output/BUILD.txt"
echo "$output"
