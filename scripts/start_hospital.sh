#!/usr/bin/env bash
# File: scripts/start_hospital.sh

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APP_NAME="hospital_system"
APP_PATH="$PROJECT_ROOT/$APP_NAME"

LOG_DIR="$PROJECT_ROOT/logs"
PID_FILE="$LOG_DIR/hospital.pid"
RUNTIME_LOG="$LOG_DIR/runtime.log"
SCHEDULE_LOG="$LOG_DIR/schedule_log.txt"
MEMORY_LOG="$LOG_DIR/memory_log.txt"

check_only=0

show_usage() {
    echo "Usage:"
    echo "  ./scripts/start_hospital.sh"
    echo "  ./scripts/start_hospital.sh --check-only"
}

say() {
    printf '[%s] START: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1"
}

prepare_logs() {
    mkdir -p "$LOG_DIR"
    touch "$RUNTIME_LOG" "$SCHEDULE_LOG" "$MEMORY_LOG"
}

pid_is_running() {
    local old_pid="$1"

    if [[ -z "$old_pid" ]]; then
        return 1
    fi

    kill -0 "$old_pid" 2>/dev/null
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --check-only)
            check_only=1
            shift
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

cd "$PROJECT_ROOT" || exit 1
prepare_logs

say "project root is $PROJECT_ROOT"
say "logs are ready"

if [[ -f "$PID_FILE" ]]; then
    saved_pid="$(cat "$PID_FILE")"

    if pid_is_running "$saved_pid"; then
        say "hospital simulator already running with PID $saved_pid"
        exit 0
    fi

    say "old PID file found, but process is not running"
    rm -f "$PID_FILE"
fi

if (( check_only == 1 )); then
    say "check-only mode completed"
    exit 0
fi

if [[ ! -x "$APP_PATH" ]]; then
    say "executable $APP_NAME was not found"
    say "run: make all"
    exit 1
fi

say "starting hospital simulator FIFO listener"

nohup "$APP_PATH" --fifo-forever >> "$RUNTIME_LOG" 2>&1 &
new_pid=$!

printf '%s\n' "$new_pid" > "$PID_FILE"
sleep 1

if pid_is_running "$new_pid"; then
    say "hospital simulator started with PID $new_pid"
    say "runtime log: $RUNTIME_LOG"
else
    say "hospital simulator failed to stay running"
    rm -f "$PID_FILE"
    exit 1
fi
