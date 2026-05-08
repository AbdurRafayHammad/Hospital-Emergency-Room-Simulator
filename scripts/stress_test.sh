#!/usr/bin/env bash
# File: scripts/stress_test.sh

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRIAGE_SCRIPT="$PROJECT_ROOT/scripts/triage.sh"
HOSPITAL_APP="$PROJECT_ROOT/hospital_system"
SIM_APP="$PROJECT_ROOT/patient_simulator"
REQUEST_LOG="$PROJECT_ROOT/logs/admission_requests.txt"

mode="triage"
total_patients=12
nurse_count=3
strategy="all"

say() {
    printf '[%s] STRESS: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1"
}

show_usage() {
    echo "Usage:"
    echo "  ./scripts/stress_test.sh"
    echo "  ./scripts/stress_test.sh 30"
    echo "  ./scripts/stress_test.sh --threads 15 4"
    echo "  ./scripts/stress_test.sh --memory all 12"
    echo "  ./scripts/stress_test.sh --fifo 8"
}

choose_bed() {
    case $(( "$1" % 3 )) in
        0) echo "GENERAL" ;;
        1) echo "ICU" ;;
        *) echo "ISOLATION" ;;
    esac
}

choose_severity() {
    case $(( "$1" % 5 )) in
        0) echo "1" ;;
        1) echo "2" ;;
        2) echo "3" ;;
        3) echo "4" ;;
        *) echo "5" ;;
    esac
}

if [[ $# -gt 0 && "$1" == "--threads" ]]; then
    mode="threads"
    total_patients="${2:-12}"
    nurse_count="${3:-3}"
elif [[ $# -gt 0 && "$1" == "--memory" ]]; then
    mode="memory"
    strategy="${2:-all}"
    total_patients="${3:-12}"
elif [[ $# -gt 0 && "$1" == "--fifo" ]]; then
    mode="fifo"
    total_patients="${2:-8}"
elif [[ $# -eq 1 ]]; then
    total_patients="$1"
elif [[ $# -gt 1 ]]; then
    show_usage
    exit 1
fi

if ! [[ "$total_patients" =~ ^[0-9]+$ ]] || (( total_patients == 0 )); then
    echo "Error: patient count must be positive"
    exit 1
fi

if [[ "$mode" == "threads" ]]; then
    if ! [[ "$nurse_count" =~ ^[0-9]+$ ]] || (( nurse_count == 0 )); then
        echo "Error: nurse count must be positive"
        exit 1
    fi

    say "running thread stress test patients=$total_patients nurses=$nurse_count"
    "$HOSPITAL_APP" --thread-demo "$total_patients" "$nurse_count"
    exit $?
fi

if [[ "$mode" == "memory" ]]; then
    say "running memory stress test strategy=$strategy patients=$total_patients"
    "$HOSPITAL_APP" --memory-demo "$strategy" "$total_patients"
    exit $?
fi

if [[ "$mode" == "fifo" ]]; then
    say "running FIFO stress test patients=$total_patients"
    "$HOSPITAL_APP" --fifo "$total_patients" &
    hospital_pid=$!
    sleep 1
    "$SIM_APP" "$total_patients"
    wait "$hospital_pid"
    exit $?
fi

if [[ ! -x "$TRIAGE_SCRIPT" ]]; then
    echo "Error: triage script is not executable"
    echo "Run: chmod +x scripts/*.sh"
    exit 1
fi

say "creating $total_patients simulated patient requests"

i=1
while (( i <= total_patients )); do
    "$TRIAGE_SCRIPT" \
        --name "StressPatient_$i" \
        --age "$(( 18 + (i * 7) % 65 ))" \
        --severity "$(choose_severity "$i")" \
        --bed "$(choose_bed "$i")" \
        --units "$(( 4 + (i * 3) % 22 ))"

    i=$(( i + 1 ))
    sleep 0.1
done

say "stress test completed"
say "records saved in $REQUEST_LOG"
tail -n 5 "$REQUEST_LOG"
