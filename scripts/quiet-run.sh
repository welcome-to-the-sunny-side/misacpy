#!/usr/bin/env bash
# Run a command with CPU-frequency noise minimized:
#   - sets the cpufreq governor to "performance" on all CPUs
#   - disables turbo / boost (Intel intel_pstate and ACPI cpufreq paths)
#   - pins the command to a single core via taskset
#
# Original kernel settings are restored on exit (success, failure, Ctrl-C).
# Requires sudo for the sysfs writes; sudo credentials are warmed up once
# at the start so the run is not interrupted by a password prompt.
#
# Usage:
#   ./scripts/quiet-run.sh <command> [args...]
#
# Env:
#   CORE   core id to pin to (default: 2)

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 <command> [args...]" >&2
    exit 2
fi

core="${CORE:-2}"

if [[ $EUID -eq 0 ]]; then
    SUDO=()
else
    SUDO=(sudo)
fi

intel_no_turbo="/sys/devices/system/cpu/intel_pstate/no_turbo"
acpi_boost="/sys/devices/system/cpu/cpufreq/boost"

orig_no_turbo=""
orig_boost=""
orig_governors=()

write_sysfs() {
    local value="$1" path="$2"
    echo "$value" | "${SUDO[@]}" tee "$path" >/dev/null
}

read_state() {
    if [[ -r $intel_no_turbo ]]; then
        orig_no_turbo="$(cat "$intel_no_turbo")"
    fi
    if [[ -r $acpi_boost ]]; then
        orig_boost="$(cat "$acpi_boost")"
    fi
    for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [[ -r $gov ]] || continue
        orig_governors+=("$gov=$(cat "$gov")")
    done
}

apply_quiet() {
    if [[ -n $orig_no_turbo ]]; then
        write_sysfs 1 "$intel_no_turbo"
    fi
    if [[ -n $orig_boost ]]; then
        write_sysfs 0 "$acpi_boost"
    fi
    for gov in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
        [[ -e $gov ]] || continue
        write_sysfs performance "$gov"
    done
}

restore() {
    set +e
    if [[ -n $orig_no_turbo ]]; then
        write_sysfs "$orig_no_turbo" "$intel_no_turbo"
    fi
    if [[ -n $orig_boost ]]; then
        write_sysfs "$orig_boost" "$acpi_boost"
    fi
    for entry in ${orig_governors[@]+"${orig_governors[@]}"}; do
        local path="${entry%%=*}"
        local value="${entry#*=}"
        write_sysfs "$value" "$path"
    done
    set -e
}

# Warm sudo creds so the timed run isn't interrupted by a password prompt.
"${SUDO[@]}" -v

read_state
trap restore EXIT INT TERM
apply_quiet

echo "[quiet-run] pinned to core $core, governor=performance, turbo=off" >&2

status=0
taskset -c "$core" "$@" || status=$?
exit "$status"
