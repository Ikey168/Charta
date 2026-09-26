#!/bin/sh
set -eu

repo_root=${GITHUB_WORKSPACE:-.}
cd "$repo_root"

capture_emulator_evidence() {
    adb logcat -d -v time > "$repo_root/android-logcat.txt" || true
    adb shell dumpsys activity activities > "$repo_root/android-activities.txt" || true
}
trap capture_emulator_evidence EXIT

run_filtered_test() {
    recovery_test_filter=$1
    recovery_result_dir="$repo_root/$2"
    recovery_stage=$3
    mkdir -p "$recovery_result_dir"

    if [ -n "$recovery_stage" ]; then
        adb shell am instrument -w -r -e class "$recovery_test_filter" \
            -e acceptanceRecoveryStage "$recovery_stage" \
            com.ikore.doodlebound.test/androidx.test.runner.AndroidJUnitRunner \
            > "$recovery_result_dir/direct-runner.txt" 2>&1
    else
        adb shell am instrument -w -r -e class "$recovery_test_filter" \
            com.ikore.doodlebound.test/androidx.test.runner.AndroidJUnitRunner \
            > "$recovery_result_dir/direct-runner.txt" 2>&1
    fi
    grep -F 'OK (1 test)' "$recovery_result_dir/direct-runner.txt"
}

# sys.boot_completed can flip before the package manager accepts installs; wait for it.
wait_for_package_service() {
    attempts=0
    until adb shell cmd package list packages android > /dev/null 2>&1; do
        attempts=$((attempts + 1))
        if [ "$attempts" -ge 60 ]; then
            echo 'Package service did not become ready within 120 s' >&2
            return 1
        fi
        sleep 2
    done
}

install_apk() {
    for attempt in 1 2 3; do
        wait_for_package_service
        if adb install "$@"; then
            return 0
        fi
        echo "adb install $* failed (attempt $attempt); waiting for the device" >&2
        adb wait-for-device
        sleep 10
    done
    return 1
}

adb wait-for-device
wait_for_package_service
install_apk -r android/app/build/outputs/apk/debug/app-debug.apk
install_apk -r -t android/app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb logcat -c -b main -b crash
adb shell am start -W -n com.ikore.doodlebound/.MainActivity
sleep 8
adb shell pidof com.ikore.doodlebound
adb shell am force-stop com.ikore.doodlebound

run_filtered_test \
    'com.ikore.doodlebound.AcceptanceRecoveryTest#seedRunCheckpointForHostProcessDeath' \
    android/app/build/outputs/androidTest-results/process-death-seed \
    seed

# The seed run leaves the instrumentation's empty activity on top of the app's task; a plain
# start would resurface that task instead of MainActivity, and on API 29 the app process never
# starts. FLAG_ACTIVITY_NEW_TASK | FLAG_ACTIVITY_CLEAR_TASK (0x10008000) launches MainActivity
# in a fresh task. The checkpoint under test lives in SharedPreferences, so this keeps it.
adb shell am start -W -f 0x10008000 -n com.ikore.doodlebound/.MainActivity
restore_result_dir="$repo_root/android/app/build/outputs/androidTest-results/process-death-restore"
mkdir -p "$restore_result_dir"
adb shell pidof com.ikore.doodlebound > "$restore_result_dir/pid-before-force-stop.txt"
test -s "$restore_result_dir/pid-before-force-stop.txt"
adb shell am force-stop com.ikore.doodlebound
adb get-state > /dev/null
if adb shell pidof com.ikore.doodlebound > "$restore_result_dir/pid-after-force-stop.txt"; then
    test ! -s "$restore_result_dir/pid-after-force-stop.txt"
fi
test ! -s "$restore_result_dir/pid-after-force-stop.txt"
printf '%s\n' 'Host force-stop completed; target process is absent.' \
    > "$restore_result_dir/force-stop-result.txt"

run_filtered_test \
    'com.ikore.doodlebound.AcceptanceRecoveryTest#restoreRunCheckpointAfterHostProcessDeath' \
    android/app/build/outputs/androidTest-results/process-death-restore \
    restore
run_filtered_test \
    'com.ikore.doodlebound.AcceptanceRecoveryTest#deniedCameraPermissionOffersNonCameraFallbacks' \
    android/app/build/outputs/androidTest-results/acceptance-denied-camera \
    ''
run_filtered_test \
    'com.ikore.doodlebound.AcceptanceRecoveryTest#cancelledPhotoPickerReturnsToHome' \
    android/app/build/outputs/androidTest-results/acceptance-picker-cancel \
    ''

cd "$repo_root/android"
./gradlew --no-daemon :app:connectedDebugAndroidTest
cd "$repo_root"
adb shell dumpsys activity activities > android-activities.txt
grep -F 'com.ikore.doodlebound' android-activities.txt
