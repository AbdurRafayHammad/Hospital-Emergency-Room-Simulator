#!/usr/bin/env bash
# File: scripts/triage.sh

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
REQUEST_LOG="$LOG_DIR/admission_requests.txt"

patient_name=""
patient_age=""
severity=""
bed_type=""
care_units=""

show_usage() {
    echo "Usage:"
    echo "  ./scripts/triage.sh"
    echo "  ./scripts/triage.sh --name NAME --age AGE --severity 1-5 --bed GENERAL|ICU|ISOLATION --units N"
}

ask_missing_value() {
    local label="$1"
    local current_value="$2"

    if [[ -z "$current_value" ]]; then
        read -r -p "$label: " current_value
    fi

    printf '%s' "$current_value"
}

valid_number() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

normalise_bed_type() {
    case "${1^^}" in
        GENERAL) echo "GENERAL" ;;
        ICU) echo "ICU" ;;
        ISOLATION) echo "ISOLATION" ;;
        *) return 1 ;;
    esac
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --name)
            patient_name="${2:-}"
            shift 2
            ;;
        --age)
            patient_age="${2:-}"
            shift 2
            ;;
        --severity)
            severity="${2:-}"
            shift 2
            ;;
        --bed)
            bed_type="${2:-}"
            shift 2
            ;;
        --units)
            care_units="${2:-}"
            shift 2
            ;;
        --help)
            show_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            show_usage
            exit 1
            ;;
    esac
done

mkdir -p "$LOG_DIR"

patient_name="$(ask_missing_value "Patient name" "$patient_name")"
patient_age="$(ask_missing_value "Patient age" "$patient_age")"
severity="$(ask_missing_value "Severity 1-critical to 5-stable" "$severity")"
bed_type="$(ask_missing_value "Bed type GENERAL/ICU/ISOLATION" "$bed_type")"
care_units="$(ask_missing_value "Estimated care units" "$care_units")"

if [[ -z "$patient_name" ]]; then
    echo "Error: patient name cannot be empty"
    exit 1
fi

if ! valid_number "$patient_age" || (( patient_age < 0 || patient_age > 120 )); then
    echo "Error: age must be a number between 0 and 120"
    exit 1
fi

if ! [[ "$severity" =~ ^[1-5]$ ]]; then
    echo "Error: severity must be from 1 to 5"
    exit 1
fi

if ! bed_type="$(normalise_bed_type "$bed_type")"; then
    echo "Error: bed type must be GENERAL, ICU, or ISOLATION"
    exit 1
fi

if ! valid_number "$care_units" || (( care_units == 0 )); then
    echo "Error: care units must be a positive number"
    exit 1
fi

patient_id="P$(date +%Y%m%d%H%M%S)-$$"
timestamp="$(date '+%Y-%m-%d %H:%M:%S')"
record="$timestamp|$patient_id|$patient_name|$patient_age|$severity|$bed_type|$care_units"

printf '%s\n' "$record" >> "$REQUEST_LOG"

echo "[$timestamp] TRIAGE accepted patient=$patient_id severity=$severity bed=$bed_type units=$care_units"
echo "Record saved in: $REQUEST_LOG"
