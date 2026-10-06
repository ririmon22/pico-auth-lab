#!/usr/bin/env bash
set -euo pipefail
if (( $# < 3 )); then
    echo "Usage: $0 off|on|unknown CPU OUTPUT_PREFIX [benchmark options]" >&2
    exit 2
fi
label=$1
cpu=$2
prefix=$3
shift 3
case "$label" in off|on|unknown) ;; *) echo 'Bad label' >&2; exit 2;; esac
for arg in "$@"; do
    case "$arg" in
        -o*|--output*|--label*|-c*|--cpu*|-h|--help)
            echo 'Do not override output, label, or CPU through extra options' >&2; exit 2;;
    esac
done
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
[[ -x ./tme_bench ]] || { echo 'Run make first' >&2; exit 2; }
mkdir -p -- "$(dirname -- "$prefix")"
# CSVs grow across invocations; environment and console logs stay per-run.
set -o noclobber
hash_before=$(sha256sum ./tme_bench | cut -d ' ' -f 1)
if [[ -e "$prefix.csv.sha256" ]]; then
    [[ "$(cat "$prefix.csv.sha256")" == "$hash_before" ]] || {
        echo 'Binary hash changed; use a new prefix to keep comparisons reproducible' >&2; exit 2;
    }
else
    printf '%s\n' "$hash_before" > "$prefix.csv.sha256"
fi
logprefix="$prefix.$(date -u +%Y%m%dT%H%M%S%N)-$$"
{
    date -Is
    printf 'DECLARED_TME=%s\nCPU=%s\n' "$label" "$cpu"
    printf 'COMMAND: '; printf '%q ' ./tme_bench --label "$label" -c "$cpu" -o "$prefix.csv" "$@"; echo
    uname -a
    lscpu
    gcc --version
    sha256sum ./tme_bench
    free -m
    printf '\nCPU/cache/frequency metadata\n'
    for f in /sys/devices/system/cpu/cpu"$cpu"/cache/index*/{level,type,size,shared_cpu_list} \
             /sys/devices/system/cpu/cpu"$cpu"/topology/{thread_siblings_list,core_id,physical_package_id} \
             /sys/devices/system/cpu/cpu"$cpu"/cpufreq/{scaling_governor,scaling_min_freq,scaling_max_freq,scaling_cur_freq} \
             /sys/devices/system/cpu/intel_pstate/no_turbo \
             /sys/devices/system/cpu/smt/active \
             /sys/kernel/mm/transparent_hugepage/{enabled,defrag} \
             /sys/devices/system/clocksource/clocksource0/current_clocksource; do
        if [[ -r "$f" ]]; then printf '%s: ' "$f"; cat "$f"; fi
    done
    printf '\nMemory lock limit: '; ulimit -l
    printf '\nCPU info:\n'; cat /proc/cpuinfo
    printf '\nInterrupts:\n'; cat /proc/interrupts || true
    printf '\nSensors:\n'; if command -v sensors >/dev/null; then sensors || true; fi
    printf '\nTME activation MSR (read only, if rdmsr already permitted):\n'
    if command -v rdmsr >/dev/null; then rdmsr -p "$cpu" 0x982 || true; else echo 'not read'; fi
} > "$logprefix.meta.txt" 2>&1
./tme_bench --label "$label" -c "$cpu" -o "$prefix.csv" "$@" 2>&1 | tee -- "$logprefix.txt"
hash_after=$(sha256sum ./tme_bench | cut -d ' ' -f 1)
[[ "$hash_before" == "$hash_after" ]] || { echo 'Binary changed during run; discard this experiment' >&2; exit 1; }
