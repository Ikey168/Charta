#!/usr/bin/env bash
# Measure the Android demo against the budgets in docs/ANDROID_DEMO_ACCEPTANCE.md (A23 / #444).
#
# Prints a Markdown report with raw samples and p50/p95 for:
#   - cold launch (am start -W TotalTime) over N launches
#   - frame times of the game's SurfaceView (SurfaceFlinger present timestamps) during play
#   - app PSS sampled during play (peak and first/last, for retained growth)
#   - APK size
# Run against exactly one authorized device with the app already installed. Frame sampling
# needs the operator to start a level; the script waits for Enter. Physical-phone numbers are
# the acceptance evidence; emulator numbers are diagnostic only.
#
# Usage: scripts/android-demo-profile.sh [--launches N] [--play-seconds S] [--apk path.apk] [--out raw-dir]
set -euo pipefail

pkg=com.ikore.doodlebound
activity=$pkg/.MainActivity
launches=20
play_seconds=120
apk=""
out=""
while [ $# -gt 0 ]; do
    case "$1" in
        --launches) launches=$2; shift 2 ;;
        --play-seconds) play_seconds=$2; shift 2 ;;
        --apk) apk=$2; shift 2 ;;
        --out) out=$2; shift 2 ;;
        *) echo "Unknown argument: $1" >&2; exit 2 ;;
    esac
done
case "$launches$play_seconds" in *[!0-9]*) echo "--launches and --play-seconds take integers" >&2; exit 2 ;; esac

adb_bin=${ADB:-adb}
command -v "$adb_bin" >/dev/null 2>&1 || { echo "adb is required" >&2; exit 2; }
if [ "$($adb_bin devices | awk 'NR>1 && $2=="device" {n++} END {print n+0}')" -ne 1 ]; then
    echo "Exactly one authorized device is required" >&2; exit 2
fi
sh_() { $adb_bin shell "$@" | tr -d '\r'; }
sh_ pm path "$pkg" | grep -q package: || { echo "$pkg is not installed" >&2; exit 2; }

raw=${out:-$(mktemp -d "${TMPDIR:-/tmp}/doodlebound-profile.XXXXXX")}
mkdir -p "$raw"

# p50/p95 of a newline-separated list of numbers (nearest-rank).
percentiles() {
    sort -n | awk '{v[NR]=$1} END {
        if (NR == 0) { print "n=0"; exit }
        p50 = v[int((NR * 50 + 99) / 100)]; p95 = v[int((NR * 95 + 99) / 100)]
        printf "n=%d, p50=%s, p95=%s, max=%s", NR, p50, p95, v[NR] }'
}

echo "Measuring $launches cold launches..." >&2
: > "$raw/launch-ms.txt"
for i in $(seq 1 "$launches"); do
    sh_ am force-stop "$pkg"
    sleep 2
    total=$(sh_ am start -W -S -n "$activity" | awk -F': ' '/TotalTime/ {print $2}')
    [ -n "$total" ] && echo "$total" >> "$raw/launch-ms.txt"
    sleep 3
done
launch_stats=$(percentiles < "$raw/launch-ms.txt")

echo >&2
echo "Start a representative level on the phone now, then press Enter to sample ${play_seconds}s of play." >&2
read -r _

layer=$(sh_ dumpsys SurfaceFlinger --list | grep -F "SurfaceView" | grep -F "$pkg" | grep -v -F 'Background' | head -1 || true)
: > "$raw/present-ns.txt"
: > "$raw/pss-kb.txt"
end=$(( $(date +%s) + play_seconds ))
while [ "$(date +%s)" -lt "$end" ]; do
    if [ -n "$layer" ]; then
        # Columns: desired-present, actual-present, frame-ready (ns). Skip the refresh-period
        # header, empty slots (0) and pending fences (INT64_MAX).
        # adb shell re-parses arguments on the device; quote the bracketed layer name for it.
        sh_ dumpsys SurfaceFlinger --latency "'$layer'" |
            awk 'NR > 1 && NF == 3 && $2 != 0 && $2 != "9223372036854775807" {print $2}' >> "$raw/present-ns.txt"
    fi
    pss=$(sh_ dumpsys meminfo "$pkg" | awk '/TOTAL PSS:/ {print $3; exit} /^ *TOTAL / {print $2; exit}')
    [ -n "$pss" ] && echo "$pss" >> "$raw/pss-kb.txt"
    sleep 1
done

if [ -s "$raw/present-ns.txt" ]; then
    sort -n -u "$raw/present-ns.txt" |
        awk 'NR > 1 { d = ($1 - prev) / 1e6; if (d > 0 && d < 1000) printf "%.2f\n", d } { prev = $1 }' \
        > "$raw/frame-ms.txt"
    frame_stats=$(percentiles < "$raw/frame-ms.txt")
    over=$(awk '$1 > 33.3 {n++} END {print n+0}' "$raw/frame-ms.txt")
else
    frame_stats="unavailable (no SurfaceView layer found for $pkg; was a level on screen?)"
    over="n/a"
fi
if [ -s "$raw/pss-kb.txt" ]; then
    pss_peak=$(sort -n "$raw/pss-kb.txt" | tail -1)
    pss_first=$(head -1 "$raw/pss-kb.txt")
    pss_last=$(tail -1 "$raw/pss-kb.txt")
else
    pss_peak=n/a; pss_first=n/a; pss_last=n/a
fi

apk_size="not measured (pass --apk)"
apk_hash=""
if [ -n "$apk" ]; then
    apk_size="$(( $(stat -c %s "$apk") / 1024 / 1024 )) MB ($(stat -c %s "$apk") bytes)"
    apk_hash=$(sha256sum "$apk" | awk '{print $1}')
fi

cat <<REPORT
# Doodlebound performance profile

- Date (UTC): $(date -u +'%Y-%m-%d %H:%M:%S UTC')
- Device: $(sh_ getprop ro.product.manufacturer) $(sh_ getprop ro.product.model), Android $(sh_ getprop ro.build.version.release) (API $(sh_ getprop ro.build.version.sdk)), SoC $(sh_ getprop ro.soc.model)
- Emulator: $( [ "$(sh_ getprop ro.kernel.qemu)" = 1 ] && echo "yes (diagnostic only)" || echo no )
- Installed version: $(sh_ dumpsys package "$pkg" | awk -F= '/versionName=/ {print $2; exit}')
- APK SHA-256: ${apk_hash:-not provided}
- Raw samples: $raw

| Measure | Result | Budget |
| --- | --- | --- |
| Cold launch TotalTime (ms) | $launch_stats | p95 ≤ 3000 over 20 launches |
| Play frame time (ms), ${play_seconds}s | $frame_stats; frames over 33.3 ms: $over | p95 ≤ 33.3 over 30 minutes |
| App PSS (KB) | peak $pss_peak; first $pss_first; last $pss_last | peak ≤ 358400 (350 MB) |
| APK size | $apk_size | ≤ 150 MB |

Frame times come from SurfaceFlinger present timestamps for layer \`${layer:-none}\`.
Launch time is the ActivityManager TotalTime to first frame, not the moment Home is
interactive; confirm interactivity by observation. Capture-to-review and review-to-playable
timings are logged by the app during conversion (see logcat) and are not measured here.
REPORT
